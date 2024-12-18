#include "Error.h"

#include <Net/NetPacket.h>

namespace pmgrd {
    [[nodiscard]] const char* ErrTypeToStr(const ErrType type) noexcept
    {
        /* clang-format off */
        static const char* err_str_arr[] =
        {
        // Etc
        "InvalidOperation",
        "InvalidState",
        "Timeout",
        "JsonParseError",
        "NotFound",

        // CLI releated errors
        "UnknownCommand",
        "UnknownSubCommand",
        "UnknownArgument",

        // Network releated errors
        "NetSocketError",
        "NetConnectionTimeout",
        "NetBadPacket",
        "NetListenFailure",
        "NetReadFailure",
        "NetWriteFailure",
        "NetInitConFailure",

        // Camera releated
        "InvalidCameraConfiguration",

        // I/O releated errors
        "IOError",

        // System
        "ForkFailed"
        };
        /* clang-format on */
        return err_str_arr[static_cast<u8>(type)];
    }

    [[nodiscard]] Err Err::FromPacket(net::Packet&& packet)
    {
        Err err;
        packet >> err;
        return err;
    }
} // namespace pmgrd
