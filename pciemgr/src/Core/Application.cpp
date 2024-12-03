#include "Application.h"

#include <Utils/Utils.h>

#include <chrono>
#include <ranges>

#include <fcntl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace pciemgr {
    Application::Application(const std::vector<std::string_view>& args)
        : m_DaemonMode(false)
        , m_RootComplex(false)
        , m_LogFilePath("/var/log/pciepciemgr.log")
        , m_ArgOrder(0)
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
        AddArgument({ { "--daemon", "-d" }, "Execute as a daemon.", ArgType::Option, &Application::Arg_DaemonHandler});
        AddArgument({ { "--rc", "-r" }, "Execute as the Root Complex.", ArgType::Option, &Application::Arg_RCHandler});
        AddArgument({ { "--camconf", "-cf" }, "Load the specified camera configuration file.", ArgType::Option, &Application::Arg_CamconfHandler});
        AddArgument({ { "--sendstr", "-s" }, "Send a string to the RC.", ArgType::Option, &Application::Arg_SendStrHandler});
        AddArgument({ { "root", "rc" }, "Communicate with the RC.", ArgType::SubCommand, &Application::Arg_RCCommandHandler});
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

    void Application::DispatchArguments(const std::vector<std::string_view>& args)
    {
        if (args.size() > 1)
        {
            // Sort the arguments according to their order (skip the first argument which is the full binary path).
            std::sort(m_ArgMap.begin(), m_ArgMap.end(), [](const CLIArg& a, const CLIArg& b)
            {
                return a.order < b.order;
            });

            for (const CLIArg& e : m_ArgMap)
            {
                for (const std::string_view uarg : args)
                {
                    const auto arg = utils::StrSplit(uarg, '=')[0];
                    if (const auto arg_it = std::find(e.args.begin(), e.args.end(), arg); arg_it != e.args.end())
                    {
                        // TODO: Optimize with std::ranges::subrange
                        auto sub_args = std::vector<std::string_view>{ std::find(args.begin(), args.end(), *arg_it), args.end() };
                        if (auto result = (this->*(e.delegate))(std::move(sub_args)); !result)
                        {
                            const auto err = result.UnwrapErr();
                            Panic("An Error occured!\n\t{}", err);
                        }
                        else if (e.type == ArgType::SubCommand)
                            // If the previous argument was a sub-command then halt because the sub-command
                            // already parsed the arguments that came after it.
                            break;
                    }
                }
            }
        }
        else
        {
            fmt::println("Usage:\n\t{} <options> [command] [<args>]", GetBinaryName());
            fmt::println("\nArguments:");
            for (const CLIArg& e : m_ArgMap)
            {
                std::string arg_name;
                for (const auto& name : e.args)
                {
                    arg_name += name;
                    arg_name += " | ";
                }
                arg_name.resize(arg_name.size() - 3);
                fmt::println("\t{}\t\t{}", arg_name, e.desc);
            }
            std::exit(EXIT_SUCCESS);
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
                    if (const auto result = (this->*(it->second))(owner, std::move(packet)); !result)
                    {
                        const auto err = result.UnwrapErr();
                        m_Logger->Log(lgx::Level::Error, "An Error Occured!\n\t{}", err);

                        // Send the error to the client.
                        net::BeginSend(owner, err);
                    }
                    else
                        // Tell the client that everything went well.
                        net::BeginSend(owner, net::Packet::Ok());
                }
            }
        }
    }

    Result<Err> Application::ConnectToRC() noexcept
    {
        m_Ep = IPEndPoint_New(net::IPAddress_Parse(Application::RootServerIP), net::AddressFamily_InterNetwork,
                              Application::RootServerPort);

        if (net::Socket_Connect(m_Socket, m_Ep) == CS_SOCKET_ERROR)
            return Err{ ErrType::ConnectionTimeout, "Failed to connect to ({}:{}).", m_Ep.address.str, m_Ep.port };

        m_Logger->Log(lgx::Level::Info, "Connected to Root Complex.");

        return Result<Err>::Ok();
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

    [[nodiscard]] Result<Err> Application::Arg_DaemonHandler(
        [[maybe_unused]] std::vector<std::string_view> args) noexcept
    {
        m_DaemonMode = true;

        // Create file for logging.
        m_LogFile = std::ofstream{ m_LogFilePath, std::ios_base::out | std::ios_base::trunc };
        if (m_LogFile.is_open())
        {
            m_Logger->SetOutputStreams({ &m_LogFile, &std::cout });
            m_Logger->SetDefaultPrefix(m_Logger->GetDefaultPrefix() + 'd');
            return Result<Err>::Ok();
        }
        else
            return Err{ ErrType::IOError, "Failed to open {} for writing.", m_LogFilePath };

        return Result<Err>::Ok();
    }

    [[nodiscard]] Result<Err> Application::Arg_RCHandler(
        [[maybe_unused]] std::vector<std::string_view> args) noexcept
    {
        m_RootComplex = true;

        // Check for root privileges.
        if (getuid() != 0)
            return Err{ "Root privileges are required in order to operate as the Root Complex." };

        const auto prefix = m_Logger->GetDefaultPrefix();
        m_Logger->SetDefaultPrefix((prefix.back() == 'd') ? "RPd" : "RP");

        m_Ep = IPEndPoint_New(net::IPAddress_New(net::IPAddressType_Any), net::AddressFamily_InterNetwork,
                              Application::RootServerPort);

        m_Logger->Log(lgx::Level::Info, "Binding to (localhost:{})...", m_Ep.port);
        if (net::Socket_Bind(m_Socket, m_Ep) == CS_SOCKET_ERROR)
            return Err{ ErrType::SocketError, "Failed to bind to endpoint ({}:{}).", m_Ep.address.str, m_Ep.port };

        return Result<Err>::Ok();
    }

    [[nodiscard]] Result<Err> Application::Arg_CamconfHandler(
        std::vector<std::string_view> args) noexcept
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
            return Err{ ErrType::JsonParseError, "Failed to load camera configuration file: {}", m_CameraConfigPath };

        return Result<Err>::Ok();
    }

    [[nodiscard]] Result<Err> Application::Arg_SendStrHandler(
        std::vector<std::string_view> args) noexcept
    {
        if (auto result = ConnectToRC(); !result)
            return std::move(result);

        auto        msg = utils::StrSplit(args[0], '=')[1];
        net::Packet packet;
        packet.header.type = net::PacketType::String;
        packet << msg;
        net::BeginSend(m_Socket, std::move(packet));

        // The RC is always going to respond with a packet indicating if the operating went well or not.
        // This is done by checking the returned packet's type field, if it is of type PacketType::Error,
        // then an Error occured, we can then deserialize the packet to receive the Err object.
        // Otherwise PacketType::Ok is returned.
        auto recv_packet = net::BeginReceive(m_Socket);
        if (recv_packet)
        {
            if (recv_packet->header.type == net::PacketType::Ok)
                m_Logger->Info("Operation succeeded.");
            else
            {
                Err err;
                (*recv_packet) >> err;
                return err;
            }
        }
        else
            return Err{ ErrType::BadPacket };

        return Result<Err>::Ok();
    }

    [[nodiscard]] Result<Err> Application::Arg_RCCommandHandler(
        std::vector<std::string_view> args) noexcept
    {
        if (auto result = ConnectToRC(); !result)
            return std::move(result);

        // pciemgr rc reboot
        if (args.size() > 1)
        {
            const auto cmd = args[1];
            if (utils::StrLower(cmd) == "reboot")
            {
                // TODO: Write a command handler?
                net::BeginSend(m_Socket, net::Packet{ { net::PacketType::Reboot } });
                auto reply = net::BeginReceive(m_Socket);
                if (reply)
                {
                    switch (reply->header.type)
                    {
                        using enum net::PacketType;

                        default:
                        case Ok: {
                            m_Logger->Info("RC rebooting...");
                            break;
                        }
                        case Error: {
                            // Receive error from server.
                            Err err;
                            (*reply) >> err;
                            return err;
                        }
                    }
                }
                else
                {
                    m_Logger->Error("RC failed to acknowledge the command.");
                    return Err{ ErrType::Timeout };
                }
            }
            else
            {
                return Err{ ErrType::UnknownSubCommand };
            }
        }
        else
        {
            // TODO: Optimize sub-command handling.
            m_Logger->Info(
                "Usage: {} rc|root <command>\nList of available commands:\n\treboot\tReboots the Root Complex.",
                GetBinaryName());
            return Result<Err>::Ok();
        }
        return Result<Err>::Ok();
    }

    [[nodiscard]] Result<Err> Application::Net_StringHandler([[maybe_unused]] net::Socket* client,
                                                             net::Packet&&                 packet) noexcept
    {
        std::string msg;
        packet >> msg;
        m_Logger->Log(lgx::Level::Info, "Ep sent a string: {}", msg);
        return Result<Err>::Ok();
    }

    [[nodiscard]] Result<Err> Application::Net_RebootHandler(net::Socket*                   client,
                                                             [[maybe_unused]] net::Packet&& packet) noexcept
    {
        m_Logger->Log(lgx::Level::Info, "Rebooting...");

        // Send a fake success packet because this method is never going to return unless the reboot fails (which is
        // unlikely).
        // TODO: Consider handling SIGTERM and sending a Reboot packet to the client.
        net::BeginSend(client, net::Packet::Ok());

        // Synchronise filesystems.
        sync();

        // NOTE: I am commenting this so that I don't accidentally reboot the build server again.
        // reboot(RB_AUTOBOOT);

        // We should never reach here in theory.
        return Result<Err>::Ok();
    }
} // namespace pciemgr
