#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Utils/Utils.h"

namespace mgrd {
    class CLI
    {
    public:
        enum class Status
        {
            CommandOk,
            CommandNotFound,
            CommandError
        };

    public:
        using Args      = std::vector<std::string>;
        using CommandFn = std::function<Status(const Args&)>;

    private:
        std::unordered_map<std::string, CommandFn> m_Commands;
        std::vector<std::string> m_History; // TODO: For auto-completion and stuff like that in the future.

    public:
        CLI() noexcept;

    public:
        Status Dispatch(const std::string& cmd) noexcept;

    public:
        inline void AddCommand(const std::string& cmd, CommandFn functor,
                               const bool caseSensitive = false) noexcept
        {
            if (caseSensitive)
                m_Commands[cmd] = functor;
            else
                m_Commands[utils::StrLower(cmd)] = functor;
        }
    };
} // namespace mgrd
