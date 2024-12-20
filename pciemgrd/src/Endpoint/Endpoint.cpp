#include "Endpoint.h"

namespace pmgrd {
    Endpoint::Endpoint(const u8 id, net::Socket* const socket) noexcept
        : m_Id(id)
        , m_Socket(socket)
    {
    }

    Endpoint::~Endpoint() noexcept
    {
        if (m_Socket)
        {
            Socket_Dispose(m_Socket);
            m_Socket = nullptr;
        }
        m_Id = 0;
    }
} // namespace pmgrd
