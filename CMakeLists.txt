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

# ---- External: FreeType (FreeType License, BSD-style) — SDF glyph rendering ----
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
FetchContent_Declare(freetype
  URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-3.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(freetype)
if(MSVC)
  target_compile_options(freetype PRIVATE /utf-8) # its sources contain UTF-8 comments
endif()

# ---- External: yyjson (MIT) — JSON / JSONL reading and writing ----
set(YYJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(yyjson
  URL https://github.com/ibireme/yyjson/archive/refs/tags/0.10.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(yyjson)

find_package(OpenGL REQUIRED)

# ---- External: glad (generated GL 4.5 core loader, vendored) ----
add_library(glad STATIC ThirdParty/glad/src/gl.c)
target_include_directories(glad PUBLIC ThirdParty/glad/include)
set_target_properties(glad PROPERTIES FOLDER ThirdParty)

# ---- Engine (static lib; public boundary is the C API) ----
add_library(BidulgiEngine STATIC
  Engine/include/bdg/bdg_api.h
  Engine/Core/Log.h        Engine/Core/Log.cpp
  Engine/Platform/Window.h Engine/Platform/Window.cpp
  Engine/RHI/RHI.h         Engine/RHI/RHI_GL.cpp
  Engine/Render2D/Batch2D.h Engine/Render2D/Batch2D.cpp
  Engine/Text/Font.h       Engine/Text/Font.cpp
  Engine/Core/Image.h      Engine/Core/Image.cpp
  Engine/UI/Theme.h        Engine/UI/Theme.cpp
  Engine/UI/UI.h           Engine/UI/UI.cpp
  Engine/Runtime/Screens.h Engine/Runtime/Screens.cpp
  Engine/Runtime/Engine.cpp)
target_include_directories(BidulgiEngine PUBLIC Engine/include PRIVATE Engine)
target_link_libraries(BidulgiEngine PRIVATE SDL3::SDL3-static glad freetype yyjson OpenGL::GL)
target_compile_definitions(BidulgiEngine PRIVATE $<$<CONFIG:Debug>:BDG_DEBUG=1> $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)

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
