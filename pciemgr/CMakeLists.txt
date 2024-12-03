project("pciepciemgr")

file(GLOB_RECURSE PCIEMGR_SOURCES "src/*.cpp")
file(GLOB_RECURSE PCIEMGR_HEADERS "src/*.h")

add_executable(pciemgr ${PCIEMGR_SOURCES} ${PCIEMGR_HEADERS})

set_property(TARGET pciemgr PROPERTY CXX_STANDARD 20)

if (CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(pciemgr PRIVATE
    -Wall # Enable all warnings.
    -Wextra # Enable extra warnings.
    -Werror # Treat warnings as errors.
    -pedantic # Enforce strict ISO C++ compliance.
    -fsanitize=address,undefined # Enable Address and Undefined sanitizers.
  )

  # For Address Sanitizer.
  target_link_options(pciemgr PRIVATE -fsanitize=address)
endif()

# Include src directory for ease of use.
target_include_directories(pciemgr PRIVATE "src/")

# Logex
add_subdirectory("vendor/Logex" "${CMAKE_BINARY_DIR}/Logex")
target_link_libraries(pciemgr Logex)
target_include_directories(pciemgr PRIVATE "vendor/Logex/include")

# json
add_subdirectory("vendor/json" "${CMAKE_BINARY_DIR}/json")
target_link_libraries(pciemgr nlohmann_json)
target_include_directories(pciemgr PRIVATE "vendor/json/include")

# Install
install(TARGETS pciemgr DESTINATION bin)

if (CMAKE_BUILD_TYPE STREQUAL "Shipping")
  install(FILES "infra/pciepciemgr.service" DESTINATION /etc/systemd/system)
endif()
