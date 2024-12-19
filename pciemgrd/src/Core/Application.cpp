#include "Application.h"

#include <Utils/Utils.h>

#include <chrono>
#include <ranges>

#include <fcntl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace pmgrd {
    std::unique_ptr<Application> Application::s_Instance = nullptr;

    Application::Application(const std::vector<std::string_view>& args)
        : m_Args(args)
        , m_DaemonMode(false)
        , m_RootComplex(false)
        , m_LogFilePath("/var/log/pciepciemgr.log")
        , m_Concentrator(false)
        , m_CrewStation(false)
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
        m_CLI    = std::make_unique<CLI>(m_Args, *m_Logger);

        // Set binary name.
        m_BinName = utils::StrSplit(args[0], '/').back();

        // NOTE: Arguments are parsed in the order they were added.
        m_CLI->AddArgument({ { "--daemon", "-d" },
                             "Execute as a daemon.",
                             CLI::ArgType::Option,
                             utils::BindDelegate(this, &Application::Arg_DaemonHandler) });
        m_CLI->AddArgument({ { "--rootcomplex", "-r" },
                             "Execute as the Root Complex.",
                             CLI::ArgType::Option,
                             utils::BindDelegate(this, &Application::Arg_RCHandler) });
        m_CLI->AddArgument({ { "--crewstation", "-cs" },
                             "Connect as a Crew Station.",
                             CLI::ArgType::Option,
                             utils::BindDelegate(this, &Application::Arg_CrewStationHandler) });
        m_CLI->AddArgument({ { "--concentrator", "-ct" },
                             "Connect as a Crew Station.",
                             CLI::ArgType::Option,
                             utils::BindDelegate(this, &Application::Arg_ConcentratorHandler) });
        m_CLI->AddArgument({ { "--camconf", "-cf" },
                             "Load the specified camera configuration file.",
                             CLI::ArgType::Option,
                             utils::BindDelegate(this, &Application::Arg_CamconfHandler) });

        // FIXME: Options do not consume arguments that are appropriate to them.
        // e.g., -j 0 (0 does gets parsed an argument and fails).
        // Temporary fix, make -j and -l 'sub-commands'.
        m_CLI->AddArgument({ { "--leave", "-l" },
                             "Leave from a multicast group.",
                             CLI::ArgType::SubCommand,
                             utils::BindDelegate(this, &Application::Arg_LeaveHandler) });
        m_CLI->AddArgument({ { "--join", "-j" },
                             "Join a multicast group.",
                             CLI::ArgType::SubCommand,
                             utils::BindDelegate(this, &Application::Arg_JoinHandler) });
        m_CLI->AddArgument({ { "--sendstr", "-s" },
                             "Send a string to the RC.",
                             CLI::ArgType::SubCommand,
                             utils::BindDelegate(this, &Application::Arg_SendStrHandler) });
        m_CLI->AddArgument({ { "root", "rc" },
                             "Communicate with the RC.",
                             CLI::ArgType::SubCommand,
                             utils::BindDelegate(this, &Application::Arg_RCCommandHandler) });
        m_CLI->AddArgument({ { "gst" },
                             "Invoke GStreamer based on configuration sent by the RC.",
                             CLI::ArgType::SubCommand,
                             utils::BindDelegate(this, &Application::Arg_GSTHandler) });

        m_NetHandler = std::make_unique<net::NetHandler>(*m_Logger, m_Socket);

        m_NetHandler->AddPacket(net::PacketType::String, utils::BindDelegate(this, &Application::Net_StringHandler));
        m_NetHandler->AddPacket(net::PacketType::Reboot, utils::BindDelegate(this, &Application::Net_RebootHandler));
        m_NetHandler->AddPacket(net::PacketType::Join, utils::BindDelegate(this, &Application::Net_JoinHandler));
        m_NetHandler->AddPacket(net::PacketType::Leave, utils::BindDelegate(this, &Application::Net_LeaveHandler));
        m_NetHandler->AddPacket(net::PacketType::GetCtrConfig,
                                utils::BindDelegate(this, &Application::Net_GetCtrConfigHandler));
        m_NetHandler->AddPacket(net::PacketType::GetCrewConfig,
                                utils::BindDelegate(this, &Application::Net_GetCrewConfigHandler));
    }

    Application::~Application() noexcept
    {
        if (m_Socket)
        {
            m_NetHandler->Stop();
            net::Socket_Dispose(m_Socket);
            m_Socket = nullptr;
        }

        if (m_Started)
        {
            m_Started.store(false);

            m_Logger->Log(lgx::Level::Info, "Application deinit");
        }

        net::CSSocket_Dispose();
    }

    Result<Err> Application::Init() noexcept
    {
        // Parse CLI arguments.
        auto arg_result = m_CLI->DispatchArguments();
        if (!arg_result)
        {
            auto err = arg_result.UnwrapErr();
            m_Logger->Fatal("An Error Occured!\n\t{}", err);
            return std::move(err);
        }
        else if (!arg_result.Unwrap())
            return Ok();

        // NOTE: You can log now.
        m_Logger->Log(lgx::Level::Info, "Application init");

        m_Started.store(true);

        m_Logger->Info("Daemon mode: {}", m_DaemonMode);
        m_Logger->Info("Root Complex: {}", m_RootComplex);

        if (m_CameraConfigPath.empty() && m_RootComplex)
            m_Logger->Log(lgx::Level::Warn, "Camera configuration file not specified.");

        m_Started.store(true);

        return Ok();
    }

    Result<Err> Application::Run() noexcept
    {
        if (m_RootComplex)
        {
            if (net::Socket_Listen(m_Socket, Application::RootMaximumEndpoints) == CS_SOCKET_ERROR)
                return Err{ ErrType::NetListenFailure };

            m_NetHandler->BeginPacketDispatch();
            if (auto result = m_NetHandler->BeginAccept(); !result)
                return result;
        }

        return Ok();
    }

    Result<Err> Application::LoadCameraConfig() noexcept
    {
        m_Logger->Log(lgx::Level::Info, "Loading '{}'...", m_CameraConfigPath);
        std::ifstream fs{ m_CameraConfigPath };
        if (fs.is_open())
        {
            std::string content = std::string((std::istreambuf_iterator<char>(fs)), (std::istreambuf_iterator<char>()));
            nlohmann::json j    = nlohmann::json::parse(content);

            if (j.contains("crewStations"))
                m_CrewStations = j["crewStations"].get<std::list<CrewStation>>();
            else
                return Err{ ErrType::InvalidCameraConfiguration };

            if (j.contains("concentrators"))
            {
                for (const auto& e : j["concentrators"])
                {
                    const auto node_id = e["nodeId"];

                    Camera cam_obj;
                    if (e.contains("cameras"))
                    {
                        for (const auto& cam : e["cameras"])
                        {
                            cam_obj        = cam.get<Camera>();
                            cam_obj.nodeId = node_id;
                            m_Cameras.push_back(std::move(cam_obj));
                        }
                    }
                    else
                        return Err{ ErrType::InvalidCameraConfiguration };
                }
            }
            else
                return Err{ ErrType::InvalidCameraConfiguration };

            m_Logger->Log(lgx::Level::Info, "Successfully loaded {} camera configuration(s)", m_Cameras.size());
        }
        else
            return Err{ ErrType::JsonParseError, "Failed to load camera configuration file: {}", m_CameraConfigPath };

        return Ok();
    }

    Result<Err> Application::ConnectToRC() noexcept
    {
        const auto ip_endpoint = IPEndPoint_New(net::IPAddress_Parse(Application::RootServerIP),
                                                net::AddressFamily_InterNetwork, Application::RootServerPort);

        if (net::Socket_Connect(m_Socket, ip_endpoint) == CS_SOCKET_ERROR)
            return Err{ ErrType::NetConnectionTimeout, "Failed to connect to ({}:{}).", ip_endpoint.address.str,
                        m_Ep.port };

        // Grab Node ID from /etc/vlink.conf.
        auto node_file = utils::fs::ReadToString("/etc/vlink.conf");
        if (node_file)
            m_NodeID = static_cast<u8>(std::stoi(utils::StrSplit(node_file.Unwrap(), '=')[1]));
        else
            return node_file.UnwrapErr();

        m_Logger->Info("Node ID: {}", m_NodeID);
        m_Logger->Info("Connected to Root Complex.");
        m_Logger->Info("Sending InitConn packet...");

        // Send Reply to register as an Endpoint.
        net::Packet initconn{ net::PacketType::Ready };
        initconn << m_NodeID;
        TRY_UNWRAP(net::BeginSend(m_Socket, std::move(initconn)));

        // Wait for Ready acknowledgement.
        if (const auto result = net::BeginReceive(m_Socket); !result || result.Unwrap().Type() != net::PacketType::Ok)
            return Err{ ErrType::NetReadyFailure };

        // Request for configuration.
        if (m_CrewStation)
        {
            TRY_UNWRAP(net::BeginSend(m_Socket, net::PacketType::GetCrewConfig));
            auto result = net::BeginReceive(m_Socket);
            if (!result)
                return result.UnwrapErr();

            // Check if the packet itself is an error.
            auto the_horror = result.Unwrap();
            if (!the_horror)
                return Err::FromPacket(std::move(the_horror));

            // Parse the configuration.
            std::string jsonstr;
            result.Unwrap() >> jsonstr;

            m_Logger->Log(__func__, lgx::Level::Info, "Crew config: {}", jsonstr);
        }
        else if (m_Concentrator)
        {
            TRY_UNWRAP(net::BeginSend(m_Socket, net::PacketType::GetCtrConfig));
            auto result = net::BeginReceive(m_Socket);
            if (!result)
                return result.UnwrapErr();

            // Check if the packet itself is an error.
            auto the_horror = result.Unwrap();
            if (!the_horror)
                return Err::FromPacket(std::move(the_horror));

            // Parse the configuration.
            std::string jsonstr;
            result.Unwrap() >> jsonstr;

            // Convert to json and deserialise it from json to vector of cameras.
            nlohmann::json j = nlohmann::json::parse(jsonstr);
            m_Cameras        = j["cameras"].get<std::list<Camera>>();

            // Validate cameras.
            for (const auto& e : m_Cameras)
                TRY_UNWRAP(e.Validate());

            m_Logger->Log(__func__, lgx::Level::Info, "Crew config: {}", jsonstr);
        }

        return Ok();
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
            return Ok();
        }
        else
            return Err{ ErrType::IOError, "Failed to open {} for writing.", m_LogFilePath };

        return Ok();
    }

    [[nodiscard]] Result<Err> Application::Arg_RCHandler([[maybe_unused]] std::vector<std::string_view> args) noexcept
    {
        m_RootComplex = true;

        // Check for root privileges.
        if (getuid() != 0)
            return Err{ "Root privileges are required in order to operate as the Root Complex." };

        const auto prefix = m_Logger->GetDefaultPrefix();
        m_Logger->SetDefaultPrefix((prefix.back() == 'd') ? "RPd" : "RP");

        const auto ip_endpoint = IPEndPoint_New(net::IPAddress_New(net::IPAddressType_Any),
                                                net::AddressFamily_InterNetwork, Application::RootServerPort);

        m_Logger->Log(lgx::Level::Info, "Binding to (localhost:{})...", ip_endpoint.port);
        if (net::Socket_Bind(m_Socket, ip_endpoint) == CS_SOCKET_ERROR)
            return Err{ ErrType::NetSocketError, "Failed to bind to endpoint ({}:{}).", ip_endpoint.address.str,
                        m_Ep.port };

        return Ok();
    }

    [[nodiscard]] Result<Err> Application::Arg_JoinHandler([[maybe_unused]] std::vector<std::string_view> args) noexcept
    {
        if (auto result = ConnectToRC(); !result)
            return result;

        // BUG: args[1] might not be a number or it might not exist at all.
        net::Packet packet{ net::PacketType::Join };
        packet << std::stoi(args[1].data());
        net::BeginSend(m_Socket, std::move(packet));
        if (auto result = net::BeginReceive(m_Socket).Unwrap(); result.Type() == net::PacketType::Err)
            return Err::FromPacket(std::move(result));

        m_Logger->Log(lgx::Level::Info, "Successfully joined.");
        return Ok();
    }

    [[nodiscard]] Result<Err> Application::Arg_LeaveHandler(
        [[maybe_unused]] std::vector<std::string_view> args) noexcept
    {
        if (auto result = ConnectToRC(); !result)
            return result;

        // BUG: args[1] might not be a number or it might not exist at all.
        net::Packet packet{ net::PacketType::Leave };
        packet << std::stoi(args[1].data());
        net::BeginSend(m_Socket, std::move(packet));
        auto result1 = net::BeginReceive(m_Socket);
        if (auto result = result1.Unwrap(); result.Type() == net::PacketType::Err)
            return Err::FromPacket(std::move(result));

        m_Logger->Log(lgx::Level::Info, "Successfully left.");
        return Ok();
    }

    [[nodiscard]] Result<Err> Application::Arg_CamconfHandler(std::vector<std::string_view> args) noexcept
    {
        m_CameraConfigPath = utils::StrSplit(args[0], '=')[1];

        return LoadCameraConfig();
    }

    [[nodiscard]] Result<Err> Application::Arg_SendStrHandler(std::vector<std::string_view> args) noexcept
    {
        if (auto result = ConnectToRC(); !result)
            return result;

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
            auto packet = recv_packet.Unwrap();
            if (packet.header.type == net::PacketType::Ok)
                m_Logger->Info("Operation succeeded.");
            else
            {
                return Err::FromPacket(std::move(packet));
            }
        }
        else
            return Err{ ErrType::NetBadPacket };

        return Ok();
    }

    [[nodiscard]] Result<Err> Application::Arg_RCCommandHandler(std::vector<std::string_view> args) noexcept
    {
        if (auto result = ConnectToRC(); !result)
            return result;

        // pciemgr rc reboot
        if (args.size() > 1)
        {
            const auto cmd = args[1];
            if (utils::StrLower(cmd) == "reboot")
            {
                // TODO: Write a command handler?
                net::BeginSend(m_Socket, net::Packet{ net::PacketType::Reboot });
                auto reply = net::BeginReceive(m_Socket);
                if (reply)
                {
                    auto packet = reply.Unwrap();
                    switch (packet.header.type)
                    {
                        default:
                        case net::PacketType::Ok: {
                            m_Logger->Info("RC rebooting...");
                            break;
                        }
                        case net::PacketType::Err: {
                            // Receive the error from the server.
                            return Err::FromPacket(std::move(packet));
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
                "Usage: {} rc | root <command>\nList of available commands:\n\treboot\tReboots the Root Complex.",
                GetBinaryName());
            return Ok();
        }
        return Ok();
    }

    [[nodiscard]] Result<Err> Application::Arg_CrewStationHandler(
        [[maybe_unused]] std::vector<std::string_view> args) noexcept
    {
        m_CrewStation = true;
        return Ok();
    }

    [[nodiscard]] Result<Err> Application::Arg_ConcentratorHandler(
        [[maybe_unused]] std::vector<std::string_view> args) noexcept
    {
        if (m_CrewStation)
            return Err{ ErrType::InvalidOperation,
                        "A Node cannot be a crew station and a concentrator at the same time." };

        m_Concentrator = true;
        return Ok();
    }

    [[nodiscard]] Result<Err> Application::Net_StringHandler([[maybe_unused]] Endpoint& ep,
                                                             net::Packet&&              packet) noexcept
    {
        std::string msg;
        packet >> msg;
        m_Logger->Log(lgx::Level::Info, "Ep sent a string: {}", msg);
        ep.Send(Ok());
        return Ok();
    }

    [[nodiscard]] Result<Err> Application::Net_RebootHandler(Endpoint&                      ep,
                                                             [[maybe_unused]] net::Packet&& packet) noexcept
    {
        m_Logger->Log(__func__, lgx::Level::Info, "Rebooting...");

        // Send a fake success packet because this method is never going to return unless the reboot fails (which is
        // unlikely).
        ep.Send(Ok());

        // Synchronise filesystems.
        sync();

        // NOTE: I am commenting this so that I don't accidentally reboot the build server again.
        // reboot(RB_AUTOBOOT);

        // We should never reach here in theory.
        return Ok();
    }

    [[nodiscard]] Result<Err> Application::Net_JoinHandler(Endpoint& ep, [[maybe_unused]] net::Packet&& packet) noexcept
    {
        m_Logger->Log(__func__, lgx::Level::Info, "Node#{} requested to join.", ep.GetID());

        u8 group_id;
        packet >> group_id;
        auto it = std::find(m_Groups[group_id].begin(), m_Groups[group_id].end(), ep.GetID());
        if (it == m_Groups[group_id].end())
            m_Groups[group_id].push_back(ep.GetID());
        else
            return Err{ ErrType::InvalidOperation, "Already in group {}.", group_id };

        ep.Send(Ok());

        return Ok();
    }

    [[nodiscard]] Result<Err> Application::Net_LeaveHandler(Endpoint&                      ep,
                                                            [[maybe_unused]] net::Packet&& packet) noexcept
    {
        m_Logger->Log(__func__, lgx::Level::Info, "Node#{} requested to leave.", ep.GetID());

        u8 group_id;
        packet >> group_id;
        auto it = std::find(m_Groups[group_id].begin(), m_Groups[group_id].end(), ep.GetID());
        if (it != m_Groups[group_id].end())
            m_Groups[group_id].erase(it);
        else
            return Err{ ErrType::InvalidOperation, "Not in group {}. Join first.", group_id };

        ep.Send(Ok());

        return Ok();
    }

    [[nodiscard]] Result<Err> Application::Net_GetCrewConfigHandler(Endpoint&                      ep,
                                                                    [[maybe_unused]] net::Packet&& packet) noexcept
    {
        const auto ep_id = ep.GetID();

        LoadCameraConfig();

        m_Logger->Info("EP#{} requested for crew configuration.", ep_id);

        auto it = std::find_if(m_CrewStations.begin(), m_CrewStations.end(),
                               [ep_id](const auto& crew) { return crew.nodeId == ep_id; });

        nlohmann::json j = it->groups;
        if (it != m_CrewStations.end())
            ep.Send(net::Packet{ net::PacketType::String, j.dump(4) });
        else
            return Err{ ErrType::NotFound, "Node#{} is not a crew station.", ep_id };

        return Ok();
    }

    [[nodiscard]] Result<Err> Application::Net_GetCtrConfigHandler(Endpoint&                      ep,
                                                                   [[maybe_unused]] net::Packet&& packet) noexcept
    {
        const auto ep_id = ep.GetID();
        m_Logger->Info("EP#{} requested for concentrator configuration.", ep_id);

        LoadCameraConfig();

        nlohmann::json j;
        auto           crew_it = std::find_if(m_CrewStations.begin(), m_CrewStations.end(),
                                              [ep_id](const auto& crew) { return crew.nodeId == ep_id; });

        if (crew_it != m_CrewStations.end())
        {
            j["nodeId"] = crew_it->nodeId;
            for (const auto group_id : crew_it->groups)
            {
                auto cam_it = std::find_if(m_Cameras.begin(), m_Cameras.end(),
                                           [group_id](const auto& cam) { return cam.id == group_id; });
                if (cam_it != m_Cameras.end())
                    j["cameras"].push_back(*cam_it);
            }
        }
        else
            return Err{ ErrType::InvalidOperation, "Ep# {} did not match any crew stations.", ep_id };

        ep.Send(net::Packet{ j.dump(4) });
        return Ok();
    }

    [[nodiscard]] Result<Err> Application::Arg_GSTHandler([[maybe_unused]] std::vector<std::string_view> args) noexcept
    {
        if (auto result = ConnectToRC(); !result)
            return result;

        std::vector<pid_t> pids;
        pids.reserve(m_Cameras.size());

        for (const auto& cam : m_Cameras)
        {
            const auto str = std::vector<std::string>{
                "gst-launch-1.0",
                "nvv4l2camerasrc",
                fmt::format("device=/dev/video{}", cam.videoDev),
                "!",
                "'video/x-raw(memory:NVMM)',",
                fmt::format("width={},", cam.width),
                fmt::format("height={},", cam.height),
                fmt::format("framerate={}/1,", cam.fps),
                fmt::format("'format=(string){}", cam.videoFmt),
                "!",
                "nvvidconv",
                "flip-method=0",
                "!",
                "videoconvert",
                "!",
                "video/x-raw,",
                fmt::format("width={},", cam.width),
                fmt::format("height={},", cam.height),
                fmt::format("framerate={}/1,", cam.fps),
                fmt::format("'format=(string){}", cam.videoFmt),
                "!",
                "ttmcastsink",
                "camera-id=1",
                fmt::format("device=/dev/video{}", cam.videoDev),
            };
            auto pargs = std::vector<char*>{};
            for (auto& e : str)
                pargs.push_back(const_cast<char*>(e.data()));
            pargs.push_back(nullptr);

            pid_t pid = fork();

            switch (pid)
            {
                case 0: {
                    execvp("gst-launch-1.0", pargs.data());
                    Panic("Failed to launch gst-launch-1.0");
                    break;
                }
                case -1: return Err{ ErrType::ForkFailed };
                default: break;
            }

            pids.push_back(pid);
            std::ostringstream os;
            for (const auto& e : str)
                os << e;

            m_Logger->Log(lgx::Info, "GST ({}) Arguments: {}", pid, os.str());
        }

        // TODO: Wait for each process asynchronously via threads?
        for (const auto& p : pids)
        {
            i32 status;
            waitpid(p, &status, 0);
            m_Logger->Log(lgx::Info, "PID {} exited with status code: {}.", p, status);
        }

        return Ok();
    }

    Application& Application::New(const std::vector<std::string_view>& args)
    {
        if (!s_Instance)
        {
            s_Instance = std::unique_ptr<Application>(new Application{ args });
            return *s_Instance;
        }
        throw std::runtime_error("An Application instance already exists.");
    }

    [[nodiscard]] Application& Application::Get()
    {
        if (s_Instance)
            return *s_Instance;
        throw std::runtime_error("Application instance hasn't been.");
    }
} // namespace pmgrd
