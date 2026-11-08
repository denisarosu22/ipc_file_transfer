#include "net.h"
#include "log.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <memory>
#include <system_error>

#ifndef _WIN32
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/select.h>
#  include <sys/time.h>
#endif

namespace net {

    namespace {

#if defined(MSG_NOSIGNAL)
        constexpr int kSendFlags = MSG_NOSIGNAL;
#else
        constexpr int kSendFlags = 0;
#endif

        constexpr std::size_t kMaxIoChunk = 1024 * 1024;

        bool IsInterrupted(int code) noexcept
        {
#ifdef _WIN32
            return code == WSAEINTR;
#else
            return code == EINTR;
#endif
        }

        bool IsAbortedConnection(int code) noexcept
        {
#ifdef _WIN32
            return code == WSAECONNRESET;
#else
            return code == ECONNABORTED;
#endif
        }

        int CloseNative(NativeSocket handle) noexcept
        {
#ifdef _WIN32
            return ::closesocket(handle);
#else
            return ::close(handle);
#endif
        }

        struct AddrInfoDeleter
        {
            void operator()(addrinfo* info) const noexcept { ::freeaddrinfo(info); }
        };
        using AddrInfoPtr = std::unique_ptr<addrinfo, AddrInfoDeleter>;

        AddrInfoPtr Resolve(const char* host, const std::string& port, int family, int flags)
        {
            addrinfo hints{};
            hints.ai_family = family;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;
            hints.ai_flags = flags;

            addrinfo* result = nullptr;
            const int rc = ::getaddrinfo(host, port.c_str(), &hints, &result);
            if (rc != 0)
            {
                const std::string target = std::string(host ? host : "*") + ":" + port;
#ifdef _WIN32
                throw NetworkError("getaddrinfo(" + target + ")", rc);
#else
                if (rc == EAI_SYSTEM)
                {
                    throw NetworkError("getaddrinfo(" + target + ")", errno);
                }
                throw std::runtime_error("getaddrinfo(" + target + ") failed: " + ::gai_strerror(rc));
#endif
            }
            return AddrInfoPtr(result);
        }

        Socket CreateSocket(const addrinfo& info)
        {
            Socket socket(::socket(info.ai_family, info.ai_socktype, info.ai_protocol));
            if (!socket.IsValid())
            {
                throw NetworkError("socket()", LastError());
            }

#if defined(SO_NOSIGPIPE)
            int on = 1;
            if (::setsockopt(socket.Get(), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)) != 0)
            {
                throw NetworkError("setsockopt(SO_NOSIGPIPE)", LastError());
            }
#endif
            return socket;
        }

