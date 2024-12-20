#include "Utils.h"

#include <CommonDef.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

namespace pmgrd::utils {
    [[nodiscard]] std::vector<std::string> StrSplit(const std::string& str, const char delim) noexcept
    {
        std::vector<std::string> tokens;
        std::stringstream        ss{ str };
        std::string              token;
        while (std::getline(ss, token, delim))
            tokens.push_back(std::move(token));
        return (tokens.empty()) ? std::vector{ str } : tokens;
    }

    [[nodiscard]] std::vector<std::string_view> StrSplit(const std::string_view str, const char delim) noexcept
    {
        std::vector<std::string_view> tokens;
        usize                         start = 0;
        usize                         end   = str.find(delim);

        while (end != std::string_view::npos)
        {
            tokens.emplace_back(str.substr(start, end - start));
            start = end + 1;
            end   = str.find(delim, start);
        }

        tokens.emplace_back(str.substr(start));
        return tokens;
    }

    [[nodiscard]] std::string StrLower(const std::string_view str) noexcept
    {
        std::string ret_str{ str };
        std::transform(ret_str.begin(), ret_str.end(), ret_str.begin(), [](const char c) { return std::tolower(c); });
        return ret_str; // NVRO
    }

    namespace fs {
        [[nodiscard]] ValuedResult<std::string, Err> ReadToString(const std::string& path) noexcept
        {
            std::ifstream fs{ path };
            if (fs.is_open())
                return std::string((std::istreambuf_iterator<char>(fs)), (std::istreambuf_iterator<char>()));
            else
                return Err{ ErrType::IOError, "Unable to open '{}' for reading.", path };
        }
    } // namespace fs
} // namespace pmgrd::utils
