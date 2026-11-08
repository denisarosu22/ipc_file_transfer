#include "../common/log.h"
#include "../common/net.h"
#include "../common/protocol.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <csignal>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {
    constexpr int kExitUsageError = 2;
    constexpr const char* kDefaultOutputDir = "received";

    constexpr int kListenBacklog = 8;
    constexpr int kReceiveTimeoutSeconds = 30;
    constexpr int kAcceptPollMilliseconds = 500;

    // Global because a signal handler cannot receive parameters. Atomic because on
    // Windows the Ctrl+C handler runs on another thread.
    std::atomic<bool> g_stopRequested{ false };
    static_assert(std::atomic<bool>::is_always_lock_free, "needed in a signal handler");

    void OnStopSignal(int)
    {
        g_stopRequested = true;
    }

    class BadRequest : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    class TempFileGuard
    {
    public:
        explicit TempFileGuard(fs::path path)
            : m_path(std::move(path))
        {
        }

        ~TempFileGuard()
        {
            if (m_committed)
            {
                return;
            }
            std::error_code ec;
            fs::remove(m_path, ec);
            if (ec)
            {
                try
                {
                    logger::Error("cannot remove temporary file '" + m_path.u8string() + "': " + ec.message());
                }
                catch (...)
                {
                }
            }
        }

        TempFileGuard(const TempFileGuard&) = delete;
        TempFileGuard& operator=(const TempFileGuard&) = delete;

        const fs::path& Path() const noexcept { return m_path; }
        void Commit() noexcept { m_committed = true; }

    private:
        fs::path m_path;
        bool m_committed = false;
    };

    fs::path ValidateFileName(const std::string& rawName)
    {
        if (rawName.find('\0') != std::string::npos)
        {
            throw BadRequest("file name contains a NUL character");
        }
        const fs::path name = fs::u8path(rawName);
        if (name.empty() || name != name.filename() || name.has_root_path() ||
            name == "." || name == ".." ||
            rawName.find('/') != std::string::npos || rawName.find('\\') != std::string::npos)
        {
            throw BadRequest("invalid file name '" + rawName + "'");
        }
        return name;
    }

    void SendStatus(const net::Socket& client, protocol::Status status)
    {
        const auto byte = static_cast<std::uint8_t>(status);
        net::SendAll(client, &byte, sizeof(byte));
    }

    fs::path ReceiveFile(const net::Socket& client, const fs::path& outputDir)
    {
        const std::uint32_t magic = protocol::ReceiveU32(client);
        if (magic != protocol::kMagic)
        {
            throw BadRequest("unknown protocol (bad magic number)");
        }
        const std::uint32_t nameLength = protocol::ReceiveU32(client);
        if (nameLength == 0 || nameLength > protocol::kMaxFileNameLength)
        {
            throw BadRequest("file name length " + std::to_string(nameLength) + " is out of range");
        }
        const std::uint64_t fileSize = protocol::ReceiveU64(client);

        std::string rawName(nameLength, '\0');
        net::ReceiveAll(client, rawName.data(), rawName.size());
        const fs::path fileName = ValidateFileName(rawName);

        const fs::path finalPath = outputDir / fileName;
        logger::Info("receiving '" + rawName + "' (" + std::to_string(fileSize) + " bytes)");

        // The guard must be declared before the stream: the file is closed first, then deleted.
        TempFileGuard tempFile(fs::path(finalPath).concat(".part"));
        {
            std::ofstream output(tempFile.Path(), std::ios::binary | std::ios::trunc);
            if (!output)
            {
                throw std::runtime_error("cannot create '" + tempFile.Path().u8string() + "'");
            }

            std::vector<char> buffer(protocol::kChunkSize);
            std::uint64_t remaining = fileSize;
            while (remaining > 0)
            {
                const std::size_t chunk =
                    static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
                net::ReceiveAll(client, buffer.data(), chunk);
                output.write(buffer.data(), static_cast<std::streamsize>(chunk));
                if (!output)
                {
                    throw std::runtime_error("write error on '" + tempFile.Path().u8string() + "'");
                }
                remaining -= chunk;
            }

            output.close();
            if (!output)
            {
                throw std::runtime_error("cannot flush/close '" + tempFile.Path().u8string() + "'");
            }
        }

        std::error_code ec;
        fs::rename(tempFile.Path(), finalPath, ec);
        if (ec)
        {
            throw std::runtime_error("cannot rename to '" + finalPath.u8string() + "': " + ec.message());
        }
        tempFile.Commit();
        return finalPath;
    }

    void HandleClient(const net::Socket& client, const std::string& peer, const fs::path& outputDir)
    {
        try
        {
            net::SetReceiveTimeout(client, kReceiveTimeoutSeconds);
            const fs::path stored = ReceiveFile(client, outputDir);
            SendStatus(client, protocol::Status::Ok);
            logger::Info("saved '" + stored.u8string() + "' from " + peer);
            return;
        }
        catch (const BadRequest& e)
        {
            logger::Error("client " + peer + ": bad request: " + e.what());
            try { SendStatus(client, protocol::Status::BadRequest); }
            catch (...) {}
        }
        catch (const std::exception& e)
        {
            logger::Error("client " + peer + ": " + e.what());
            try { SendStatus(client, protocol::Status::ServerError); }
            catch (...) {}
        }
        catch (...)
        {
            logger::Error("client " + peer + ": unknown exception");
        }
    }

} // namespace

int main(int argc, char* argv[])
{
    logger::EnableUtf8Console();
    if (argc > 3)
    {
        logger::Error(std::string("usage: ") + argv[0] + " [port] [output_dir]");
        return kExitUsageError;
    }

    try
    {
        const std::string port = argc > 1 ? argv[1] : protocol::kDefaultPort;
        const fs::path outputDir = argc > 2 ? fs::path(argv[2]) : fs::path(kDefaultOutputDir);

        std::error_code ec;
        fs::create_directories(outputDir, ec);
        if (ec || !fs::is_directory(outputDir))
        {
            throw std::runtime_error("cannot use output directory '" + outputDir.u8string() + "'" +
                (ec ? ": " + ec.message() : std::string()));
        }

        if (std::signal(SIGINT, OnStopSignal) == SIG_ERR ||
            std::signal(SIGTERM, OnStopSignal) == SIG_ERR)
        {
            throw std::runtime_error("cannot install the Ctrl+C handler");
        }

        const net::NetworkInit networkInit;
        const net::Socket listener = net::ListenOn(port, kListenBacklog);
        logger::Info("listening on port " + port + ", saving files to '" +
            fs::absolute(outputDir).u8string() + "' (Ctrl+C to stop)");

        while (!g_stopRequested)
        {
            if (!net::WaitForClient(listener, kAcceptPollMilliseconds))
            {
                continue;
            }
            std::string peer;
            const net::Socket client = net::AcceptClient(listener, peer);
            logger::Info("client connected: " + peer);
            HandleClient(client, peer, outputDir);
        }

        logger::Info("stop requested, shutting down");
        return EXIT_SUCCESS;
    }
    catch (const std::exception& e)
    {
        logger::Error(e.what());
    }
    catch (...)
    {
        logger::Error("unknown exception");
    }
    return EXIT_FAILURE;
}