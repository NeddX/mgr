#include "Utils.h"

#include <algorithm>
#include <sstream>

namespace mgrd::utils {
    [[nodiscard]] std::vector<std::string> StrSplit(const std::string& str, const char delim) noexcept
    {
        std::vector<std::string> tokens;
        std::stringstream        ss{ str };
        std::string              token;
        while (std::getline(ss, token, delim))
            tokens.push_back(std::move(token));
        return tokens;
    }

    [[nodiscard]] std::string StrLower(const std::string_view str) noexcept
    {
        std::string ret_str{ str };
        std::transform(ret_str.begin(), ret_str.end(), ret_str.begin(), [](const char c) { return std::tolower(c); });
        return ret_str; // NVRO
    }
} // namespace mgrd::utils
