cmake_minimum_required(VERSION 3.24)

project(gcp-assignment1)

set(CMAKE_CXX_STANDARD           20)  # std::span, std::size, designated inits
set(CMAKE_CXX_STANDARD_REQUIRED  ON)  # enforce it
set(CMAKE_CXX_EXTENSIONS        OFF)  # disable compiler-specific extensions

# Exceptions are switched off per target below. /EHsc is put into
# CMAKE_CXX_FLAGS by CMake itself and there is no per-target property for it,
# so this one flag has to be removed here, or MSVC warns D9025.
if(MSVC)
  string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Prospero")
  include(ps5-libs.cmake)
  list(APPEND CMAKE_PREFIX_PATH deps/sdl3-playstation) # SDL3: find_package
  set(RESOURCE_DIR "/app0/resources") # app0 is a mount root, not a common dir
  set(SHADER_ROOT  "/app0")
else()
  set(RESOURCE_DIR "${CMAKE_SOURCE_DIR}/resources")
  set(SHADER_ROOT  "${CMAKE_SOURCE_DIR}")
endif()

# Include directories from an imported target are system includes by default,
# which MSVC spells /external:I - and Visual Studio's IntelliSense does not look
# inside /external:I directories.  The result is that <SDL3/SDL.h> and every SDL
# type below it is underlined in red in the editor, while the build itself
# succeeds, because the compiler does honour the flag.
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)

find_package(SDL3 REQUIRED CONFIG)

# The GLSL in shaders/ is compiled to the SPIR-V that SDL_GPU loads at run
# time.  A .spv is written beside its GLSL source, and rebuilt whenever the
# GLSL is newer, or missing - so editing a shader is enough, as long as the
# program is built again afterwards.
#
# glslangValidator comes with the Vulkan SDK, or from the glslang-tools package
# on Debian and Ubuntu.  -V asks for Vulkan-flavoured SPIR-V, which is what
# SDL_GPU_SHADERFORMAT_SPIRV means.  A Windows copy is bundled in
# deps/spirv-compiler; PATHS is searched last, so it is only a fallback.
find_program(GLSLANG_VALIDATOR
             NAMES glslangValidator
             HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
             PATHS "${CMAKE_SOURCE_DIR}/deps/spirv-compiler"
             NO_CMAKE_FIND_ROOT_PATH)

