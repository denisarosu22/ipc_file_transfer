#pragma once

#include "net.h"

#include <cstddef>
#include <cstdint>

// Client -> server: magic(u32) | name length(u32) | file size(u64) | name | data
// Server -> client: status(u8). Integers are big-endian.
namespace protocol {

    constexpr std::uint32_t kMagic = 0x46545031;
    constexpr std::uint32_t kMaxFileNameLength = 255;
    constexpr std::size_t kChunkSize = 64 * 1024;
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

    inline void SendU32(const net::Socket& socket, std::uint32_t value)
    {
        unsigned char bytes[4];
        for (int i = 3; i >= 0; --i)
        {
            bytes[i] = static_cast<unsigned char>(value & 0xFF);
            value >>= 8;
        }
        net::SendAll(socket, bytes, sizeof(bytes));
    }

    inline void SendU64(const net::Socket& socket, std::uint64_t value)
    {
        unsigned char bytes[8];
        for (int i = 7; i >= 0; --i)
        {
            bytes[i] = static_cast<unsigned char>(value & 0xFF);
            value >>= 8;
        }
        net::SendAll(socket, bytes, sizeof(bytes));
    }

    inline std::uint32_t ReceiveU32(const net::Socket& socket)
    {
        unsigned char bytes[4];
        net::ReceiveAll(socket, bytes, sizeof(bytes));
        std::uint32_t value = 0;
        for (unsigned char byte : bytes)
        {
            value = (value << 8) | byte;
        }
        return value;
    }

    inline std::uint64_t ReceiveU64(const net::Socket& socket)
    {
        unsigned char bytes[8];
        net::ReceiveAll(socket, bytes, sizeof(bytes));
        std::uint64_t value = 0;
        for (unsigned char byte : bytes)
        {
            value = (value << 8) | byte;
        }
        return value;
    }

} // namespace protocol