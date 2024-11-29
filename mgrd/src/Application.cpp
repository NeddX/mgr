#include "Application.h"

#include "Utils/Utils.h"

#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

namespace mgrd {
    Application::Application(const std::vector<std::string_view>& args)
        : m_DaemonMode(false)
        , m_RootComplex(false)
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

        // NOTE: Arguments are parsed in the order they were added.
        AddArgument({ "--daemon", "-d" }, &Application::Arg_DaemonHandler);
        AddArgument({ "--rc", "-r" }, &Application::Arg_RCHandler);
        AddArgument({ "--camconf", "-cf" }, &Application::Arg_CamconfHandler);
        AddArgument({ "--sendstr", "-s" }, &Application::Arg_SendStrHandler);
        DispatchArguments(args);

        AddNetPacket(net::PacketType::String, &Application::Net_StringHandler);

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

    void Application::DispatchArguments(std::vector<std::string_view> args) noexcept
    {
        // Sort the arguments according to their order.
        std::sort(args.begin(), args.end(),
                  [this](const auto& s1, const auto& s2)
                  {
                      const auto a = (m_ArgOrderMap.contains(s1)) ? m_ArgOrderMap[s1] : 255;
                      const auto b = (m_ArgOrderMap.contains(s2)) ? m_ArgOrderMap[s2] : 255;
                      return a < b;
                  });

        for (const auto& e : args)
        {
            const auto arg = utils::StrSplit(e, '=')[0];
            if (const auto it = m_ArgMap.find(arg); it != m_ArgMap.end())
            {
                if (!(this->*(it->second))(e))
                    m_Logger->Log(lgx::Level::Error, "{} parsed with error(s).", arg);
            }
        }
    }

    void Application::BeginPacketDispatch()
    {
        while (m_Running.load())
        {
            while (!m_PacketQueue.empty())
            {
                std::scoped_lock lock{ m_PacketQueueMutex };

                auto       packet = std::move(m_PacketQueue.front());
                const auto type   = packet.header.type;
                m_PacketQueue.pop();

                m_Logger->Log(lgx::Level::Info, "Processing incoming packet: {}", net::TypeToStr(type));
                if (const auto it = m_PacketMap.find(packet.header.type); it != m_PacketMap.end())
                {
                    if (!(this->*(it->second))(std::move(packet)))
                        m_Logger->Log(lgx::Level::Error, "{} packet processed with error(s).", net::TypeToStr(type));
                }
            }
        }
    }

    void Application::Run()
    {
        if (m_RootComplex)
        {
            if (net::Socket_Listen(m_Socket, Application::RootMaximumEndpoints) == CS_SOCKET_ERROR)
            {
                m_Logger->Log(lgx::Level::Fatal, "Failed to listen on ({}:{}).", m_Ep.address.str, m_Ep.port);
                throw std::runtime_error("Failed to listen.");
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

                                             m_PacketQueue.push(std::move(*packet));
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

    bool Application::Arg_DaemonHandler(const std::string_view arg)
    {
        m_DaemonMode = true;
        m_LogFile    = std::ofstream{ "/tmp/mgrd.log", std::ios_base::out | std::ios_base::trunc };
        if (m_LogFile.is_open())
        {
            m_Logger->SetOutputStreams({ &m_LogFile });
            m_Logger->SetDefaultPrefix(m_Logger->GetDefaultPrefix() + 'd');
            return true;
        }
        throw std::runtime_error("Failed to open /tmp/mgrd.log for writing...");
    }

    bool Application::Arg_RCHandler(const std::string_view arg)
    {
        m_RootComplex = true;

        const auto prefix = m_Logger->GetDefaultPrefix();
        m_Logger->SetDefaultPrefix((prefix.back() == 'd') ? "RPd" : "RP");

        m_Ep = IPEndPoint_New(net::IPAddress_New(net::IPAddressType_Any), net::AddressFamily_InterNetwork,
                              Application::RootServerPort);

        m_Logger->Log(lgx::Level::Info, "Binding to (localhost:{})...", m_Ep.port);
        if (net::Socket_Bind(m_Socket, m_Ep) == CS_SOCKET_ERROR)
        {
            m_Logger->Log(lgx::Level::Fatal, "Failed to bind to endpoint ({}:{}).", m_Ep.address.str, m_Ep.port);
            m_Running.store(false);
            return false;
        }
        return true;
    }

    bool Application::Arg_CamconfHandler(const std::string_view arg)
    {
        m_CameraConfigPath = utils::StrSplit(arg, '=')[1];
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

    [[nodiscard]] bool Application::Arg_SendStrHandler(const std::string_view arg)
    {
        m_Ep = IPEndPoint_New(net::IPAddress_Parse(Application::RootServerIP), net::AddressFamily_InterNetwork,
                              Application::RootServerPort);

        if (net::Socket_Connect(m_Socket, m_Ep) == CS_SOCKET_ERROR)
        {
            m_Logger->Log(lgx::Level::Fatal, "Failed to connect to ({}:{}).", m_Ep.address.str, m_Ep.port);
            return false;
        }

        m_Logger->Log(lgx::Level::Info, "Connected to Root Complex.");

        auto        msg = utils::StrSplit(arg, '=')[1];
        net::Packet packet;
        packet.header.type = net::PacketType::String;
        packet << msg;
        net::BeginSend(m_Socket, std::move(packet));
        return true;
    }

    [[nodiscard]] bool Application::Net_StringHandler(net::Packet&& packet) noexcept
    {
        std::string msg;
        packet >> msg;
        m_Logger->Log(lgx::Level::Info, "Ep sent a string: {}", msg);
        return true;
    }
} // namespace mgrd
