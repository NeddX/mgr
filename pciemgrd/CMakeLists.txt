project("pciemgrdd")

file(GLOB_RECURSE PCIEMGRD_SOURCES "src/*.cpp")
file(GLOB_RECURSE PCIEMGRD_HEADERS "src/*.h")

add_executable(pciemgrd ${PCIEMGRD_SOURCES} ${PCIEMGRD_HEADERS})

set_property(TARGET pciemgrd PROPERTY CXX_STANDARD 20)

if (CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(pciemgrd PRIVATE
    -Wall # Enable all warnings.
    -Wextra # Enable extra warnings.
    -Werror # Treat warnings as errors.
    -pedantic # Enforce strict ISO C++ compliance.
    -fsanitize=address,undefined # Enable Address and Undefined sanitizers.
  )

  # For Address Sanitizer.
  target_link_options(pciemgrd PRIVATE -fsanitize=address)
endif()

# Include src directory for ease of use.
target_include_directories(pciemgrd PRIVATE "src/")

# Logex
add_subdirectory("vendor/Logex" "${CMAKE_BINARY_DIR}/Logex")
target_link_libraries(pciemgrd Logex)
target_include_directories(pciemgrd PRIVATE "vendor/Logex/include")

# json
add_subdirectory("vendor/json" "${CMAKE_BINARY_DIR}/json")
target_link_libraries(pciemgrd nlohmann_json)
target_include_directories(pciemgrd PRIVATE "vendor/json/include")

# Install
install(TARGETS pciemgrd DESTINATION bin)

if (CMAKE_BUILD_TYPE STREQUAL "Shipping")
  install(FILES "infra/pciepciemgrd.service" DESTINATION /etc/systemd/system)
endif()
