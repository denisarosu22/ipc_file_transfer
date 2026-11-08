#include "../common/log.h"
#include "../common/net.h"
#include "../common/protocol.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

    void PrintUsage(const char* program)
    {
        logger::Error(std::string("usage: ") + program + " <file> [host] [port]");
    }

    void SendFile(const fs::path& filePath, const std::string& host, const std::string& port)
    {
        std::error_code ec;
        if (!fs::is_regular_file(filePath, ec))
        {
            throw std::runtime_error("'" + filePath.u8string() + "' is not an existing regular file" +
                (ec ? ": " + ec.message() : std::string()));
        }

        const std::uintmax_t fileSize = fs::file_size(filePath, ec);
        if (ec)
        {
            throw std::runtime_error("cannot get size of '" + filePath.u8string() + "': " + ec.message());
        }

        const std::string fileName = filePath.filename().u8string();
        if (fileName.empty() || fileName.size() > protocol::kMaxFileNameLength)
        {
            throw std::runtime_error("file name must be 1.." +
                std::to_string(protocol::kMaxFileNameLength) + " bytes long");
        }

        std::ifstream input(filePath, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("cannot open '" + filePath.u8string() + "' for reading");
        }

        logger::Info("connecting to " + host + ":" + port + " ...");
        const net::Socket socket = net::ConnectTo(host, port);
        logger::Info("connected, sending '" + fileName + "' (" + std::to_string(fileSize) + " bytes)");

        protocol::SendU32(socket, protocol::kMagic);
        protocol::SendU32(socket, static_cast<std::uint32_t>(fileName.size()));
        protocol::SendU64(socket, static_cast<std::uint64_t>(fileSize));
        net::SendAll(socket, fileName.data(), fileName.size());

        std::vector<char> buffer(protocol::kChunkSize);
        std::uintmax_t remaining = fileSize;
        while (remaining > 0)
        {
            const std::size_t toRead =
                static_cast<std::size_t>(std::min<std::uintmax_t>(remaining, buffer.size()));
            input.read(buffer.data(), static_cast<std::streamsize>(toRead));
            const std::streamsize got = input.gcount();
            if (got <= 0)
            {
                throw std::runtime_error("read error: the file is shorter than expected "
                    "(was it modified during the transfer?)");
            }
            net::SendAll(socket, buffer.data(), static_cast<std::size_t>(got));
            remaining -= static_cast<std::uintmax_t>(got);
        }

        std::uint8_t reply = 0;
        net::ReceiveAll(socket, &reply, sizeof(reply));
        const auto status = static_cast<protocol::Status>(reply);
        if (status != protocol::Status::Ok)
        {
            throw std::runtime_error(std::string("server rejected the file: ") +
                protocol::StatusText(status));
        }
        logger::Info("done: the server confirmed that the file was saved");
    }

} // namespace

int main(int argc, char* argv[])
{
    logger::EnableUtf8Console();
    if (argc < 2 || argc > 4)
    {
        PrintUsage(argc > 0 ? argv[0] : "file_client");
        return 2;
    }

    try
    {
        const fs::path filePath = fs::path(argv[1]);
        const std::string host = argc > 2 ? argv[2] : "127.0.0.1";
        const std::string port = argc > 3 ? argv[3] : protocol::kDefaultPort;

        const net::NetworkInit networkInit;
        SendFile(filePath, host, port);
        return 0;
    }
    catch (const std::exception& e)
    {
        logger::Error(e.what());
    }
    catch (...)
    {
        logger::Error("unknown exception");
    }
    return 1;
}

