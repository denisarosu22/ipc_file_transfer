#pragma once

#include "net.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Client - server: magic(u32) | name length(u32) | file size(u64) | name | data
// Server - client: status(u8). Integers are big-endian.
namespace protocol {

    constexpr std::uint32_t kMagic = 0x46545031;
    constexpr std::uint32_t kMaxFileNameLength = 255;
    constexpr std::size_t kChunkSize = 64 * 1024;
    constexpr const char* kDefaultHost = "127.0.0.1";
    constexpr const char* kDefaultPort = "5555";

    enum class Status : std::uint8_t
    {
        Ok = 0,
        BadRequest = 1,
        ServerError = 2
    };

    inline const char* StatusText(Status status)
    {
        switch (status)
        {
        case Status::Ok:          return "OK";
        case Status::BadRequest:  return "bad request";
        case Status::ServerError: return "server error";
        }
        return "unknown status";
    }

    template <typename T>
    void SendInteger(const net::Socket& socket, T value)
    {
        static_assert(std::is_unsigned_v<T>, "only unsigned integers are sent");
        std::array<unsigned char, sizeof(T)> bytes{};
        for (auto it = bytes.rbegin(); it != bytes.rend(); ++it)
        {
            *it = static_cast<unsigned char>(value);
            value >>= CHAR_BIT;
        }
        net::SendAll(socket, bytes.data(), bytes.size());
    }

    template <typename T>
    T ReceiveInteger(const net::Socket& socket)
    {
        static_assert(std::is_unsigned_v<T>, "only unsigned integers are received");
        std::array<unsigned char, sizeof(T)> bytes{};
        net::ReceiveAll(socket, bytes.data(), bytes.size());
        T value = 0;
        for (const unsigned char byte : bytes)
        {
            value = static_cast<T>((value << CHAR_BIT) | byte);
        }
        return value;
    }

    inline void SendU32(const net::Socket& socket, std::uint32_t value) { SendInteger(socket, value); }
    inline void SendU64(const net::Socket& socket, std::uint64_t value) { SendInteger(socket, value); }
    inline std::uint32_t ReceiveU32(const net::Socket& socket) { return ReceiveInteger<std::uint32_t>(socket); }
    inline std::uint64_t ReceiveU64(const net::Socket& socket) { return ReceiveInteger<std::uint64_t>(socket); }

} // namespace protocol