add_library(kyumist_engine STATIC
    core/Log.cpp
    core/Window.cpp
    core/Application.cpp
    core/InputMap.cpp
    renderer/ModelLoader.cpp
    renderer/VulkanRenderer.cpp
    scene/EditorCamera.cpp
    scene/Scene.cpp
    scene/SceneSerializer.cpp
    scripting/ScriptEngine.cpp
    physics/PhysicsSystem.cpp
    audio/AudioEngine.cpp
    project/ProjectManager.cpp
    ui/TouchControlsOverlay.cpp
    ui/UISystem.cpp
)

if(NOT ANDROID)
    add_dependencies(kyumist_engine kyumist_shaders kyumist_java_api)
    get_filename_component(JVM_LIB_DIR "${JAVA_JVM_LIBRARY}" DIRECTORY)
endif()

target_include_directories(kyumist_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${KYUMIST_ZLIB_INCLUDE_DIRS}
    "${miniaudio_SOURCE_DIR}"
)

if(NOT ANDROID)
    target_include_directories(kyumist_engine PUBLIC ${JNI_INCLUDE_DIRS})
endif()

target_compile_definitions(kyumist_engine PUBLIC
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    KM_ENGINE_VERSION="${KYMENGINE_VERSION}"
)

if(NOT ANDROID)
    target_compile_definitions(kyumist_engine PUBLIC
        GLFW_INCLUDE_NONE
        GLFW_INCLUDE_VULKAN
        KM_SHADER_DIR="${CMAKE_SOURCE_DIR}/assets/shaders"
        KM_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
        KM_JAVA_API_JAR="${CMAKE_BINARY_DIR}/java/kyumist-api.jar"
        KM_SCRIPTS_DIR="${CMAKE_SOURCE_DIR}/assets/scripts"
        KM_SCRIPTS_BUILD_DIR="${CMAKE_BINARY_DIR}/user-scripts-classes"
        KM_JAVAC_EXECUTABLE="${Java_JAVAC_EXECUTABLE}"
    )
endif()

if(WIN32)
    target_compile_definitions(kyumist_engine PUBLIC
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
    )
    if(MSVC)
        target_compile_options(kyumist_engine PUBLIC /utf-8)
    endif()
endif()

target_link_libraries(kyumist_engine PUBLIC
    spdlog::spdlog
    glm::glm
    Vulkan::Vulkan
    vk-bootstrap::vk-bootstrap
    GPUOpen::VulkanMemoryAllocator
    imgui
    EnTT::EnTT
    nlohmann_json::nlohmann_json
    ZLIB::ZLIB
    RmlUi::RmlUi
    ${CMAKE_DL_LIBS}
    Threads::Threads
)

if(ANDROID)
    target_link_libraries(kyumist_engine PUBLIC
        android
        log
        m
    )
else()
    target_link_libraries(kyumist_engine PUBLIC
        glfw
        ${JAVA_JVM_LIBRARY}
    )
    if(NOT WIN32)
        target_link_options(kyumist_engine PUBLIC
            "-Wl,-rpath,$ORIGIN:$ORIGIN/../lib:$ORIGIN/jre/lib/server:${JVM_LIB_DIR}"
        )
    endif()
endif()
