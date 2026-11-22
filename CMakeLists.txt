cmake_minimum_required(VERSION 3.20)
project(client-manager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(client-manager
    src/main.cpp
    src/util.cpp
    src/wmi.cpp
    src/sysinfo.cpp
    src/proc.cpp
    src/net.cpp
    src/svc.cpp
    src/persist.cpp
    src/software.cpp
    src/fs.cpp
)

target_include_directories(client-manager PRIVATE src)

target_compile_definitions(client-manager PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(client-manager PRIVATE /W4 /EHsc /utf-8)
endif()

enable_testing()
add_executable(json-config-test tests/json_config_test.cpp)
target_include_directories(json-config-test PRIVATE src)
if(MSVC)
    target_compile_options(json-config-test PRIVATE /UNDEBUG /EHsc /utf-8)
else()
    target_compile_options(json-config-test PRIVATE -UNDEBUG)
endif()
add_test(NAME json-config COMMAND json-config-test)

# Win32 + COM + IP Helper + WMI + Task Scheduler + version info.
target_link_libraries(client-manager PRIVATE
    advapi32
    iphlpapi
    ws2_32
    ole32
    oleaut32
    wbemuuid
    shell32
    secur32
    taskschd
    version
    user32
    kernel32
)
