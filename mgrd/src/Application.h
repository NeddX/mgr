#pragma once

#include <CommonDef.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <Logex.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "CLI/CLI.h"
#include "Camera/Camera.h"
#include "Endpoint/Endpoint.h"
#include "Net/NetPacket.h"

namespace mgrd {
    class Application
    {
    public:
        static constexpr auto DBusInterfaceName    = "com.example.mgrd";
        static constexpr auto RootServerPort       = 7777;
        static constexpr auto RootServerIP         = "127.0.0.1";
        static constexpr auto RootMaximumEndpoints = 10;

    public:
        using ArgDelegate      = bool (Application::*)(const std::string_view);
        using EndpointDelegate = void (Application::*)(const Endpoint);
        using PacketDelegate   = bool (Application::*)(net::Packet&&);

    private:
        bool                                                m_DaemonMode;
        bool                                                m_RootComplex;
        lgx::Logger::Properties                             m_LoggerProperties;
        std::unique_ptr<lgx::Logger>                        m_Logger;
        std::ofstream                                       m_LogFile;
        net::Socket*                                        m_Socket;
        net::IPEndPoint                                     m_Ep;
        std::thread                                         m_ShellThread;
        std::thread                                         m_PacketDispatcherThread;
        std::atomic<bool>                                   m_Running;
        std::vector<net::Socket*>                           m_Clients;
        std::string                                         m_CameraConfigPath;
        std::vector<Camera>                                 m_Cameras;
        std::unordered_map<std::string_view, ArgDelegate>   m_ArgMap;
        std::unordered_map<std::string_view, u8>            m_ArgOrderMap;
        std::unordered_map<net::PacketType, PacketDelegate> m_PacketMap;
        u8                      m_ArgOrder = 0; // Surely one would not need more than 255 options.
        std::vector<Endpoint>   m_ConnectedEndpoints;
        std::queue<net::Packet> m_PacketQueue;
        std::mutex              m_PacketQueueMutex;

    public:
        Application(const std::vector<std::string_view>& args);
        ~Application() noexcept;

    public:
        void Run();
        void DispatchArguments(std::vector<std::string_view> args) noexcept;
        void BeginPacketDispatch();

    private:
        inline void AddArgument(const std::string_view arg, ArgDelegate delegate) noexcept
        {
            m_ArgOrderMap[arg] = m_ArgOrder++;
            m_ArgMap[arg]      = delegate;
        }
        inline void AddArgument(const std::initializer_list<const std::string_view> args, ArgDelegate delegate) noexcept
        {
            const auto order = m_ArgOrder++;
            for (const auto& e : args)
            {
                m_ArgOrderMap[e] = m_ArgOrder;
                m_ArgMap[e]      = delegate;
            }
        }
        inline void AddNetPacket(const net::PacketType type, PacketDelegate delegate) noexcept
        {
            m_PacketMap[type] = delegate;
        }

    private:
        [[nodiscard]] bool Arg_DaemonHandler(const std::string_view arg);
        [[nodiscard]] bool Arg_RCHandler(const std::string_view arg);
        [[nodiscard]] bool Arg_CamconfHandler(const std::string_view arg);
        [[nodiscard]] bool Arg_SendStrHandler(const std::string_view arg);

    private:
        [[nodiscard]] bool Net_StringHandler(net::Packet&& packet) noexcept;
    };
} // namespace mgrd
