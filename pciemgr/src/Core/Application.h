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
#include <Core/Error.h>
#include <Core/Result.h>
#include <Endpoint/Endpoint.h>
#include <Net/NetPacket.h>

namespace pciemgr {
    /**
     * @class Documentation class.
     * @brief Application class is responsible for the actual application itself.
     */
    class Application
    {
        public:
            static constexpr auto RootServerPort       = 7779;
            static constexpr auto RootServerIP         = "127.0.0.1";
            static constexpr auto RootMaximumEndpoints = 10;

        public:
            using ArgDelegate      = Result<Err> (Application::*)(std::vector<std::string_view>);
            using EndpointDelegate = void (Application::*)(const Endpoint);
            using PacketDelegate   = Result<Err> (Application::*)(net::Socket*, net::Packet&&);

        public:
            enum class ArgType
            {
            Option,
            SubCommand
        };
            struct CLIArg
            {
                std::initializer_list<std::string_view> args;
                std::string_view                        desc;
                ArgType                                 type;
                ArgDelegate                             delegate;
                u8 order;
            };

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
            std::vector<CLIArg>                                 m_ArgMap;
            std::unordered_map<net::PacketType, PacketDelegate> m_PacketMap;
            u8                                                  m_ArgOrder;
            std::vector<Endpoint>                               m_ConnectedEndpoints;
            std::queue<std::pair<net::Socket*, net::Packet>>    m_PacketQueue;
            std::mutex                                          m_PacketQueueMutex;

        public:
            Application(const std::vector<std::string_view>& args);
            ~Application() noexcept;

        public:
            void        Run();
            void        DispatchArguments(const std::vector<std::string_view>& args);
            void        BeginPacketDispatch();
            Result<Err> ConnectToRC() noexcept;

        private:
            /*
             * @brief Returns the current binary name.
             * @retrun The binary name as std::string_view
             */
            [[nodiscard]] constexpr std::string_view GetBinaryName() const noexcept { return m_BinName; }
            template <typename... TArgs>
            /*
             * @brief Logs the formatted message at Fatal level and halts the application.
             * @param fmt String to be formatted.
             * @param args Format arguments.
             */
            inline void Panic(const std::string_view fmt, TArgs&&... args) const noexcept
            {
                if (m_Logger)
                    m_Logger->Log(lgx::Level::Fatal, fmt, std::forward<TArgs>(args)...);
                std::exit(-1);
            }
            /*
             * @brief Maps arguments to a delegate and appends them to the argument map for handling command-line arguments.
             * @param args Arguments to be mapped.
             * @param delegate Delegate to be associated with args.
             */
            inline void AddArgument(CLIArg arg)
            {
                arg.order = m_ArgOrder++;
                m_ArgMap.push_back(std::move(arg));
            }
            /*
             * @brief Assosicates the packet type to a delegate for handling packets of that type.
             * @param type Packet type to be assosiacted.
             * @param delegate Delegate to assosicate with packet type.
             */
            inline void AddNetPacket(const net::PacketType type, PacketDelegate delegate) noexcept
            {
                m_PacketMap[type] = delegate;
            }

        private:
            [[nodiscard]] Result<Err> Arg_DaemonHandler(std::vector<std::string_view> args) noexcept;
            [[nodiscard]] Result<Err> Arg_RCHandler(std::vector<std::string_view> args) noexcept;
            [[nodiscard]] Result<Err> Arg_CamconfHandler(std::vector<std::string_view> args) noexcept;
            [[nodiscard]] Result<Err> Arg_SendStrHandler(std::vector<std::string_view> args) noexcept;
            [[nodiscard]] Result<Err> Arg_RCCommandHandler(std::vector<std::string_view> args) noexcept;

        private:
            // TODO: Make [[maybe_unused]] attribute to both `client` and `packet`.
            [[nodiscard]] Result<Err> Net_StringHandler(net::Socket* client, net::Packet&& packet) noexcept;
            [[nodiscard]] Result<Err> Net_RebootHandler(net::Socket* client, net::Packet&& packet) noexcept;
    };
} // namespace pciemgr
