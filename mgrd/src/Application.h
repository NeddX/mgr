#pragma once

#include <CommonDef.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <Logex.h>
#include <dbus/dbus.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "CLI/CLI.h"
#include "Net/CSSocket.h"
#include "Camera/Camera.h"

namespace mgrd {
    class Application
    {
    public:
        static constexpr auto DBusInterfaceName    = "com.example.mgrd";
        static constexpr auto RootServerPort       = 7777;
        static constexpr auto RootServerIP         = "127.0.0.1";
        static constexpr auto RootMaximumEndpoints = 10;

    private:
        // TODO: json configuration file
        bool                         m_DaemonMode;
        bool                         m_RootComplex;
        std::unique_ptr<lgx::Logger> m_Logger;
        std::ofstream                m_LogFile;
        DBusError                    m_dbError;
        DBusConnection*              m_dbConnection;
        struct sockaddr_in           m_Address;
        CLI                          m_CLI;
        Socket*                      m_Socket;
        IPEndPoint                   m_Ep;
        std::thread                  m_ShellThread;
        std::atomic<bool>            m_Running;
        std::vector<Socket*>         m_Clients;
        std::string                  m_CameraConfigPath;
        std::vector<Camera> m_Cameras;

    public:
        Application(const bool daemonMode, const bool rootComplex, std::string cameraConfigPath);
        ~Application() noexcept;

    public:
        void Run();

    private:
        CLI::Status Cmd_StartHandler(const CLI::Args& args);
        CLI::Status Cmd_RebootHandler(const CLI::Args& args);
        CLI::Status Cmd_PingHandler(const CLI::Args& args);
        CLI::Status Cmd_ExitHandler(const CLI::Args& args);
        CLI::Status Cmd_SendStrHandler(const CLI::Args& args);
    };
} // namespace mgrd
