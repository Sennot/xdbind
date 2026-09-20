cmake_minimum_required(VERSION 3.21)
project(MacroBinds VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Set GEODE_SDK to Geode SDK 5.10.0")
endif()
add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Macro Binds supports Windows x64 only")
endif()
add_library(${PROJECT_NAME} SHARED src/main.cpp src/bridge.cpp)
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(${PROJECT_NAME} PRIVATE bcrypt)
# Geode must use the same keyword signature as the bcrypt link above.
# xdBot is supplied separately; there are no public headers/libs to fetch.
# Keep the required runtime dependency in mod.json.
setup_geode_mod(${PROJECT_NAME}
    LINK_TYPE PRIVATE
    EXTERNALS "zilko.xdbot:=2.7.7"
)
