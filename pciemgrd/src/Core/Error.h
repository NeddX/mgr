#pragma once

#include <CommonDef.h>

#include <iostream>

#include <stdarg.h>
#include <string>

#include <fmt/core.h>

#define TRY_UNWRAP(x)                                                                                                  \
    if (auto result = x; !result)                                                                                      \
        return result.UnwrapErr();

namespace pmgrd {
    // Forward declarations.
    namespace net {
        struct Packet;
    }

    // NOTE: Do not forget to add an entry inside ErrTypeToStr in Error.cpp when adding a new error type!
    enum class ErrType : u8
    {
        // Etc
        InvalidOperation,
        InvalidState,
        Timeout,
        JsonParseError,
        NotFound,

        // CLI releated errors
        UnknownCommand,
        UnknownSubCommand,
        UnknownArgument,

        // Network releated errors
        NetSocketError,
        NetConnectionTimeout,
        NetBadPacket,
        NetListenFailure,
        NetWriteFailure,
        NetReadFailure,
        NetReadyFailure,

        // Camera releated
        InvalidCameraConfiguration,

        // I/O releated errors
        IOError,

        // System
        ForkFailed
    };

    [[nodiscard]] const char* ErrTypeToStr(const ErrType type) noexcept;

    struct Err
    {
        friend struct net::Packet;

    private:
        ErrType     m_Type;
        std::string m_Message;

    public:
        constexpr Err() noexcept = default;
        constexpr Err(const ErrType type) noexcept
            : m_Type(type)
        {
        }
        constexpr Err(const ErrType type, std::string message) noexcept
            : m_Type(type)
            , m_Message(std::move(message))
        {
        }
        template <typename... TArgs>
        Err(const ErrType type, const std::string_view fmt, TArgs&&... args)
            : m_Type(type)
            , m_Message(fmt::vformat(fmt, fmt::make_format_args(std::forward<TArgs>(args)...)))
        {
        }
        template <typename... TArgs>
        Err(const std::string_view fmt, TArgs&&... args)
            : m_Type(ErrType::InvalidOperation)
            , m_Message(fmt::vformat(fmt, fmt::make_format_args(std::forward<TArgs>(args)...)))
        {
        }
        /* TODO: Make this guy non-copyable, for now we have issues regarding ValuedResult.
        constexpr Err(Err&& other) noexcept : m_Type(std::move(other.m_Type)), m_Message(std::move(other.m_Message))
        {

        }
        constexpr Err& operator=(Err&& other) noexcept
        {
            Err{std::move(other)}.Swap(*this);
            return *this;
        }
        */

    public:
        constexpr Err& Swap(Err& other) noexcept
        {
            std::swap(m_Type, other.m_Type);
            std::swap(m_Message, other.m_Message);
            return *this;
        }
        [[nodiscard]] constexpr u8          Code() const noexcept { return static_cast<u8>(m_Type); }
        [[nodiscard]] constexpr ErrType     Type() const noexcept { return m_Type; }
        [[nodiscard]] constexpr std::string Message() const noexcept { return m_Message; }
        [[nodiscard]] constexpr bool        HasMessage() const noexcept { return !m_Message.empty(); }

    public:
        [[nodiscard]] static Err FromPacket(net::Packet&& packet);
    };
} // namespace pmgrd

namespace fmt {
    template <>
    struct formatter<pmgrd::Err> : formatter<std::string_view>
    {
        auto format(const pmgrd::Err& err, format_context& ctx) const
        {
            if (err.HasMessage())
            {
                return fmt::format_to(ctx.out(), "Error Type: {}\n\tMessage: {}", pmgrd::ErrTypeToStr(err.Type()),
                                      err.Message());
            }
            else
                return fmt::format_to(ctx.out(), "Error Type: {}", pmgrd::ErrTypeToStr(err.Type()));
        }
    };
} // namespace fmt
