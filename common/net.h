#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <netdb.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace net {

#ifdef _WIN32
    using NativeSocket = SOCKET;
    constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
    using NativeSocket = int;
    constexpr NativeSocket kInvalidSocket = -1;
#endif

    class NetworkError : public std::runtime_error
    {
    public:
        NetworkError(const std::string& operation, int code);

        int Code() const noexcept { return m_code; }

    private:
        int m_code;
    };

    int LastError() noexcept;

    std::string ErrorText(int code);

    class NetworkInit
    {
    public:
        NetworkInit();
        ~NetworkInit();

        NetworkInit(const NetworkInit&) = delete;
        NetworkInit& operator=(const NetworkInit&) = delete;
    };

    class Socket
    {
    public:
        Socket() noexcept = default;
        explicit Socket(NativeSocket handle) noexcept;
        ~Socket();

        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;

        Socket(Socket&& other) noexcept;
        Socket& operator=(Socket&& other) noexcept;

        bool IsValid() const noexcept { return m_handle != kInvalidSocket; }
        NativeSocket Get() const noexcept { return m_handle; }

       
        void Close() noexcept;

    private:
        NativeSocket m_handle = kInvalidSocket;
    };

    Socket ConnectTo(const std::string& host, const std::string& port);

    Socket ListenOn(const std::string& port, int backlog);

    bool WaitForClient(const Socket& listener, int timeoutMs);

    Socket AcceptClient(const Socket& listener, std::string& peerDescription);
    void SetReceiveTimeout(const Socket& socket, int seconds);
    void SendAll(const Socket& socket, const void* data, std::size_t size);
    void ReceiveAll(const Socket& socket, void* data, std::size_t size);

}