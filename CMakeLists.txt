cmake_minimum_required(VERSION 3.24)
project(BidulgiEngine VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---- External: SDL3 (zlib) ----
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.10/SDL3-3.2.10.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(SDL3)

find_package(OpenGL REQUIRED)

# ---- Engine (static lib; public boundary is the C API) ----
add_library(BidulgiEngine STATIC
  Engine/include/bdg/bdg_api.h
  Engine/Core/Log.h        Engine/Core/Log.cpp
  Engine/Platform/Window.h Engine/Platform/Window.cpp
  Engine/RHI/RHI.h         Engine/RHI/RHI_GL.cpp
  Engine/Runtime/Engine.cpp)
target_include_directories(BidulgiEngine PUBLIC Engine/include PRIVATE Engine)
target_link_libraries(BidulgiEngine PRIVATE SDL3::SDL3-static OpenGL::GL)

# ---- Editor executable (empty window for now) ----
add_executable(BidulgiEditor Editor/main.cpp)
target_link_libraries(BidulgiEditor PRIVATE BidulgiEngine)

if(MSVC)
  target_compile_options(BidulgiEngine PRIVATE /W4 /utf-8)
  target_compile_options(BidulgiEditor PRIVATE /W4 /utf-8)
  # Double-click opens a window only (no console). main() stays the entry point.
  target_link_options(BidulgiEditor PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

# Copy the built editor to Bin/ so it can be opened directly.
add_custom_command(TARGET BidulgiEditor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/Bin
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:BidulgiEditor> ${CMAKE_SOURCE_DIR}/Bin/)
