#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <Core/Error.h>
#include <Core/Result.h>

namespace pmgrd::utils {
    [[nodiscard]] std::vector<std::string>      StrSplit(const std::string& str, const char delim = ' ') noexcept;
    [[nodiscard]] std::vector<std::string_view> StrSplit(const std::string_view str, const char delim = ' ') noexcept;
    [[nodiscard]] std::string                   StrLower(const std::string_view str) noexcept;

    namespace fs {
        [[nodiscard]] ValuedResult<std::string, Err> ReadToString(const std::string& path) noexcept;
    } // namespace fs

    template <typename Fn>
    constexpr auto BindDelegate(auto* self, Fn delegate)
    {
        return [self, delegate](auto&&... args) { return (self->*delegate)(std::forward<decltype(args)>(args)...); };
    }
} // namespace pmgrd::utils