        std::string DescribeAddress(const sockaddr_in& address)
        {
            std::array<char, INET_ADDRSTRLEN> ip{};
            if (::inet_ntop(AF_INET, &address.sin_addr, ip.data(), ip.size()) == nullptr)
           
            {
                return "<unknown>";
            }
            return std::string(ip.data()) + ":" + std::to_string(ntohs(address.sin_port));
        }

    } // namespace

    NetworkError::NetworkError(const std::string& operation, int code)
        : std::runtime_error(operation + " failed: " + ErrorText(code) +
            " (code " + std::to_string(code) + ")")
        , m_code(code)
    {
    }

    int LastError() noexcept
    {
#ifdef _WIN32
        return ::WSAGetLastError();
#else
        return errno;
#endif
    }

    std::string ErrorText(int code)
    {
        return std::system_category().message(code);
    }

    NetworkInit::NetworkInit()
    {
#ifdef _WIN32
        WSADATA data{};
        const int rc = ::WSAStartup(MAKEWORD(2, 2), &data);
        if (rc != 0)
        {
            throw NetworkError("WSAStartup()", rc);
        }
#endif
    }

    NetworkInit::~NetworkInit()
    {
#ifdef _WIN32
        if (::WSACleanup() != 0)
        {
            try
            {
                logger::Error("WSACleanup() failed: " + ErrorText(LastError()));
            }
            catch (...)
            {
            }
        }
#endif
    }

    Socket::Socket(NativeSocket handle) noexcept
        : m_handle(handle)
    {
    }

    Socket::~Socket()
    {
        Close();
    }

    Socket::Socket(Socket&& other) noexcept
        : m_handle(other.m_handle)
    {
        other.m_handle = kInvalidSocket;
    }

    Socket& Socket::operator=(Socket&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            m_handle = other.m_handle;
            other.m_handle = kInvalidSocket;
        }
        return *this;
    }

    void Socket::Close() noexcept
    {
        if (m_handle == kInvalidSocket)
        {
            return;
        }
        const NativeSocket handle = m_handle;
        m_handle = kInvalidSocket; // never close the same handle twice
        if (CloseNative(handle) != 0)
        {
            try
            {
                logger::Error("closing socket failed: " + ErrorText(LastError()));
            }
            catch (...)
            {
            }
        }
    }

    Socket ConnectTo(const std::string& host, const std::string& port)
    {
        const AddrInfoPtr addresses = Resolve(host.c_str(), port, AF_UNSPEC, 0);

        int lastError = 0;
        for (const addrinfo* info = addresses.get(); info != nullptr; info = info->ai_next)
        {
            Socket socket = CreateSocket(*info);
            if (::connect(socket.Get(), info->ai_addr, static_cast<int>(info->ai_addrlen)) == 0)
            {
                return socket;
            }
            lastError = LastError();
        }
        throw NetworkError("connect(" + host + ":" + port + ")", lastError);
    }

    Socket ListenOn(const std::string& port, int backlog)
    {
        const AddrInfoPtr addresses = Resolve(nullptr, port, AF_INET, AI_PASSIVE);
        const addrinfo& info = *addresses;

        Socket listener = CreateSocket(info);

#ifndef _WIN32
        int on = 1;
        if (::setsockopt(listener.Get(), SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0)
        {
            throw NetworkError("setsockopt(SO_REUSEADDR)", LastError());
        }
#endif

        if (::bind(listener.Get(), info.ai_addr, static_cast<int>(info.ai_addrlen)) != 0)
        {
            throw NetworkError("bind(port " + port + ")", LastError());
        }
        if (::listen(listener.Get(), backlog) != 0)
        {
            throw NetworkError("listen()", LastError());
        }
        return listener;
    }

    bool WaitForClient(const Socket& listener, int timeoutMs)
    {
        fd_set readSet{};
        FD_ZERO(&readSet);
        FD_SET(listener.Get(), &readSet);

        timeval timeout{};
        timeout.tv_sec = timeoutMs / 1000;
        timeout.tv_usec = (timeoutMs % 1000) * 1000;

        const int nfds = static_cast<int>(listener.Get()) + 1; // ignored on Windows
        const int rc = ::select(nfds, &readSet, nullptr, nullptr, &timeout);
        if (rc < 0)
        {
            const int code = LastError();
            if (IsInterrupted(code))
            {
                return false;
            }
            throw NetworkError("select()", code);
        }
        return rc > 0;
    }

    Socket AcceptClient(const Socket& listener, std::string& peerDescription)
    {
        for (;;)
        {
            sockaddr_in address{};
            socklen_t length = sizeof(address);
            Socket client(::accept(listener.Get(), reinterpret_cast<sockaddr*>(&address), &length));
            if (client.IsValid())
            {
                peerDescription = DescribeAddress(address);
                return client;
            }
            const int code = LastError();
            if (!IsInterrupted(code) && !IsAbortedConnection(code))
            {
                throw NetworkError("accept()", code);
            }
        }
    }

    void SetReceiveTimeout(const Socket& socket, int seconds)
    {
#ifdef _WIN32
        const DWORD timeout = static_cast<DWORD>(seconds) * 1000;
#else
        timeval timeout{};
        timeout.tv_sec = seconds;
#endif
        if (::setsockopt(socket.Get(), SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0)
        {
            throw NetworkError("setsockopt(SO_RCVTIMEO)", LastError());
        }
    }

    void SendAll(const Socket& socket, const void* data, std::size_t size)
    {
        const char* cursor = static_cast<const char*>(data);
        while (size > 0)
        {
            const std::size_t chunk = std::min(size, kMaxIoChunk);
            const auto sent = ::send(socket.Get(), cursor, static_cast<int>(chunk), kSendFlags);
            if (sent < 0)
            {
                const int code = LastError();
                if (IsInterrupted(code))
                {
                    continue;
                }
                throw NetworkError("send()", code);
            }
            cursor += sent;
            size -= static_cast<std::size_t>(sent);
        }
    }

    void ReceiveAll(const Socket& socket, void* data, std::size_t size)
    {
        char* cursor = static_cast<char*>(data);
        while (size > 0)
        {
            const std::size_t chunk = std::min(size, kMaxIoChunk);
            const auto received = ::recv(socket.Get(), cursor, static_cast<int>(chunk), 0);
            if (received < 0)
            {
                const int code = LastError();
                if (IsInterrupted(code))
                {
                    continue;
                }
                throw NetworkError("recv()", code);
            }
            if (received == 0)
            {
                throw std::runtime_error("connection closed by peer before all data was received");
            }
            cursor += received;
            size -= static_cast<std::size_t>(received);
        }
    }

} // namespace net