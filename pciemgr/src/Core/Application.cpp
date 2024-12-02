#include "Application.h"

#include <Utils/Utils.h>

#include <chrono>
#include <ranges>

#include <fcntl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace mgrd {
    Application::Application(const std::vector<std::string_view>& args)
        : m_DaemonMode(false)
        , m_RootComplex(false) , m_LogFilePath("/var/log/pciemgrd.log") , m_ArgOrder(0)
    {
        net::CSSocket_Init();

        // Communication via TCP/IP.
        m_Socket = net::Socket_New(net::AddressFamily_InterNetwork, net::SocketType_Stream, net::ProtocolType_Tcp);

        m_LoggerProperties.serializeToNonStdoutStreams = false;
        m_LoggerProperties.defaultPrefix               = "EP";
        m_LoggerProperties.flushOnLog                  = true;
        m_LoggerProperties.defaultStyle =
            lgx::Logger::DefaultStyle{ .format            = "[{datetime}] [{level}] ({prefix}): {msg}\n",
                                       .defaultInfoStyle  = fmt::fg(fmt::color::gray),
                                       .defaultWarnStyle  = fmt::fg(fmt::color::yellow),
                                       .defaultErrorStyle = fmt::fg(fmt::color::red),
                                       .defaultFatalStyle = fmt::fg(fmt::color::dark_red) };

        // When not in daemon mode, default output will be stdout, m_LogFile otherwise.
        m_LoggerProperties.outputStreams = { &std::cout };

        // NOTE: DO NOT LOG BEFORE CLI ARGUMENTS HAVE BEEN PARSED!
        m_Logger = std::make_unique<lgx::Logger>(m_LoggerProperties);

        m_BinName = args[0];

        // NOTE: Arguments are parsed in the order they were added.
        AddArgument({ "--daemon", "-d" }, &Application::Arg_DaemonHandler);
        AddArgument({ "--rc", "-r" }, &Application::Arg_RCHandler);
        AddArgument({ "--camconf", "-cf" }, &Application::Arg_CamconfHandler);
        AddArgument({ "--sendstr", "-s" }, &Application::Arg_SendStrHandler);
        AddArgument({ "root", "rc" }, &Application::Arg_RCCommandHandler);
        DispatchArguments(args);

        AddNetPacket(net::PacketType::String, &Application::Net_StringHandler);
        AddNetPacket(net::PacketType::Reboot, &Application::Net_RebootHandler);

        // NOTE: You can log now.
        m_Logger->Log(lgx::Level::Info, "Application init");

        m_Running.store(true);

        m_Logger->Log(lgx::Level::Info, "Daemon mode: {}", m_DaemonMode);
        m_Logger->Log(lgx::Level::Info, "Root Complex: {}", m_RootComplex);

        if (m_CameraConfigPath.empty())
            m_Logger->Log(lgx::Level::Warn, "Camera configuration file not specified.");
    }

    Application::~Application() noexcept
    {
        m_Running.store(false);

        m_Logger->Log(lgx::Level::Info, "Application deinit");

        if (m_Socket)
        {
            net::Socket_Dispose(m_Socket);
            m_Socket = nullptr;
        }

        if (m_ShellThread.joinable())
            m_ShellThread.join();

        if (m_PacketDispatcherThread.joinable())
            m_PacketDispatcherThread.join();

        net::CSSocket_Dispose();
    }

    void Application::DispatchArguments(const std::vector<std::string_view>& args) noexcept
    {
        // Sort the arguments according to their order.
        std::vector<std::string_view> sorted_args{ args };
        std::sort(sorted_args.begin(), sorted_args.end(),
                  [this](const auto& s1, const auto& s2)
                  {
                      const auto a = (m_ArgOrderMap.contains(s1)) ? m_ArgOrderMap[s1] : 255;
                      const auto b = (m_ArgOrderMap.contains(s2)) ? m_ArgOrderMap[s2] : 255;
                      return a < b;
                  });

        for (const auto& e : sorted_args)
        {
            const auto arg = utils::StrSplit(e, '=')[0];
            if (const auto it = m_ArgMap.find(arg); it != m_ArgMap.end())
            {
                // TODO: Optimize with std::ranges::subrange
                auto sub_args = std::vector<std::string_view>{ std::find(args.begin(), args.end(), e), args.end() };
                if (!(this->*(it->second))(std::move(sub_args)))
                    m_Logger->Log(lgx::Level::Error, "{} parsed with error(s).", arg);
            }
        }
    }

    void Application::BeginPacketDispatch()
    {
        while (m_Running.load())
        {
            std::scoped_lock lock{ m_PacketQueueMutex };

            while (!m_PacketQueue.empty())
            {
                auto [owner, packet] = std::move(m_PacketQueue.front());
                const auto type      = packet.header.type;
                m_PacketQueue.pop();

                m_Logger->Log(lgx::Level::Info, "Processing incoming packet: {}", net::TypeToStr(type));
                if (const auto it = m_PacketMap.find(packet.header.type); it != m_PacketMap.end())
                {
                    if (!(this->*(it->second))(owner, std::move(packet)))
                    {
                        m_Logger->Log(lgx::Level::Error, "{} packet processed with error(s).", net::TypeToStr(type));

                        /*
                        ** Error Handling system purposal
                        ** A Net delegate will return Result<T> type where T is a user defined Error Struct.
                        ** If A Net delegate returns Result::Ok, then no errors occured, if T was returned then an error
                        *occured.
                        ** T can be something as basic as an int indicating a error return code or an enum or even a
                        *struct.
                        ** Error structs can be serialized and sent over the network like so:
                        */
                        /*
                        Error err = GET;
                        net::Packet err_packet{net::PacketType::Error};
                        err_packet << err;
                        net::BeginSend(m_Socket, err_packet);
                        */
                    }
                }
            }
        }
    }

    void Application::ConnectToRC()
    {
        m_Ep = IPEndPoint_New(net::IPAddress_Parse(Application::RootServerIP), net::AddressFamily_InterNetwork,
                              Application::RootServerPort);

        if (net::Socket_Connect(m_Socket, m_Ep) == CS_SOCKET_ERROR)
        {
            Panic("Failed to connect to ({}:{}).", m_Ep.address.str, m_Ep.port);
        }

        m_Logger->Log(lgx::Level::Info, "Connected to Root Complex.");
    }

    void Application::Run()
    {
        if (m_RootComplex)
        {
            if (net::Socket_Listen(m_Socket, Application::RootMaximumEndpoints) == CS_SOCKET_ERROR)
            {
                Panic("Failed to listen.");
            }

            // Begin dispatching packets on a separate thread.
            m_PacketDispatcherThread = std::thread{ &Application::BeginPacketDispatch, this };

            // Handle endpoints.
            while (m_Running.load())
            {
                m_Logger->Log(lgx::Level::Info, "Waiting for an endpoint...");

                net::Socket* potential_ep = net::Socket_Accept(m_Socket);
                if (potential_ep)
                {
                    m_Logger->Log(lgx::Level::Info, "A connection was made by ({}:{})!",
                                  potential_ep->remote_ep.address.str, potential_ep->remote_ep.port);
                    std::thread{ [this](net::Socket* eps)
                                 {
                                     while (eps->connected)
                                     {
                                         auto packet = net::BeginReceive(eps);
                                         if (packet)
                                         {
                                             std::scoped_lock lock{ m_PacketQueueMutex };

                                             m_PacketQueue.emplace(eps, std::move(*packet));
                                         }
                                     }
                                     net::Socket_Dispose(eps);
                                 },
                                 potential_ep }
                        .detach();
                }
            }
        }
    }

    bool Application::Arg_DaemonHandler([[maybe_unused]] std::vector<std::string_view> args)
    {
        m_DaemonMode = true;

        // Create file for logging.
        m_LogFile = std::ofstream{ m_LogFilePath, std::ios_base::out | std::ios_base::trunc };
        if (m_LogFile.is_open())
        {
            m_Logger->SetOutputStreams({ &m_LogFile, &std::cout });
            m_Logger->SetDefaultPrefix(m_Logger->GetDefaultPrefix() + 'd');
            return true;
        }
        else
            Panic("Failed to open {} for writing.", m_LogFilePath);

        return true;
    }

    bool Application::Arg_RCHandler([[maybe_unused]] std::vector<std::string_view> args)
    {
        m_RootComplex = true;

        // Check for root privileges.
        if (getuid() != 0)
            Panic("Root privileges are required in order to operate as the Root Complex.");

        const auto prefix = m_Logger->GetDefaultPrefix();
        m_Logger->SetDefaultPrefix((prefix.back() == 'd') ? "RPd" : "RP");

        m_Ep = IPEndPoint_New(net::IPAddress_New(net::IPAddressType_Any), net::AddressFamily_InterNetwork,
                              Application::RootServerPort);

        m_Logger->Log(lgx::Level::Info, "Binding to (localhost:{})...", m_Ep.port);
        if (net::Socket_Bind(m_Socket, m_Ep) == CS_SOCKET_ERROR)
        {
            Panic("Failed to bind to endpoint ({}:{}).", m_Ep.address.str, m_Ep.port);
        }
        return true;
    }

    bool Application::Arg_CamconfHandler(std::vector<std::string_view> args)
    {
        m_CameraConfigPath = utils::StrSplit(args[0], '=')[1];
        m_Logger->Log(lgx::Level::Info, "Loading '{}'...", m_CameraConfigPath);
        std::ifstream fs{ m_CameraConfigPath };
        if (fs.is_open())
        {
            std::string content = std::string((std::istreambuf_iterator<char>(fs)), (std::istreambuf_iterator<char>()));
            nlohmann::ordered_json j = nlohmann::json::parse(content);
            m_Cameras                = j["cameras"].get<std::vector<Camera>>();
            m_Logger->Log(lgx::Level::Info, "Successfully loaded {} camera configuration(s)", m_Cameras.size());
        }
        else
            m_Logger->Log(lgx::Level::Error, "Failed to load camera configuration file: {}", m_CameraConfigPath);

        return true;
    }

    [[nodiscard]] bool Application::Arg_SendStrHandler(std::vector<std::string_view> args)
    {
        ConnectToRC();

        auto        msg = utils::StrSplit(args[0], '=')[1];
        net::Packet packet;
        packet.header.type = net::PacketType::String;
        packet << msg;
        net::BeginSend(m_Socket, std::move(packet));
        return true;
    }

    [[nodiscard]] bool Application::Arg_RCCommandHandler(std::vector<std::string_view> args)
    {
        ConnectToRC();

        // pciemgr rc reboot
        if (args.size() > 1)
        {
            const auto cmd = args[1];
            if (utils::StrLower(cmd) == "reboot")
            {
                // TODO: Write a command handler?
                net::BeginSend(m_Socket, net::Packet{ { net::PacketType::Reboot } });
                const auto reply = net::BeginReceive(m_Socket);
                if (reply)
                {
                    switch (reply->header.type)
                    {
                        using enum net::PacketType;

                        case Success: m_Logger->Info("RC rebooting..."); return true;
                        case InvalidState:
                            m_Logger->Error("RC failed to reboot. Error code: InvalidState");
                            return false;
                        default: return true;
                    }
                }
                else
                {
                    m_Logger->Error("RC failed to acknowledge the command.");
                    return false;
                }
            }
        }
        else
        {
            // TODO: Optimize sub-command handling.
            m_Logger->Info(
                "Usage: {} rc|root <command>\nList of available commands:\n\treboot\tReboots the Root Complex.",
                GetBinaryName());
            return true;
        }
        return false;
    }

    [[nodiscard]] bool Application::Net_StringHandler([[maybe_unused]] net::Socket* client,
                                                      net::Packet&&                 packet) noexcept
    {
        std::string msg;
        packet >> msg;
        m_Logger->Log(lgx::Level::Info, "Ep sent a string: {}", msg);
        return true;
    }

    [[nodiscard]] bool Application::Net_RebootHandler(net::Socket*                   client,
                                                      [[maybe_unused]] net::Packet&& packet) noexcept
    {
        m_Logger->Log(lgx::Level::Info, "Rebooting...");

        // Synchronise filesystems.
        sync();

        if (reboot(RB_AUTOBOOT) != 0)
        {
            m_Logger->Log(lgx::Level::Error, "Failed to reboot.");
            /*
              -> Result<Error>
              return Result::Ok; // If everything was fine.
              return Error { * custom error struct * };
             */
            return false;
        }
        net::BeginSend(client, net::Packet{ { net::PacketType::Success } });

        return true;
    }
} // namespace mgrd
