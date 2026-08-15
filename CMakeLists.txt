cmake_minimum_required(VERSION 3.25)

project(ShaderLab VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ShaderLab v1 supports Windows 10/11 x64 only")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "ShaderLab requires MSVC 2022")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ShaderLab requires a 64-bit toolchain")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(SHADERLAB_ASAN "Build ShaderLab with MSVC AddressSanitizer" OFF)

find_package(Vulkan 1.4 REQUIRED)
find_package(volk CONFIG REQUIRED)
find_path(VMA_INCLUDE_DIR "vk_mem_alloc.h" PATH_SUFFIXES vma REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h" REQUIRED)
find_package(Stb REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(unofficial-shaderc CONFIG REQUIRED)
find_package(unofficial-spirv-reflect CONFIG REQUIRED)

add_executable(shaderlab WIN32
    src/main.cpp
    src/core/Log.cpp
    src/core/Log.h
    src/core/JobSystem.cpp
    src/core/JobSystem.h
    src/rhi/DeletionQueue.cpp
    src/rhi/DeletionQueue.h
    src/rhi/Device.cpp
    src/rhi/Device.h
    src/rhi/Buffer.cpp
    src/rhi/Buffer.h
    src/rhi/Image.cpp
    src/rhi/Image.h
    src/rhi/Swapchain.cpp
    src/rhi/Swapchain.h
    src/render/Renderer.cpp
    src/render/Renderer.h
    src/render/passes/ForwardPass.cpp
    src/render/passes/ForwardPass.h
    src/scene/ModelAsset.cpp
    src/scene/ModelAsset.h
    src/scene/OrbitCamera.cpp
    src/scene/OrbitCamera.h
)

set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(FIXED_SHADER_SOURCES
    assets/shaders/engine/fixed.vert
    assets/shaders/engine/fixed.frag
)
set(FIXED_SHADER_OUTPUTS)
foreach(SHADER_SOURCE IN LISTS FIXED_SHADER_SOURCES)
    get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME)
    set(SHADER_OUTPUT "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT "${SHADER_OUTPUT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUTPUT_DIR}"
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.4 -O
                "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}" -o "${SHADER_OUTPUT}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}"
        VERBATIM
    )
    list(APPEND FIXED_SHADER_OUTPUTS "${SHADER_OUTPUT}")
endforeach()
add_custom_target(shaderlab_fixed_shaders DEPENDS ${FIXED_SHADER_OUTPUTS})
add_dependencies(shaderlab shaderlab_fixed_shaders)

target_compile_definitions(shaderlab PRIVATE
    GLFW_INCLUDE_NONE
    VK_NO_PROTOTYPES
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    VULKAN_HPP_NO_CONSTRUCTORS
    SHADERLAB_SHADER_DIR="${SHADER_OUTPUT_DIR}"
    $<$<CONFIG:Debug>:SHADERLAB_ENABLE_VALIDATION=1>
    $<$<NOT:$<CONFIG:Debug>>:SHADERLAB_ENABLE_VALIDATION=0>
)

target_compile_options(shaderlab PRIVATE
    /W4
    /WX
    /permissive-
    /Zc:__cplusplus
    /utf-8
)
if(SHADERLAB_ASAN)
    target_compile_options(shaderlab PRIVATE /fsanitize=address /Zi)
    target_link_options(shaderlab PRIVATE /INCREMENTAL:NO)
endif()

target_include_directories(shaderlab PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Stb_INCLUDE_DIR}
    ${TINYGLTF_INCLUDE_DIRS}
    ${VMA_INCLUDE_DIR}
)

target_link_libraries(shaderlab PRIVATE
    Vulkan::Headers
    volk::volk
    glfw
    glm::glm
    nlohmann_json::nlohmann_json
    imgui::imgui
    imguizmo::imguizmo
    unofficial::shaderc::shaderc
    unofficial::spirv-reflect
    Shell32
)

set_target_properties(shaderlab PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(shaderlab_core_tests
        tests/core_smoke_tests.cpp
        src/core/Log.cpp
        src/core/Log.h
        src/core/JobSystem.cpp
        src/core/JobSystem.h
        src/rhi/DeletionQueue.cpp
        src/rhi/DeletionQueue.h
    )
    target_compile_options(shaderlab_core_tests PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /utf-8)
    target_include_directories(shaderlab_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME shaderlab_core_tests COMMAND shaderlab_core_tests)
endif()
