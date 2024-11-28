#include "CLI.h"

namespace mgrd {
    CLI::CLI() noexcept
    {
        AddCommand(
            "help", [](const Args& args) { std::cout << "display this command :)" << std::endl; return Status::CommandOk; }, true);
    }

    CLI::Status CLI::Dispatch(const std::string& cmd) noexcept
    {
        Args args = utils::StrSplit(cmd);
        if (!args.empty())
        {
            auto it = m_Commands.find(args[0]);
            if (it != m_Commands.end())
                return it->second(std::move(args));
        }
        return Status::CommandNotFound;
    }
} // namespace mgrd
