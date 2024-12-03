cmake_minimum_required(VERSION 3.20)

project("pciemgr")

set(CMAKE_EXPORT_COMPILE_COMMANDS on)

include_directories("Common/")

# Doxygen
#find_package(Doxygen REQUIRED dot OPTIONAL_COMPONENTS mscgen dia)

# Configure doxygen.
#set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
#set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

#configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

# If we're not on VS, then copy compile_commands.json for LSPs like clangd and ccls.
message(":: Generator: " ${CMAKE_GENERATOR})
if (NOT CMAKE_GENERATOR MATCHES "Visual Studio")
add_custom_command(
      OUTPUT ${CMAKE_SOURCE_DIR}/compile_commands.json
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/compile_commands.json
              ${CMAKE_SOURCE_DIR}/compile_commands.json
      DEPENDS ${CMAKE_BINARY_DIR}/compile_commands.json
      COMMENT "Copying compile_commands.json...")

    add_custom_target(copy_compile_commands ALL
                      DEPENDS ${CMAKE_SOURCE_DIR}/compile_commands.json)
else()
endif()

add_subdirectory("pciemgr/")
