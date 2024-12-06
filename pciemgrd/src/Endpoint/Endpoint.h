#pragma once

#include <CommonDef.h>

#include <memory>
#include <thread>

#include <Core/Error.h>
#include <Core/Result.h>
#include <Net/NetPacket.h>

namespace pmgrd {
    struct Endpoint
    {
    private:
        u8           m_Id;
        net::Socket* m_Socket;

    public:
        Endpoint() noexcept = default;
        Endpoint(const u8 id, net::Socket* const socket) noexcept;
        ~Endpoint() noexcept;

    public:
        [[nodiscard]] u8           GetID() const noexcept { return m_Id; }
        [[nodiscard]] net::Socket* GetSocket() const noexcept { return m_Socket; }

        [[nodiscard]] bool IsConnected() const noexcept { return m_Socket && m_Socket->connected; }

    public:
        inline Result<Err> Send(net::Packet&& packet) noexcept { return net::BeginSend(m_Socket, std::move(packet)); }
    };
} // namespace pmgrd