# CONFIGURE_DEPENDS re-runs the glob during a build, so a new shader is noticed
file(GLOB glsl-shaders CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/shaders/*.vert"          "${CMAKE_SOURCE_DIR}/shaders/*.frag"
     "${CMAKE_SOURCE_DIR}/solution/shaders/*.vert" "${CMAKE_SOURCE_DIR}/solution/shaders/*.frag")

if(GLSLANG_VALIDATOR)
  set(spv-shaders "")
  foreach(glsl ${glsl-shaders})
    add_custom_command(OUTPUT ${glsl}.spv
                       COMMAND ${GLSLANG_VALIDATOR} -V ${glsl} -o ${glsl}.spv
                       DEPENDS ${glsl}
                       COMMENT "Compiling ${glsl} to SPIR-V"
                       VERBATIM)
    list(APPEND spv-shaders ${glsl}.spv)
  endforeach(glsl)
  set(shader-outputs ${spv-shaders})

# The PS5 cannot compile SPIR-V at run time.  Its AGC backend looks each module
# up, by SDL_crc32 of its bytes, in /app0/shaders.ps5.bin.  The cache is built
# here by SPIRVCompiler (SPIR-V to HLSL to PSSL) from a staging directory
# holding every .spv above, together with Dear ImGui's own two, which come
# ready-made in resources/shaders.  The tool writes into its working directory
# and needs PS5.hlsl there, so it runs from a copy in the build.
  if(CMAKE_SYSTEM_NAME STREQUAL "Prospero")
    set(spirv-src "${CMAKE_SOURCE_DIR}/deps/spirv-compiler")
    set(spirv-dir "${CMAKE_BINARY_DIR}/spirv-compiler")
    set(spv-stage "${CMAKE_BINARY_DIR}/spv-stage")
    file(COPY "${spirv-src}/SPIRVCompiler.exe" "${spirv-src}/SDL3.dll"
              "${spirv-src}/PS5.hlsl" DESTINATION "${spirv-dir}")
    set(imgui-spv "${CMAKE_SOURCE_DIR}/resources/shaders/imgui.vert.spv"
                  "${CMAKE_SOURCE_DIR}/resources/shaders/imgui.frag.spv")
    add_custom_command(OUTPUT "${CMAKE_SOURCE_DIR}/shaders.ps5.bin"
                       COMMAND ${CMAKE_COMMAND} -E make_directory "${spv-stage}"
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different
                               ${spv-shaders} ${imgui-spv} "${spv-stage}"
                       COMMAND "${spirv-dir}/SPIRVCompiler.exe" "${spv-stage}"
                       COMMAND ${CMAKE_COMMAND} -E copy shaders.ps5.bin
                               "${CMAKE_SOURCE_DIR}/shaders.ps5.bin"
                       DEPENDS ${spv-shaders} ${imgui-spv}
                               "${spirv-dir}/SPIRVCompiler.exe"
                       WORKING_DIRECTORY "${spirv-dir}"
                       COMMENT "Building the PS5 shader cache, shaders.ps5.bin"
                       VERBATIM)
    list(APPEND shader-outputs "${CMAKE_SOURCE_DIR}/shaders.ps5.bin")
  endif()

  add_custom_target(shaders ALL DEPENDS ${shader-outputs})
else()
  message(WARNING "glslangValidator not found: the .spv files in shaders will "
                  "not be rebuilt. Install the Vulkan SDK, or the "
                  "glslang-tools package.")
endif()

# The Zod library is header-only.  Listing its headers here makes Visual Studio
# show them in the project, beside the program's own source files.
set(inc deps/sdl3-uws/include/sdl3-uws)
set(zod-headers ${inc}/sdl3-expected.hpp ${inc}/sdl3-context.hpp
                ${inc}/sdl3-window.hpp ${inc}/sdl3-gpu.hpp
                ${inc}/sdl3-texture-bank.hpp ${inc}/sdl3-gltf.hpp
                ${inc}/sdl3-camera.hpp ${inc}/sdl3-point-light.hpp
                ${inc}/sdl3-imgui.hpp ${inc}/sdl3-audio.hpp
                ${inc}/sdl3-event.hpp ${inc}/sdl3-gamepads.hpp
                ${inc}/sdl3-typedefs.hpp)

# ps5-libc-heap.cpp sets the PS5's libc heap size.  It is empty off that
# platform, and has to be linked in rather than sitting in a header, because
# the SDK reads its two symbols before main() runs.  It is not part of Zod, so
# it sits in deps/ rather than beside Zod's headers.
#
# source_group files it under "PlayStation" in Visual Studio, so that "Source
# Files" holds just the two .cpp files the project is about.
set(ps5-heap deps/ps5-libc-heap.cpp)
source_group("PlayStation" FILES ${ps5-heap})

# tinygltf, which reads the model.  Version 3 is a C library rather than the
# header-only C++ of version 2, so it is compiled once here.
#
# TINYGLTF3_ENABLE_FS is deliberately *not* defined.  Left off, the parser does
# no file I/O of its own and asks through the callbacks sdl3-gltf.hpp gives it,
# which read with SDL - so a model loads from /app0 on a PS5 like everything
# else here.
set(tinygltf-dir deps/tinygltf-3.0.1)
add_library(tinygltf3 STATIC ${tinygltf-dir}/tiny_gltf_v3.c)
target_include_directories(tinygltf3 PUBLIC ${tinygltf-dir})
set_target_properties(tinygltf3 PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

# Dear ImGui, which draws the panel.  imgui_demo.cpp is left out: it is a
# showcase of every widget, and nothing here calls it.
set(imgui-dir deps/imgui-1.92.9b)

add_library(imgui STATIC
            ${imgui-dir}/imgui.cpp
            ${imgui-dir}/imgui_draw.cpp
            ${imgui-dir}/imgui_tables.cpp
            ${imgui-dir}/imgui_widgets.cpp
            ${imgui-dir}/backends/imgui_impl_sdl3.cpp
            ${imgui-dir}/backends/imgui_impl_sdlgpu3.cpp)

target_include_directories(imgui PUBLIC ${imgui-dir} ${imgui-dir}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
target_compile_options(imgui PRIVATE
  $<IF:$<CXX_COMPILER_ID:MSVC>,/EHs-c-,-fno-exceptions>)
target_compile_definitions(imgui PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:_HAS_EXCEPTIONS=0>)

# ImGui divides the world into Windows and Unix, and a PS5 is neither.  Two of
# its default implementations take "not _WIN32" to mean a Unix, and so will not
# compile for Prospero; both have a supported switch to turn them off.
if(CMAKE_SYSTEM_NAME STREQUAL "Prospero")
  target_compile_definitions(imgui PRIVATE IMGUI_DISABLE_TIME_FUNCTIONS
                                           IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
endif()

# The program, and - where it is present - the finished version of it beside
# it.  Both are built the same way; only where their sources and their shaders
# live differs, so that one can be changed without disturbing the other.
function(add_assignment target source_dir shader_dir)
  add_executable(${target} ${source_dir}/main.cpp ${source_dir}/game.cpp
                           ${source_dir}/game.hpp ${ps5-heap} ${zod-headers})

  # The GLSL this program uses, shown in Visual Studio in a "Shader Files"
  # folder beside "Source Files".  Visual Studio has no compiler for them, so
  # it never builds them; the shaders target above is what turns them into
  # SPIR-V.
  set(glsl-files "${CMAKE_SOURCE_DIR}/${shader_dir}/model.vert"
                 "${CMAKE_SOURCE_DIR}/${shader_dir}/model.frag")
  target_sources(${target} PRIVATE ${glsl-files})
  source_group("Shader Files" FILES ${glsl-files})

  if(TARGET shaders)
    add_dependencies(${target} shaders)
  endif()

  target_include_directories(${target} PRIVATE deps/sdl3-uws/include)
  target_include_directories(${target} PRIVATE deps/sdl3-uws/expected-1.3.1/include)
  target_include_directories(${target} PRIVATE deps/glm-1.0.3)
  target_link_libraries(${target} PRIVATE SDL3::SDL3 imgui tinygltf3)
  target_compile_options(${target} PRIVATE
    $<IF:$<CXX_COMPILER_ID:MSVC>,/EHs-c-,-fno-exceptions>)
  target_compile_definitions(${target} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:_HAS_EXCEPTIONS=0>)
  target_compile_definitions(${target} PRIVATE "RESOURCE_DIR=\"${RESOURCE_DIR}\"")
  target_compile_definitions(${target} PRIVATE "SHADER_DIR=\"${SHADER_ROOT}/${shader_dir}\"")

  # Add PlayStation 5 system libraries when targeting PS5
  if(CMAKE_SYSTEM_NAME STREQUAL "Prospero")
    ps5_link_system_libs(${target})
    set_target_properties(${target} PROPERTIES # Set PS5 /app0/ ${CMAKE_SOURCE_DIR}
                          VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  endif()
endfunction()

add_assignment(assignment1 src shaders)

# The solution is not handed out with the assignment.  Where it is present, it
# is built as a project of its own, beside the program it completes.
if(EXISTS "${CMAKE_SOURCE_DIR}/solution/main.cpp")
  add_assignment(assignment1-solution solution solution/shaders)
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT assignment1)
