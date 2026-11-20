cmake_minimum_required(VERSION 3.22.1)
project(kymengine_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED KYMENGINE_VERSION OR "${KYMENGINE_VERSION}" STREQUAL "")
    set(KYMENGINE_VERSION "0.4.0")
endif()

get_filename_component(KYUMIST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

# Vulkan & zlib from Android NDK sysroot
find_library(ANDROID_VULKAN_LIB vulkan REQUIRED)
find_library(ANDROID_ZLIB_LIB z REQUIRED)
find_package(Threads REQUIRED)

if(NOT TARGET Vulkan::Headers)
    add_library(Vulkan::Headers INTERFACE IMPORTED)
    set_target_properties(Vulkan::Headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SYSROOT}/usr/include"
    )
endif()
if(NOT TARGET Vulkan::Vulkan)
    add_library(Vulkan::Vulkan SHARED IMPORTED)
    set_target_properties(Vulkan::Vulkan PROPERTIES
        IMPORTED_LOCATION "${ANDROID_VULKAN_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SYSROOT}/usr/include"
    )
endif()
if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB UNKNOWN IMPORTED)
    set_target_properties(ZLIB::ZLIB PROPERTIES
        IMPORTED_LOCATION "${ANDROID_ZLIB_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SYSROOT}/usr/include"
    )
endif()
set(KYUMIST_ZLIB_INCLUDE_DIRS "${CMAKE_SYSROOT}/usr/include")

include(FetchContent)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    vk-bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG        v1.3.280
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5-docking
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.13.2
    GIT_SHALLOW    TRUE
)

set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE
)

set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-13-3
    GIT_SHALLOW    TRUE
)

set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
set(RMLUI_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    rmlui
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG        6.0
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(spdlog glm vk-bootstrap VulkanMemoryAllocator imgui entt nlohmann_json freetype)
if(TARGET freetype AND NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()
FetchContent_MakeAvailable(rmlui)

FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()

add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_android.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)
target_include_directories(imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)
target_link_libraries(imgui PUBLIC
    Vulkan::Vulkan
    android
    log
)

add_subdirectory("${KYUMIST_ROOT}/engine" "${CMAKE_CURRENT_BINARY_DIR}/kyumist_engine_build")

add_library(native_app_glue STATIC
    "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c"
)
target_include_directories(native_app_glue PUBLIC
    "${ANDROID_NDK}/sources/android/native_app_glue"
)

add_library(kymengine_android SHARED
    android_main.cpp
)

target_link_libraries(kymengine_android PRIVATE
    kyumist_engine
    native_app_glue
    android
    log
    m
    Vulkan::Vulkan
)

target_link_options(kymengine_android PRIVATE "-u" "ANativeActivity_onCreate")
