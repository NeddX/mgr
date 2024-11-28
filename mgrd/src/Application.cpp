#include "Application.h"

#include "Utils/Utils.h"

#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

namespace mgrd {
    Application::Application(const bool daemonMode, const bool rootComplex, std::string cameraConfigPath)
        : m_DaemonMode(daemonMode)
        , m_Logger(nullptr)
        , m_RootComplex(rootComplex)
        , m_CLI()
        , m_Socket(nullptr)
        , m_ShellThread()
        , m_Running()
        , m_Clients()
        , m_CameraConfigPath(std::move(cameraConfigPath))
    {
        CSSocket_Init();

        lgx::Logger::Properties props;

        props.serializeToNonStdoutStreams = false;
        props.defaultStyle                = lgx::Logger::DefaultStyle{ .format            = "[{datetime}] [{level}]: {msg}\n",
                                                                       .defaultInfoStyle  = fmt::fg(fmt::color::gray),
                                                                       .defaultWarnStyle  = fmt::fg(fmt::color::yellow),
                                                                       .defaultErrorStyle = fmt::fg(fmt::color::red),
                                                                       .defaultFatalStyle = fmt::fg(fmt::color::dark_red) };
        if (m_DaemonMode)
        {
            m_LogFile           = std::ofstream{ "/tmp/mgrd.log", std::ios_base::out | std::ios_base::trunc };
            props.outputStreams = { &m_LogFile };
        }
        else
        {
            props.outputStreams = { &std::cout };
        }

        m_Logger = std::make_unique<lgx::Logger>(props);
        m_Logger->Log(lgx::Level::Info, "Application init");
        m_Logger->Log(lgx::Level::Info, "Daemon mode: {}", m_DaemonMode);
        m_Logger->Log(lgx::Level::Info, "Root Complex: {}", m_RootComplex);

        dbus_error_init(&m_dbError);

        m_dbConnection = dbus_bus_get(DBUS_BUS_SESSION, &m_dbError);
        if (dbus_error_is_set(&m_dbError))
        {
            m_Logger->Log(lgx::Level::Fatal, "Failed connecting to dbus session.");
            throw std::runtime_error("dbus error");
        }

        if (!m_dbConnection)
        {
            m_Logger->Log(lgx::Level::Fatal, "Failed connecting to dbus session.");
            throw std::runtime_error("dbus error");
        }

        m_Logger->Log(lgx::Level::Info, "Connected to DBus.");

        m_CLI.AddCommand("start", utils::BindDelegate(this, &Application::Cmd_StartHandler));
        m_CLI.AddCommand("reboot", utils::BindDelegate(this, &Application::Cmd_RebootHandler));
        m_CLI.AddCommand("ping", utils::BindDelegate(this, &Application::Cmd_PingHandler));
        m_CLI.AddCommand("exit", utils::BindDelegate(this, &Application::Cmd_ExitHandler));
        m_CLI.AddCommand("sendstr", utils::BindDelegate(this, &Application::Cmd_SendStrHandler));

        m_Socket = Socket_New(AddressFamily_InterNetwork, SocketType_Stream, ProtocolType_Tcp);

        if (m_RootComplex)
            m_Ep = IPEndPoint_New(IPAddress_New(IPAddressType_Any), AddressFamily_InterNetwork,
                                  Application::RootServerPort);
        else
            m_Ep = IPEndPoint_New(IPAddress_Parse(Application::RootServerIP), AddressFamily_InterNetwork,
                                  Application::RootServerPort);

        if (m_RootComplex)
        {
            m_Logger->Log(lgx::Level::Info, "Binding to (localhost:{})...", m_Ep.port);
            if (Socket_Bind(m_Socket, m_Ep) == CS_SOCKET_ERROR)
            {
                m_Logger->Log(lgx::Level::Fatal, "Failed to bind to endpoint ({}:{}).", m_Ep.address.str, m_Ep.port);
                throw std::runtime_error("Failed to bind to socket.");
            }
        }

        m_Running.store(false);

        if (!m_CameraConfigPath.empty())
        {
            m_Logger->Log(lgx::Level::Info, "Loading '{}'...", m_CameraConfigPath);
            std::ifstream fs{ m_CameraConfigPath };
            if (fs.is_open())
            {
                std::string content =
                    std::string((std::istreambuf_iterator<char>(fs)), (std::istreambuf_iterator<char>()));
                nlohmann::ordered_json j       = nlohmann::json::parse(content);
                m_Cameras = j["cameras"].get<std::vector<Camera>>();
                m_Logger->Log(lgx::Level::Info, "Successfully loaded {} camera configuration(s)", m_Cameras.size());
            }
            else
                m_Logger->Log(lgx::Level::Error, "Failed to load camera configuration file: {}", m_CameraConfigPath);
        }
        else
            m_Logger->Log(lgx::Level::Info, "Camera configuration file not specified.");
    }

    Application::~Application() noexcept
    {
        m_Running.store(false);
        m_Logger->Log(lgx::Level::Info, "Application deinit");
        dbus_connection_unref(m_dbConnection);
        m_dbConnection = nullptr;
        Socket_Dispose(m_Socket);
        m_Socket = nullptr;

        if (m_ShellThread.joinable())
            m_ShellThread.join();
    }

