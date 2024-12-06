#include <algorithm>
#include <cstring>
#include <iostream>

#include <Core/Application.h>

int main(const int argc, const char** argv)
{
    std::vector<std::string_view> args(argc);
    std::accumulate(args.begin(), args.end(), 0,
                    [argv](std::size_t i, auto& e)
                    {
                        e = std::string_view{ argv[i] };
                        return i + 1;
                    });

    auto& app = pmgrd::Application::New(args);
    if (const auto result = app.Init(); !result)
        return result.UnwrapErr().Code();
    else if (const auto result = app.Run(); !result)
        return result.UnwrapErr().Code();
    return 0;
}
