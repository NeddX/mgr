#pragma once

#include <CommonDef.h>

#include <iostream>

#include <stdarg.h>
#include <string>

#include <fmt/core.h>

namespace pciemgr {
    // Forward declarations.
    namespace net {
        struct Packet;
    }

    // NOTE: Do not forget to add an entry inside ErrTypeToStr in Error.cpp when adding a new error type!
    enum class ErrType : u8
    {
        InvalidOperation,
        InvalidState,
        UnknownCommand,
        UnknownSubCommand,
        UnknownArgument,
        Timeout,
        SocketError,
        IOError,
        ConnectionTimeout,
        JsonParseError,
        BadPacket
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
        constexpr Err(std::string message) noexcept
            : m_Type(ErrType::InvalidOperation)
            , m_Message(std::move(message))
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

    public:
        [[nodiscard]] constexpr ErrType     Type() const noexcept { return m_Type; }
        [[nodiscard]] constexpr std::string Message() const noexcept { return m_Message; }
        [[nodiscard]] constexpr bool        HasMessage() const noexcept { return !m_Message.empty(); }
    };
} // namespace pciemgr

namespace fmt {
    template <>
    struct formatter<pciemgr::Err> : formatter<std::string_view>
    {
        auto format(const pciemgr::Err& err, format_context& ctx) const
        {
            if (err.HasMessage())
            {
                return fmt::format_to(ctx.out(), "Error Type: {}\n\tMessage: {}", pciemgr::ErrTypeToStr(err.Type()),
                                      err.Message());
            }
            else
                return fmt::format_to(ctx.out(), "Error Type: {}", pciemgr::ErrTypeToStr(err.Type()));
        }
    };
} // namespace fmt
