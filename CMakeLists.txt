cmake_minimum_required(VERSION 3.24)
project(KyumistEngine VERSION 0.2.2 LANGUAGES C CXX)

if(NOT DEFINED KYMENGINE_VERSION OR "${KYMENGINE_VERSION}" STREQUAL "")
    set(KYMENGINE_VERSION "${PROJECT_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB QUIET)
find_package(Java COMPONENTS Development REQUIRED)
if(NOT DEFINED ENV{JAVA_HOME} OR "$ENV{JAVA_HOME}" STREQUAL "")
    file(REAL_PATH "${Java_JAVA_EXECUTABLE}" _JAVA_REALPATH)
    get_filename_component(_JAVA_BIN_DIR "${_JAVA_REALPATH}" DIRECTORY)
    get_filename_component(_JAVA_HOME_DIR "${_JAVA_BIN_DIR}" DIRECTORY)
    set(ENV{JAVA_HOME} "${_JAVA_HOME_DIR}")
endif()
find_package(JNI REQUIRED)
include(UseJava)

include(FetchContent)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE
)

# GLFW 3.4
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)

# GLM 1.0.1
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)

# vk-bootstrap
FetchContent_Declare(
    vk-bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG        v1.3.280
    GIT_SHALLOW    TRUE
)

# VulkanMemoryAllocator (VMA)
FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
    GIT_SHALLOW    TRUE
)

# Dear ImGui (docking)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5-docking
    GIT_SHALLOW    TRUE
)

# EnTT (ECS)
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.13.2
    GIT_SHALLOW    TRUE
)

# nlohmann/json
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# ImGuizmo
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        105e4fde45e4e74aae8973d98750820ef7aca7e2
    GIT_SHALLOW    TRUE
)

# miniaudio
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(spdlog glfw glm vk-bootstrap VulkanMemoryAllocator imgui entt nlohmann_json)

if(NOT ZLIB_FOUND)
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG        v1.3.1
        GIT_SHALLOW    TRUE
    )
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)
    if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
    set(KYUMIST_ZLIB_INCLUDE_DIRS "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
endif()

FetchContent_GetProperties(imguizmo)
if(NOT imguizmo_POPULATED)
    FetchContent_Populate(imguizmo)
endif()

FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()

add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
    "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp"
)
target_include_directories(imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
    "${imguizmo_SOURCE_DIR}/src"
)
target_compile_definitions(imgui PUBLIC
    GLFW_INCLUDE_NONE
    GLFW_INCLUDE_VULKAN
)
target_link_libraries(imgui PUBLIC
    glfw
    Vulkan::Vulkan
)

# Shader compilation (GLSL -> SPIR-V)
find_program(
    GLSLANG_VALIDATOR
    NAMES glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
    REQUIRED
)

set(SHADER_DIR "${CMAKE_SOURCE_DIR}/assets/shaders")
set(VERT_SHADER "${SHADER_DIR}/triangle.vert")
set(FRAG_SHADER "${SHADER_DIR}/triangle.frag")
set(VERT_SPV "${SHADER_DIR}/triangle.vert.spv")
set(FRAG_SPV "${SHADER_DIR}/triangle.frag.spv")
set(SCENE_VERT_SHADER "${SHADER_DIR}/scene.vert")
set(SCENE_FRAG_SHADER "${SHADER_DIR}/scene.frag")
set(SCENE_VERT_SPV "${SHADER_DIR}/scene.vert.spv")
set(SCENE_FRAG_SPV "${SHADER_DIR}/scene.frag.spv")

add_custom_command(
    OUTPUT "${VERT_SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${VERT_SHADER}" -o "${VERT_SPV}"
    DEPENDS "${VERT_SHADER}"
    COMMENT "Compiling triangle.vert to SPIR-V"
)

add_custom_command(
    OUTPUT "${FRAG_SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${FRAG_SHADER}" -o "${FRAG_SPV}"
    DEPENDS "${FRAG_SHADER}"
    COMMENT "Compiling triangle.frag to SPIR-V"
)

add_custom_command(
    OUTPUT "${SCENE_VERT_SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${SCENE_VERT_SHADER}" -o "${SCENE_VERT_SPV}"
    DEPENDS "${SCENE_VERT_SHADER}"
    COMMENT "Compiling scene.vert to SPIR-V"
)

add_custom_command(
    OUTPUT "${SCENE_FRAG_SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${SCENE_FRAG_SHADER}" -o "${SCENE_FRAG_SPV}"
    DEPENDS "${SCENE_FRAG_SHADER}"
    COMMENT "Compiling scene.frag to SPIR-V"
)

add_custom_target(kyumist_shaders DEPENDS "${VERT_SPV}" "${FRAG_SPV}" "${SCENE_VERT_SPV}" "${SCENE_FRAG_SPV}")

add_jar(kyumist_java_api
    SOURCES
        "${CMAKE_SOURCE_DIR}/java-api/src/main/java/dev/kyumist/NativeBridge.java"
        "${CMAKE_SOURCE_DIR}/java-api/src/main/java/dev/kyumist/Entity.java"
        "${CMAKE_SOURCE_DIR}/java-api/src/main/java/dev/kyumist/Input.java"
        "${CMAKE_SOURCE_DIR}/java-api/src/main/java/dev/kyumist/Audio.java"
        "${CMAKE_SOURCE_DIR}/java-api/src/main/java/dev/kyumist/KyumistScript.java"
        "${CMAKE_SOURCE_DIR}/java-api/src/main/java/dev/kyumist/ScriptHost.java"
    OUTPUT_NAME kyumist-api
    OUTPUT_DIR "${CMAKE_BINARY_DIR}/java"
)

enable_testing()
add_subdirectory(engine)
add_subdirectory(editor)
add_subdirectory(hub)
add_subdirectory(runtime)
add_subdirectory(tests)

