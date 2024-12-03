#pragma once

#include <CommonDef.h>

#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "CSSocket.h"

#include <Core/Error.h>

namespace pciemgr::net {
    // Bring the CS Socket namespace here.
    using namespace csnet;

    enum class PacketType : u8
    {
        NoOp,
        Ok,
        Reboot,
        String,
        Error
    };

    struct PacketHeader
    {
        PacketType type    = PacketType::NoOp;
        u8         dataLen = 0;
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
        constexpr Packet(const Err& err) noexcept
        {
            header.type = PacketType::Error;

            // Allocate space for ErrType.
            header.dataLen = sizeof(Err);
            data.resize(sizeof(Err));

            // Write the ErrType.
            *(reinterpret_cast<ErrType*>(data.data())) = err.Type();

            // Write the message (if any).
            if (err.HasMessage())
            {
                const auto msg = err.Message();
                header.dataLen += err.Message().size();
                data.insert(data.end(), msg.begin(), msg.end());
            }
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
        template <>
        inline Packet& operator>>(Err& err) noexcept
        {
            err.m_Type = *reinterpret_cast<ErrType*>(data.data());
            if (data.size() > sizeof(ErrType))
            {
                err.m_Message.resize(data.size() - sizeof(ErrType));
                std::memcpy(err.m_Message.data(), data.data() + sizeof(ErrType), err.m_Message.size());
            }
            data.clear();
            return *this;
        }

    public:
        /*
        ** @brief Short-hand method for creating a packet that says everything went well.
        ** @return Packet of PacketType::Ok
        */
        [[nodiscard]] static inline Packet Ok() noexcept { return Packet{ { .type = PacketType::Ok } }; }
    };

    std::optional<Packet>          BeginReceive(csnet::Socket* socket) noexcept;
    bool                           BeginSend(csnet::Socket* socket, Packet&& packet) noexcept;
    [[nodiscard]] std::string_view TypeToStr(const PacketType type) noexcept;
    [[nodiscard]] std::string_view TypeToStr(const Packet& packet) noexcept;
} // namespace pciemgr::net
