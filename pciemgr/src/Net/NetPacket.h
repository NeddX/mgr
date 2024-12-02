#pragma once

#include <CommonDef.h>

#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "CSSocket.h"

namespace mgrd::net {
    // Bring the CS Socket namespace here.
    using namespace csnet;

    enum class PacketType : u8
    {
    Reboot,
        String
    };

    [[nodiscard]] std::string_view TypeToStr(const PacketType type) noexcept;

    struct PacketHeader
    {
        PacketType type;
        u8        dataLen;
    };

    struct Packet
    {
    public:
        PacketHeader    header;
        std::vector<u8> data;

    public:
        constexpr Packet() noexcept = default;
        constexpr Packet(const PacketHeader header) noexcept
            : header(header)
            , data()
        {
        }
        constexpr Packet(const PacketHeader header, std::vector<u8> data) noexcept
            : header(header)
            , data(std::move(data))
        {
        }

    public:
        template <typename T>
            requires(std::is_standard_layout_v<T>)
        constexpr Packet& operator<<(const T& data) noexcept
        {
            const usize prev_size = this->data.size();
            this->data.resize(prev_size + sizeof(data));
            std::memcpy(this->data.data() + prev_size, &data, sizeof(T));
            this->header.dataLen = this->data.size();
            return *this;
        }
        template <>
        constexpr Packet& operator<<(const std::vector<u8>& data) noexcept
        {
            this->data.insert(this->data.end(), data.begin(), data.end());
            this->header.dataLen = this->data.size();
            return *this;
        }
        template <>
        constexpr Packet& operator<<(const std::string& data) noexcept
        {
            this->data.insert(this->data.end(), data.begin(), data.end());
            this->header.dataLen = this->data.size();
            return *this;
        }
        template <>
        constexpr Packet& operator<<(const std::string_view& data) noexcept
        {
            this->data.insert(this->data.end(), data.begin(), data.end());
            this->header.dataLen = this->data.size();
            return *this;
        }
        template <typename T>
            requires(std::is_standard_layout_v<T>)
        constexpr Packet& operator>>(T& data) noexcept
        {
            const usize total_size = this->data.size();
            std::memcpy(&data, this->data.data() + (total_size - sizeof(T)), sizeof(T));
            this->data.resize(total_size - sizeof(T));
            this->header.dataLen = this->data.size();
            return *this;
        }
        template <>
        constexpr Packet& operator>>(std::vector<u8>& data) noexcept
        {
            data.insert(data.end(), this->data.begin(), this->data.end());
            this->data.clear();
            return *this;
        }
        template <>
        constexpr Packet& operator>>(std::string& data) noexcept
        {
            data.insert(data.end(), this->data.begin(), this->data.end());
            this->data.clear();
            return *this;
        }
    };

    std::optional<Packet> BeginReceive(csnet::Socket* socket) noexcept;
    bool                  BeginSend(csnet::Socket* socket, Packet&& packet) noexcept;
} // namespace mgrd::net
