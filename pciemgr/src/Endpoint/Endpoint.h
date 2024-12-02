#pragma once

#include <CommonDef.h>

#include <functional>
#include <memory>

#include "../Net/NetPacket.h"

namespace mgrd {
    struct Endpoint
    {
    private:
        u32          m_Id;
        net::Socket* m_Socket;

    public:
        Endpoint() noexcept = default;
        Endpoint(const u32 id, net::Socket* const socket) noexcept;
        ~Endpoint() noexcept;

    public:
        [[nodiscard]] u32          GetID() const noexcept { return m_Id; }
        [[nodiscard]] net::Socket* GetSocket() const noexcept { return m_Socket; }
        [[nodiscard]] bool         IsConnected() const noexcept { return m_Socket && m_Socket->connected; }
    };
} // namespace mgrd
