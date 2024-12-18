#pragma once

#include <CommonDef.h>

#include <string>

#include <nlohmann/json.hpp>

#include <Core/Error.h>
#include <Core/Result.h>

namespace pmgrd {
    /**
     * @brief Represents a camera.
     * */
    struct Camera
    {
        u8          id;          ///< The camera id.
        u8          nodeId;      ///< The node to which the camera belongs to. @note Temporary!
        u8          groupId;     ///< The group to which the camera belongs to.
        u16         width;       ///< The width of the camera.
        u16         height;      ///< The height of the camera.
        u8          fps;         ///< The framerate which the camera suppors.
        u32         depth;       ///< The depth which the camera supports.
        u32         bufferCount; ///< The buffer count.
        std::string comprFmt;    ///< Idk
        std::string videoFmt;    ///< The video format which the camera supports.
        u8          videoDev;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Camera, id, width, height, fps, depth, bufferCount, comprFmt, videoFmt, videoDev)

    public:
        /**
         * @brief Validates the @ref Camera s properties.
         *
         * @return A @ref Result of @see Err.
         * */
        [[nodiscard]] constexpr Result<Err> Validate() const noexcept
        {
            if (id > 16 || fps > 30 || width > 1920 || height > 1080 || width < 640 || height < 480)
                return Err{ ErrType::InvalidCameraConfiguration };
            return Ok();
        }
    };

    /**
     * @brief Represents a crew station.
     * */
    struct CrewStation
    {
        u8              nodeId; ///< The id of the endpoint.
        std::vector<u8> groups; ///< The groups which belong to this node.

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(CrewStation, nodeId, groups)
    };
} // namespace pmgrd