    void Application::Run()
    {
        m_Running.store(true);
        if (!m_DaemonMode) // When not in daemon mode, start a basic shell.
        {
            if (m_RootComplex)
            {
                if (Socket_Listen(m_Socket, Application::RootMaximumEndpoints) == CS_SOCKET_ERROR)
                {
                    m_Logger->Log(lgx::Level::Fatal, "Failed to listen on ({}:{}).", m_Ep.address.str, m_Ep.port);
                    throw std::runtime_error("Failed to listen.");
                }

                while (m_Running.load())
                {
                    Socket* client = Socket_Accept(m_Socket);
                    if (client)
                    {
                        m_Logger->Log(lgx::Level::Info, "Endpoint ({}:{}) connected.", client->remote_ep.address.str,
                                      client->remote_ep.port);
                        // TODO: Append client to the vector in a thread-safe manner.

                        // NOTE: Very bad!
                        std::thread(
                            [this](Socket* socket)
                            {
                                while (socket->connected)
                                {
                                    u8 buffer[1024];
                                    if (Socket_Receive(socket, buffer, sizeof(buffer), 0) == CS_SOCKET_ERROR)
                                    {
                                        m_Logger->Log(lgx::Level::Error,
                                                      "Failed to receive from ({}:{}). Disconnecting...",
                                                      socket->remote_ep.address.str, socket->remote_ep.port);
                                        break;
                                    }
                                    buffer[sizeof(buffer) - 1] = 0;
                                    m_Logger->Log(lgx::Level::Info, "Message from Endpoint: {}",
                                                  reinterpret_cast<char*>(buffer));
                                }
                                // TODO: Remove client from vector in a thread-safe manner.
                                Socket_Dispose(socket);
                            },
                            client)
                            .detach();
                    }
                }
            }
            else
            {
                if (Socket_Connect(m_Socket, m_Ep) == CS_SOCKET_ERROR)
                {
                    m_Logger->Log(lgx::Level::Fatal, "Failed to connect to ({}:{}).", m_Ep.address.str, m_Ep.port);
                    throw std::runtime_error("Failed to connect.");
                }

                m_Logger->Log(lgx::Level::Info, "Connected to Root Complex.");

                std::string readln;
                while (m_Running.load())
                {
                    std::cout << "$: ";
                    std::getline(std::cin, readln);
                    switch (m_CLI.Dispatch(readln))
                    {
                        using enum CLI::Status;

                        case CommandError: m_Logger->Log(lgx::Level::Error, "Command ran with error(s)!"); break;
                        case CommandNotFound: m_Logger->Log(lgx::Level::Error, "Command not found."); break;
                        default: break;
                    }
                }
            }

            /*
            m_ShellThread = std::thread(
                [this]()
                {
                    std::string readln;
                    while (m_Running.load())
                    {
                        std::cout << "$: ";
                        std::getline(std::cin, readln);
                        switch (m_CLI.Dispatch(readln))
                        {
                            using enum CLI::Status;

                            case CommandError: m_Logger->Log(lgx::Level::Error, "Command ran with error(s)!"); break;
                            case CommandNotFound: m_Logger->Log(lgx::Level::Error, "Command not found."); break;
                            default: break;
                        }
                    }
                });
                */
        }
        else // Otherwise start a dbus session.
        {
            while (true)
            {
                dbus_connection_read_write(m_dbConnection, 0);
                DBusMessage* msg = dbus_connection_pop_message(m_dbConnection);

                if (msg)
                {
                    if (dbus_message_is_method_call(msg, DBusInterfaceName, "SayHello"))
                    {
                        DBusMessage* reply     = dbus_message_new_method_return(msg);
                        const char*  reply_msg = "yes yees,hello.";
                        dbus_message_append_args(reply, DBUS_TYPE_STRING, &reply_msg, DBUS_TYPE_INVALID);
                        dbus_connection_send(m_dbConnection, reply, nullptr);
                        dbus_message_unref(reply);
                    }
                    dbus_message_unref(msg);
                }
            }
        }
    }

    CLI::Status Application::Cmd_StartHandler(const CLI::Args& args)
    {
        m_Logger->Log(lgx::Level::Info, "Starting {}", args[1]);
        return CLI::Status::CommandOk;
    }

    CLI::Status Application::Cmd_RebootHandler(const CLI::Args& args)
    {
        m_Logger->Log(lgx::Level::Info, "Rebooting {}", args[1]);
        return CLI::Status::CommandOk;
    }

    CLI::Status Application::Cmd_PingHandler(const CLI::Args& args)
    {
        m_Logger->Log(lgx::Level::Info, "Pong!");
        return CLI::Status::CommandOk;
    }

    CLI::Status Application::Cmd_ExitHandler(const CLI::Args& args)
    {
        m_Logger->Log(lgx::Level::Info, "Exiting");
        std::exit(0);
        return CLI::Status::CommandOk;
    }

    CLI::Status Application::Cmd_SendStrHandler(const CLI::Args& args)
    {
        std::string        str;
        std::ostringstream oss;
        for (auto i = 1; i < args.size(); ++i)
            oss << ' ' << args[i];
        str = oss.str();
        if (Socket_Send(m_Socket, reinterpret_cast<u8*>(str.data()), str.size(), 0) < 0)
        {
            m_Logger->Log(lgx::Level::Error, "Failed to send string.");
            return CLI::Status::CommandError;
        }
        return CLI::Status::CommandOk;
    }
} // namespace mgrd
