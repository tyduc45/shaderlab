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

find_package(Vulkan 1.4 REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
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
)

target_compile_definitions(shaderlab PRIVATE
    GLFW_INCLUDE_NONE
    VK_NO_PROTOTYPES
    VULKAN_HPP_NO_CONSTRUCTORS
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

target_include_directories(shaderlab PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Stb_INCLUDE_DIR}
    ${TINYGLTF_INCLUDE_DIRS}
)

target_link_libraries(shaderlab PRIVATE
    Vulkan::Vulkan
    volk::volk
    GPUOpen::VulkanMemoryAllocator
    glfw
    glm::glm
    nlohmann_json::nlohmann_json
    imgui::imgui
    imguizmo::imguizmo
    unofficial::shaderc::shaderc
    unofficial::spirv-reflect
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
    )
    target_compile_options(shaderlab_core_tests PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /utf-8)
    target_include_directories(shaderlab_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME shaderlab_core_tests COMMAND shaderlab_core_tests)
endif()
