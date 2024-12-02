#include "NetPacket.h"

namespace mgrd::net {
    static std::string_view s_PacketTypeStr[] =
        {
        "Start",
       "Stop",
       "String"
    };

    [[nodiscard]] std::string_view TypeToStr(const PacketType type) noexcept
    {
        return s_PacketTypeStr[static_cast<u8>(type)];
    }

    std::optional<Packet> BeginReceive(csnet::Socket* socket) noexcept
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
                    return incoming_packet;
            }
        }
        return std::nullopt;
    }

    bool BeginSend(csnet::Socket* socket, Packet&& packet) noexcept
    {
        if (Socket_Send(socket, reinterpret_cast<u8*>(&packet.header), sizeof(packet.header), 0) != CS_SOCKET_ERROR)
        {
            if (Socket_Send(socket, reinterpret_cast<u8*>(packet.data.data()), packet.data.size(), 0) !=
                CS_SOCKET_ERROR)
                return true;
        }
        return false;
    }
} // namespace mgrd::net
