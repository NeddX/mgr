#pragma once

#include <CommonDef.h>

#include <functional>
#include <vector>

#include <Logex.h>

#include <Core/Error.h>
#include <Core/Result.h>

namespace pmgrd {
    class CLI
    {
    public:
        using ArgDelegate = std::function<Result<Err>(std::vector<std::string_view>)>;

    public:
        enum class ArgType
        {
            None,
            Option,
            SubCommand,
            Parsed
        };
        struct CLIArg
        {
            std::array<std::string_view, 2> args;
            std::string_view                desc;
            ArgType                         type;
            ArgDelegate                     delegate;
            u8                              order = 0;
        };

    private:
        const std::vector<std::string_view>& m_Args;
        lgx::Logger&                         m_Logger;
        std::vector<CLIArg>                  m_ArgMap;
        u8                                   m_ArgOrder;
        std::string_view                     m_BinaryName;

    public:
        CLI(const std::vector<std::string_view>& args, lgx::Logger& logger) noexcept;

    public:
    public:
        /*
         * @brief Maps arguments to a delegate and appends them to the argument map for handling command-line arguments.
         * @param args Arguments to be mapped.
         * @param delegate Delegate to be associated with args.
         */
        inline void AddArgument(CLIArg arg)
        {
            arg.order = m_ArgOrder++;
            m_ArgMap.push_back(std::move(arg));
        }

    public:
        ValuedResult<bool, Err> DispatchArguments();
    };
} // namespace pmgrd
