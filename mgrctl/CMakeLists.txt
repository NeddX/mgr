project("mgrctl")

file(GLOB_RECURSE MGRCTL_SOURCES "src/*.cpp")
file(GLOB_RECURSE MGRCTL_HEADERS "src/*.h")

add_executable(mgrctl ${MGRCTL_SOURCES} ${MGRD_HEADERS})

set_property(TARGET mgrctl PROPERTY CXX_STANDARD 20)
