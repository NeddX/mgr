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

#include <Camera/Camera.h>
#include <Endpoint/Endpoint.h>
#include <Net/NetPacket.h>

namespace mgrd {
    class Application
    {
    public:
        static constexpr auto RootServerPort       = 7779;
        static constexpr auto RootServerIP         = "127.0.0.1";
        static constexpr auto RootMaximumEndpoints = 10;

    public:
        using ArgDelegate      = bool (Application::*)(std::vector<std::string_view>);
        using EndpointDelegate = void (Application::*)(const Endpoint);
        using PacketDelegate   = bool (Application::*)(net::Socket*, net::Packet&&);

    private:
        std::string_view                                    m_BinName;
        bool                                                m_DaemonMode;
        bool                                                m_RootComplex;
        std::string                                         m_LogFilePath;
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
        u8                                                  m_ArgOrder;
        std::vector<Endpoint>                               m_ConnectedEndpoints;
        std::queue<std::pair<net::Socket*, net::Packet>>    m_PacketQueue;
        std::mutex                                          m_PacketQueueMutex;

    public:
        Application(const std::vector<std::string_view>& args);
        ~Application() noexcept;

    public:
        void Run();
        void DispatchArguments(const std::vector<std::string_view>& args) noexcept;
        void BeginPacketDispatch();
        void ConnectToRC();

    private:
        [[nodiscard]] constexpr std::string_view GetBinaryName() const noexcept { return m_BinName; }
        template <typename... TArgs>
        inline void Panic(const std::string_view fmt, TArgs&&... args) const noexcept
        {
            if (m_Logger)
                m_Logger->Log(lgx::Level::Fatal, fmt, std::forward<TArgs>(args)...);
            std::exit(-1);
        }
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
                m_ArgOrderMap[e] = order;
                m_ArgMap[e]      = delegate;
            }
        }
        inline void AddNetPacket(const net::PacketType type, PacketDelegate delegate) noexcept
        {
            m_PacketMap[type] = delegate;
        }

    private:
        [[nodiscard]] bool Arg_DaemonHandler(std::vector<std::string_view> args);
        [[nodiscard]] bool Arg_RCHandler(std::vector<std::string_view> args);
        [[nodiscard]] bool Arg_CamconfHandler(std::vector<std::string_view> args);
        [[nodiscard]] bool Arg_SendStrHandler(std::vector<std::string_view> args);
        [[nodiscard]] bool Arg_RCCommandHandler(std::vector<std::string_view> args);

    private:
        // TODO: Make [[maybe_unused]] attribute to both `client` and `packet`.
        [[nodiscard]] bool Net_StringHandler(net::Socket* client, net::Packet&& packet) noexcept;
        [[nodiscard]] bool Net_RebootHandler(net::Socket* client, net::Packet&& packet) noexcept;
    };
} // namespace mgrd
