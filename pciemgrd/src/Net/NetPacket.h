#pragma once
#include <CommonDef.h>

#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "CSSocket.h"

#include <Core/Error.h>
#include <Core/Result.h>

/**
 * @namespace pmgrd::net
 * @brief The namespace containing most of the networking functionality here.
 * */
namespace pmgrd::net {
    // Bring the CS Socket namespace here.
    using namespace csnet;

    /**
     * @brief Contains all the packet types.
     *
     * @note Do not forget to add the appropriate entry for @ref s_PacketTypeStr inside NetPacket.cpp!
     * */
    enum class PacketType : u8
    {
        NoOp,          ///< Represents an empty packet.
        Ready,         ///< This type of packet is sent when Endpoint's first connect to RC. The packet contains
                       /// information about the Endpoint.
        Ok,            ///< Indicates that the operation performed by the previous packet succeded.
        Reboot,        ///< Requests the RC to reboot.
        String,        ///< Contains ASCII string information.
        Err,           ///< Contains an Err object.
        GetCrewConfig, ///< Requests the crew station configuration.
        GetCtrConfig,  ///< Requests the concentrator configuration.
        Join,          ///< Packet indicating to join a multicast group.
        Leave          ///< Packet indicating to leave a multicast group.
    };

    /**
     * @brief The packet header. Contains the type and length of the incoming payload.
     *
     * @details When a network transaction happens, the sending side always first sends @ref PacketHeader which
     * contains the payload size, since @re PacketHeader has a fixed size, the receiving side first knows
     * how many bytes to wait for before receiving the payload which has variable length.
     * After receiving the packet header, the receiving side then decodes the packet to determine how many bytes
     * is the payload.
     * The bytes coming after a @ref PacketHeader is guaranteed to be the payload itself.
     * */
    struct PacketHeader
    {
        PacketType type    = PacketType::NoOp;
        u32        dataLen = 0;
    };

    /**
     * @brief The packet iself. Contains the packet header (@see PacketHeader) and the data.
     *
     * @details This structure also contains helpful constructors, method and overloaded operators
     * for packing and unpacking data.
     * */
    struct Packet
    {
    public:
        PacketHeader    header;
        std::vector<u8> data;

    public:
        constexpr Packet() noexcept = default;
        constexpr Packet(const PacketType type) noexcept { header.type = type; }
        constexpr Packet(const PacketType type, std::vector<u8> data) noexcept
            : data(std::move(data))
        {
            header.type    = type;
            header.dataLen = data.size();
        }
        constexpr Packet(const PacketType type, const std::string_view str) noexcept
        {
            data           = std::vector<u8>{ str.begin(), str.end() };
            header.type    = type;
            header.dataLen = data.size();
        }
        constexpr Packet(const std::string_view str) noexcept
        {
            data           = std::vector<u8>{ str.begin(), str.end() };
            header.type    = net::PacketType::String;
            header.dataLen = data.size();
        }
        constexpr Packet(const types::Ok) noexcept { header.type = PacketType::Ok; }
        constexpr Packet(const Err& err) noexcept
        {
            header.type = PacketType::Err;

            // Allocate space for ErrType.
            header.dataLen = sizeof(Err::m_Type);
            data.resize(sizeof(Err::m_Type));

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
        [[nodiscard]] PacketType Type() const noexcept { return header.type; }
        [[nodiscard]] u8*        Data() noexcept { return data.data(); }
        [[nodiscard]] const u8*  Data() const noexcept { return data.data(); }
        [[nodiscard]] u8         Size() const noexcept { return data.size(); }
        [[nodiscard]] constexpr  operator bool() const noexcept { return Type() != PacketType::Err; }

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
        /**
         * @brief Short-hand method for creating a packet that says everything went well.
         *
         * @return Packet of @ref PacketType::Ok
         *  */
        [[nodiscard]] static inline Packet Ok() noexcept { return Packet{ PacketType::Ok }; }
    };

    /**
     * @brief Utility function for receiving @ref Packet s.
     *
     * @returns @ref ValuedResult of @ref Packet or @ref Err.
     * The packet failed to be retrieved, then an @see Err is returned,
     * otherwise @ref Packet is returned.
     * */
    ValuedResult<Packet, Err> BeginReceive(csnet::Socket* socket) noexcept;

    /**
     * @brief Utility function for sending @ref Packet s.
     *
     * @returns @ref Result of @ref Err.
     * The packet failed to be sent, then an @see Err is returned.
     * */
    Result<Err> BeginSend(csnet::Socket* socket, Packet&& packet) noexcept;

    /**
     * @brief Utility function for retriving the string representation
     * of a packet type.
     *
     * @returns @ref std::string_view containing the string representation.
     * */
    [[nodiscard]] std::string_view TypeToStr(const PacketType type) noexcept;

    /**
     * @brief Utility function for retriving the string representation
     * of a packet type.
     *
     * @returns @ref std::string_view containing the string representation.
     * */
    [[nodiscard]] std::string_view TypeToStr(const Packet& packet) noexcept;
} // namespace pmgrd::net
