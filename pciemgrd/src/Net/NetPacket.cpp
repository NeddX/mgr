#include "NetPacket.h"

namespace pmgrd::net {
    /* clang-format off */
    static std::string_view s_PacketTypeStr[] =
    {
    "NoOp",
    "InitConn",
    "Ok",
    "Reboot",
    "String",
    "Error",
    "GetConfig",
    "Join",
    "Leave"
    };
    /* clang-format on */

    ValuedResult<Packet, Err> BeginReceive(csnet::Socket* socket) noexcept
    {
        Packet incoming_packet{};

        // Receive the header.
        if (Socket_Receive(socket, reinterpret_cast<u8*>(&incoming_packet.header), sizeof(incoming_packet.header), 0) !=
            CS_SOCKET_ERROR)
        {
            // Receive the payload (if any).
            if (incoming_packet.header.dataLen > 0)
            {
                incoming_packet.data.resize(incoming_packet.header.dataLen);
                if (Socket_Receive(socket, reinterpret_cast<u8*>(incoming_packet.data.data()),
                                   incoming_packet.header.dataLen, 0) != CS_SOCKET_ERROR)
                    return std::move(incoming_packet);
            }
            else
                return std::move(incoming_packet);
        }
        return Err{ ErrType::NetBadPacket };
    }

    Result<Err> BeginSend(csnet::Socket* socket, Packet&& packet) noexcept
    {
        if (Socket_Send(socket, reinterpret_cast<u8*>(&packet.header), sizeof(packet.header), 0) != CS_SOCKET_ERROR)
        {
            if (packet.header.dataLen > 0)
            {
                const auto res = Socket_Send(socket, reinterpret_cast<u8*>(packet.data.data()), packet.data.size(), 0);
                if (res != CS_SOCKET_ERROR)
                    return Ok();
            }
            else
                return Ok();
        }
        return Err{ ErrType::NetWriteFailure };
    }

    [[nodiscard]] std::string_view TypeToStr(const PacketType type) noexcept
    {
        return s_PacketTypeStr[static_cast<u8>(type)];
    }

    [[nodiscard]] std::string_view TypeToStr(const Packet& packet) noexcept
    {
        return s_PacketTypeStr[static_cast<u8>(packet.header.type)];
    }
} // namespace pmgrd::net
