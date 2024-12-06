#pragma once

#include <CommonDef.h>

#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

#include <Logex.h>

#include <Core/Error.h>
#include <Core/Result.h>
#include <Endpoint/Endpoint.h>
#include <Net/NetPacket.h>

namespace pmgrd::net {
    class NetHandler
    {
    public:
        using PacketDelegate = std::function<Result<Err>(Endpoint&, net::Packet&&)>;

    private:
        lgx::Logger&                                        m_Logger;
        net::Socket*                                        m_Socket;
        std::atomic<bool>                                   m_Run;
        std::queue<std::pair<Endpoint&, net::Packet>>       m_PacketQueue;
        std::mutex                                          m_PacketQueueMutex;
        std::thread                                         m_PacketDispatcherThread;
        std::unordered_map<net::PacketType, PacketDelegate> m_PacketMap;
        std::mutex                                          m_EndpointThreadMutex;
        std::list<std::thread>                              m_EndpointThreads;
        std::list<Endpoint>                                 m_ConnectedEndpoints;

    public:
        NetHandler(lgx::Logger& logger, net::Socket* socket) noexcept;
        ~NetHandler() noexcept;

    public:
        void Stop() noexcept { m_Run.store(false); }

    public:
        void        AddPacket(const net::PacketType type, PacketDelegate delegate) noexcept;
        Result<Err> BeginAccept() noexcept;
        void        BeginPacketDispatch() noexcept;

    private:
        void HandleEndpoint(Endpoint& ep) noexcept;
        void ThreadHandler() noexcept;
    };
} // namespace pmgrd::net
