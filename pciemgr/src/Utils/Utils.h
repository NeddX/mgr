#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pciemgr::utils {
    [[nodiscard]] std::vector<std::string>      StrSplit(const std::string& str, const char delim = ' ') noexcept;
    [[nodiscard]] std::vector<std::string_view> StrSplit(const std::string_view str, const char delim = ' ') noexcept;
    [[nodiscard]] std::string                   StrLower(const std::string_view str) noexcept;

    template <typename Fn>
    constexpr auto BindDelegate(auto* self, Fn delegate)
    {
        return [self, delegate](auto&&... args) { return (self->*delegate)(std::forward<decltype(args)>(args)...); };
    }
} // namespace pciemgr::utils
