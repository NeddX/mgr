#include <cstring>
#include <iostream>

#include <Logex.h>

#include "Application.h"

int main(const int argc, const char** argv)
{
    bool        daemon       = false;
    bool        root_complex = false;
    std::string camera_config_file;
    if (argc > 1)
    {
        for (auto i = 0; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--daemon") == 0)
                daemon = true;
            else if (std::strcmp(argv[i], "--rc") == 0)
                root_complex = true;
            else if (std::strncmp(argv[i], "--camconf=", sizeof("--camconf=") - 1) == 0)
            {
                camera_config_file = argv[i] + sizeof("--camconf=") - 1;
            }
        }
    }

    mgrd::Application app{ daemon, root_complex, std::move(camera_config_file) };
    app.Run();
    return 0;
}
