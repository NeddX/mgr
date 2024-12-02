#pragma once

#include <CommonDef.h>

#include <string>

#include <nlohmann/json.hpp>

struct Camera
{
    i32 camera_id;
    i32 resolution_width;
    i32 resolution_height;
    i32 fps;
    i32 depth;
    i32 buffer_count;
    std::string compr_format;
    std::string video_format;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Camera, camera_id, resolution_width, resolution_height, fps, depth, buffer_count, compr_format, video_format)
};
