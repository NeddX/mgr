#include "Error.h"

namespace pciemgr {
    [[nodiscard]] const char* ErrTypeToStr(const ErrType type) noexcept
    {
        /* clang-format off */
        static const char* err_str_arr[] =
        {
            "InvalidOperation",
            "InvalidState",
            "UnknownCommand",
            "UnkownSubCommand",
            "UnknownArgument",

            "Timeout",
            "SocketError",
            "IOError",
            "ConnectionTimeout",
            "JsonParseError",
            "BadPacket"
        };
        /* clang-format on */
        return err_str_arr[static_cast<u8>(type)];
    }
} // namespace pciemgr
