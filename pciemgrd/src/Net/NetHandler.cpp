#include "NetHandler.h"

#include <nlohmann/json.hpp>

namespace pmgrd::net {
    NetHandler::NetHandler(lgx::Logger& logger, net::Socket* socket) noexcept
        : m_Logger(logger)
        , m_Socket(socket)
        , m_Run(true)
    {
    }

    NetHandler::~NetHandler() noexcept
    {
        if (m_PacketDispatcherThread.joinable())
            m_PacketDispatcherThread.join();

        for (auto& e : m_EndpointThreads)
        {
            if (e.joinable())
                e.join();
        }
    }

    void NetHandler::AddPacket(const net::PacketType type, PacketDelegate delegate) noexcept
    {
        m_PacketMap[type] = std::move(delegate);
    }

    Result<Err> NetHandler::BeginAccept() noexcept
    {
        while (m_Run.load())
        {
            m_Logger.Log(lgx::Level::Info, "Waiting for an endpoint...");

            net::Socket* potential_ep = net::Socket_Accept(m_Socket);
            if (potential_ep)
            {
                m_Logger.Log(__func__, lgx::Level::Info, "A connection is being made by ({}:{})...",
                             potential_ep->remote_ep.address.str, potential_ep->remote_ep.port);
                m_Logger.Log(__func__, lgx::Level::Info, "Waiting for a Ready packet from ({}:{})...",
                             potential_ep->remote_ep.address.str, potential_ep->remote_ep.port);

                // Try and receive the Ready packet.
                if (auto initcon = net::BeginReceive(potential_ep); !initcon)
                {
                    m_Logger.Log(__func__, lgx::Level::Error,
                                 "({}:{}) failed to respond with a Ready packet! Disconnecting...",
                                 potential_ep->remote_ep.address.str, potential_ep->remote_ep.port);
                    net::Socket_Dispose(potential_ep);
                }
                else
                {
                    u8 id;
                    initcon.Unwrap() >> id;

                    m_Logger.Log(__func__, lgx::Level::Info, "EP#{} connected as ({}:{}).", id,
                                 potential_ep->remote_ep.address.str, potential_ep->remote_ep.port);

                    // Acknowledge the InitCon.
                    net::BeginSend(potential_ep, Ok());

                    // Setup as an endpoint for communication.
                    m_ConnectedEndpoints.emplace_back(id, potential_ep);
                    m_EndpointThreads.emplace_back(&NetHandler::HandleEndpoint, this,
                                                   std::ref(m_ConnectedEndpoints.back()));
                }
            }
        }
        return Ok();
    }

    void NetHandler::BeginPacketDispatch() noexcept
    {
        m_PacketDispatcherThread =
            std::thread{ [this]()
                         {
                             while (m_Run.load())
                             {
                                 std::scoped_lock lock{ m_PacketQueueMutex };

                                 while (!m_PacketQueue.empty())
                                 {
                                     auto [owner, packet] = std::move(m_PacketQueue.front());
                                     const auto type      = packet.header.type;
                                     m_PacketQueue.pop();

                                     if (const auto it = m_PacketMap.find(packet.header.type); it != m_PacketMap.end())
                                     {
                                         if (auto result = (it->second)(owner, std::move(packet)); !result)
                                         {
                                             const auto err = result.UnwrapErr();
                                             m_Logger.Log(lgx::Level::Error, "An Error Occured!\n\t{}", err);

                                             // Send the error to the client.
                                             net::BeginSend(owner.GetSocket(), err);
                                         }
                                         // else
                                         //  Tell the client that everything went well.
                                         //  net::BeginSend(owner.GetSocket(), net::Packet::Ok());
                                     }
                                     else
                                         m_Logger.Log(__func__, lgx::Level::Info, "Dropped {} packet.",
                                                      net::TypeToStr(type));
                                 }
                             }
                         } };
    }

    void NetHandler::HandleEndpoint(Endpoint& ep) noexcept
    {
        while (ep.IsConnected())
        {
            auto packet = net::BeginReceive(ep.GetSocket());
            if (packet)
            {
                std::scoped_lock lock{ m_PacketQueueMutex };

                m_PacketQueue.emplace(ep, packet.Unwrap());
            }
        }
    }
} // namespace pmgrd::net
