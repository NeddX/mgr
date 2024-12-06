#include "CLI.h"

#include <Core/Application.h>
#include <Utils/Utils.h>

namespace pmgrd {
    CLI::CLI(const std::vector<std::string_view>& args, lgx::Logger& logger) noexcept
        : m_Args(args)
        , m_Logger(logger)
        , m_ArgOrder(0)
    {
    }

    ValuedResult<bool, Err> CLI::DispatchArguments()
    {
        if (m_Args.size() > 1)
        {
            // Sort the arguments according to their order (skip the first argument which is the binary path).
            std::sort(m_ArgMap.begin(), m_ArgMap.end(),
                      [](const CLIArg& a, const CLIArg& b) { return a.order < b.order; });

            // For unrecognized arguments.
            // ArgType::None represents unrecognized arguments.
            // ArgType::SubCommand represents a sub-command so stop checking.
            // Anything that is not ArgType::None is conisdered recognized.
            std::vector<ArgType> parsed_args(m_Args.size(), ArgType::None);

            // Iterate over the argument map.
            for (auto it = m_ArgMap.begin(); it != m_ArgMap.end(); ++it)
            {
                const auto& e = *it;

                const auto arg_it = std::find_if(m_Args.begin(), m_Args.end(),
                                                 [&e](std::string_view arg)
                                                 {
                                                     arg = utils::StrSplit(arg, '=')[0];
                                                     return e.args[0] == arg || e.args[1] == arg;
                                                 });

                if (arg_it != m_Args.end())
                {
                    parsed_args[std::distance(m_Args.begin(), arg_it)] = it->type;

                    auto sub_args = std::vector<std::string_view>{ arg_it, m_Args.end() };
                    if (auto result = (e.delegate)(std::move(sub_args)); !result)
                        return result.UnwrapErr();
                    else if (e.type == ArgType::SubCommand)
                        // If the previous argument was a sub-command then halt, because the sub-command
                        // already parsed the arguments that came after it.
                        break;
                }
            }

            for (usize i = 1; i < parsed_args.size(); ++i)
            {
                if (parsed_args[i] == ArgType::None)
                    return Err{ ErrType::UnknownCommand, "Unknown argument '{}'.", m_Args[i] };
                else if (parsed_args[i] == ArgType::SubCommand)
                    break;
            }
        }
        else
        {
            fmt::println("Usage:\n\t{} <options> [command] [<args>]", Application::GetBinaryName());
            fmt::println("\nArguments:");
            for (const CLIArg& e : m_ArgMap)
            {
                std::string arg_name;
                for (const auto& name : e.args)
                {
                    arg_name += name;
                    arg_name += " | ";
                }
                arg_name.resize(arg_name.size() - 3);
                fmt::println("\t{}\t\t{}", arg_name, e.desc);
            }

            return false;
        }

        return true;
    }
} // namespace pmgrd
