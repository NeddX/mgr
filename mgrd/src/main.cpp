#include <cstring>
#include <iostream>

#include <Logex.h>

#include "Application.h"

int main(const int argc, const char** argv)
{
    std::vector<std::string_view> args(argc);
    std::accumulate(args.begin(), args.end(), 0,
                    [argv](std::size_t i, auto& e)
                    {
                        e = std::string_view{ argv[i] };
                        return i + 1;
                    });

    mgrd::Application app{ args };
    app.Run();
    return 0;
}
