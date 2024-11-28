project("mgrd")

file(GLOB_RECURSE MGRD_SOURCES "src/*.cpp")
file(GLOB_RECURSE MGRD_HEADERS "src/*.h")

add_executable(mgrd ${MGRD_SOURCES} ${MGRD_HEADERS})

set_property(TARGET mgrd PROPERTY CXX_STANDARD 20)

# Logex
add_subdirectory("vendor/Logex" "${CMAKE_BINARY_DIR}/Logex")
target_link_libraries(mgrd Logex)
target_include_directories(mgrd PUBLIC "vendor/Logex/include")

# DBus
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED dbus-1)
target_include_directories(mgrd PUBLIC ${DBUS_INCLUDE_DIRS})
target_link_libraries(mgrd ${DBUS_LIBRARIES})

# json
add_subdirectory("vendor/json" "${CMAKE_BINARY_DIR}/json")
target_link_libraries(mgrd nlohmann_json)
target_include_directories(mgrd PUBLIC "vendor/json/include")
