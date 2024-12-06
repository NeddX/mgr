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

#include <CLI/CLI.h>
#include <Camera/CamCrewStation.h>
#include <Core/Error.h>
#include <Core/Result.h>
#include <Endpoint/Endpoint.h>
#include <Net/NetHandler.h>
#include <Net/NetPacket.h>

/**
 * @namespace pmgrd
 * @brief The global daemon namespace.
 *
 * This namespaces contains every class, struct & function for the daemon.
 */
namespace pmgrd {
    /**
     * @class Application
     * @brief The Core Application class responsible for handling the daemon itself.
     *
     * This class manages the execution of the daemon.
     */
    class Application
    {
    public:
        /**
         * @brief The pre-defined RC daemon's server port.
         * */
        static constexpr auto RootServerPort = 7777;
        /**
         * @brief The pre-defined RC daemon's server IP.
         * */
        static constexpr auto RootServerIP = "127.0.0.1";
        /**
         * @brief Maximum Endpoint that can connect to the RC's daemon.
         * */
        static constexpr auto RootMaximumEndpoints = 10;

    private:
        const std::vector<std::string_view>& m_Args;
        std::unique_ptr<CLI>                 m_CLI;
        std::string_view                     m_BinName;
        bool                                 m_DaemonMode;
        bool                                 m_RootComplex;
        std::string                          m_LogFilePath;
        lgx::Logger::Properties              m_LoggerProperties;
        std::unique_ptr<lgx::Logger>         m_Logger;
        std::ofstream                        m_LogFile;
        net::Socket*                         m_Socket;
        net::IPEndPoint                      m_Ep;
        std::atomic<bool>                    m_Started;
        std::string                          m_CameraConfigPath;
        std::unique_ptr<net::NetHandler>     m_NetHandler;
        u8                                   m_NodeID;
        bool                                 m_Concentrator;
        bool                                 m_CrewStation;
        std::array<std::list<u8>, 63>        m_Groups;
        std::list<Camera>                    m_Cameras;
        std::list<CrewStation>               m_CrewStations;

    private:
        static std::unique_ptr<Application> s_Instance;

    private:
        Application(const std::vector<std::string_view>& args);

    public:
        ~Application() noexcept;

    public:
        /**
         *  @brief Initialises and parses the CLI arguments.
         *
         *  @returns @ref Result of @ref Err where @ref Err indicates an error has occured.
         *  */
        Result<Err> Init() noexcept;

        /**
         *  @brief Starts listening for Endpoints.
         *
         *  @returns @ref Result of @ref Err where @ref Err indicates an error has occured.
         *  */
        Result<Err> Run() noexcept;

        /**
         *  @brief Tries to connect to the RC server.
         *
         *  @details After successfully connecting, this method sends a @ref net::PacketType::InitCon packet
         *  which contains its ID from /etc/vlink.conf to the RC to register itself as an @ref Endpoint on the RC side.
         *  Afterwards it sends a @ref net::PacketType::GetConfig where the RC tries to match it with a @ref Crew
         *  Station and then responds with a @ref json object containing its @ref Camera.
         *
         *  @returns @ref Result of @ref Err where @ref Err indicates an error has occured.
         *  */
        Result<Err> ConnectToRC() noexcept;

    public:
        /**
         * @brief Returnss the current binary name.
         *
         * @retrun The binary name as std::string_view
         *  */
        [[nodiscard]] static inline std::string_view GetBinaryName() { return Application::Get().m_BinName; }

        /**
         * @brief Logs the formatted message at Fatal level and halts the application.
         *
         * @param fmt String to be formatted.
         * @param args Format arguments.
         *  */
        template <typename... TArgs>
        static inline void Panic(const std::string_view fmt, TArgs&&... args)
        {
            auto& inst = Application::Get();
            if (inst.m_Logger)
                inst.m_Logger->Log(lgx::Level::Fatal, fmt, std::forward<TArgs>(args)...);
            std::exit(EXIT_FAILURE);
        }

    private:
        [[nodiscard]] Result<Err> Arg_DaemonHandler(std::vector<std::string_view> args) noexcept;
        [[nodiscard]] Result<Err> Arg_RCHandler(std::vector<std::string_view> args) noexcept;
        [[nodiscard]] Result<Err> Arg_JoinHandler(std::vector<std::string_view> args) noexcept;
        [[nodiscard]] Result<Err> Arg_LeaveHandler(std::vector<std::string_view> args) noexcept;
        [[nodiscard]] Result<Err> Arg_CamconfHandler(std::vector<std::string_view> args) noexcept;
        [[nodiscard]] Result<Err> Arg_SendStrHandler(std::vector<std::string_view> args) noexcept;
        [[nodiscard]] Result<Err> Arg_RCCommandHandler(std::vector<std::string_view> args) noexcept;
        [[nodiscard]] Result<Err> Arg_CrewStationHandler(std::vector<std::string_view> args) noexcept;
        [[nodiscard]] Result<Err> Arg_ConcentratorHandler(std::vector<std::string_view> args) noexcept;

    private:
        [[nodiscard]] Result<Err> Net_StringHandler(Endpoint& ep, net::Packet&& packet) noexcept;
        [[nodiscard]] Result<Err> Net_RebootHandler(Endpoint& ep, net::Packet&& packet) noexcept;
        [[nodiscard]] Result<Err> Net_JoinHandler(Endpoint& ep, net::Packet&& packet) noexcept;
        [[nodiscard]] Result<Err> Net_LeaveHandler(Endpoint& ep, net::Packet&& packet) noexcept;
        [[nodiscard]] Result<Err> Net_GetCrewConfigHandler(Endpoint& ep, net::Packet&& packet) noexcept;
        [[nodiscard]] Result<Err> Net_GetCtrConfigHandler(Endpoint& ep, net::Packet&& packet) noexcept;

    public:
        /**
         * @brief Creates a new singleton instance of @ref Application.
         *
         * @param args Command line arguments.
         * @throws std::runtime_error if the instance already exists.
         * @returns Reference to the newly created @ref Application instance.
         *  */
        static Application& New(const std::vector<std::string_view>& args);

        /**
         * @brief Returnss the current @ref Application singleton instance.
         *
         * @throws std::runtime_error if no instance has been created with @ref Application::New.
         * @returns Reference to the current @ref Application instance.
         *  */
        [[nodiscard]] static Application& Get();
    };
} // namespace pmgrd
