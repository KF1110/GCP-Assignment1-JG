#ifndef _SDL3_IMGUI_HPP_
#define _SDL3_IMGUI_HPP_

#include "sdl3-expected.hpp" // Result, fail, ns::in_place
#include "sdl3-gpu.hpp"      // GpuDevice
#include "sdl3-window.hpp"   // Window
#include <SDL3/SDL.h>        // SDL_GetGPUSwapchainTextureFormat
#include <imgui.h>           // ImGui::CreateContext, ImGuiIO
#include <imgui_impl_sdl3.h> // ImGui_ImplSDL3_InitForSDLGPU
#include <imgui_impl_sdlgpu3.h> // ImGui_ImplSDLGPU3_Init

namespace zod
{

// Dear ImGui, turned on and off.
//
// ImGui keeps its state in a context of its own, and talks to the outside world
// through two "backends": one that reads SDL's events and window, and one that
// draws with SDL_GPU.  Three things have to be started, and the same three
// stopped, in the opposite order - and all of it between the window and the
// device being made and unmade.  Asking create() for a Window and a GpuDevice
// is what arranges that: this cannot be declared before them, so it is
// destroyed before them.
//
// Only the starting and stopping are here.  The per-frame calls stay in the
// program that draws, where they can be seen:
//
//   ImGui_ImplSDLGPU3_NewFrame();          // once a frame, before the widgets
//   ImGui_ImplSDL3_NewFrame();
//   ImGui::NewFrame();
//   ...the windows and widgets...
//   ImGui::Render();                       // works out what to draw
//   ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmd);
//   ...and inside a render pass:
//   ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmd, pass);
//
// and every SDL event should be handed to ImGui_ImplSDL3_ProcessEvent.
class Gui
{
  struct Key {};   // private tag: only Gui can name it

public:
  static Result<Gui> create(const Window& window, const GpuDevice& device)
  {
    IMGUI_CHECKVERSION(); // the headers and the built library must match

    // ImGui allocates with malloc by default, and on a PS5 the libc heap is
    // not where this project's memory comes from: SDL is built to allocate
    // through sceKernelAllocateDirectMemory (SDL_malloc_playstation.c).  Point
    // ImGui at the same allocator, before the context that will use it exists.
    // ps5-libc-heap.cpp, beside this header, makes the libc heap usable in its
    // own right, so this is no longer load-bearing - but one allocator is
    // easier to reason about than two.
    ImGui::SetAllocatorFunctions(
      [](size_t size, void*) -> void* { return SDL_malloc(size); },
      [](void* ptr, void*)            { SDL_free(ptr); });

    if (!ImGui::CreateContext())
    {
      return fail("ImGui::CreateContext");
    }

    // No imgui.ini beside the program: there is nothing here worth remembering
    // between runs, and a console has nowhere to write it anyway.
    ImGui::GetIO().IniFilename = nullptr;

    // ImGui can drive its own menus from the keyboard and a gamepad, with
    // ImGuiConfigFlags_NavEnableKeyboard and ...NavEnableGamepad.  Neither is
    // switched on here, because a program using this header is likely to read
    // the keyboard and the pad itself - and because ImGui's gamepad support
    // would open the pads a second time, behind zod::Gamepads' back.
    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForSDLGPU(window.sdl()))
    {
      ImGui::DestroyContext();
      return fail("ImGui_ImplSDL3_InitForSDLGPU");
    }

    // The drawing backend makes a pipeline of its own, so it has to be told
    // what it will be drawing into: the same swapchain format the rest of the
    // drawing uses.
    ImGui_ImplSDLGPU3_InitInfo info{};
    info.Device            = device.sdl();
    info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device.sdl(),
                                                              window.sdl());
    info.MSAASamples       = SDL_GPU_SAMPLECOUNT_1;

    if (!ImGui_ImplSDLGPU3_Init(&info))
    {
      ImGui_ImplSDL3_Shutdown();
      ImGui::DestroyContext();
      return fail("ImGui_ImplSDLGPU3_Init");
    }

    return Result<Gui>(ns::in_place, Key{});
  }

  Gui(Key) { }             // You can't construct this; call create instead
  Gui(const Gui&) = delete;
  Gui& operator=(const Gui&) = delete;

  ~Gui()
  {
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();
  }

  // How big the text is.  The default suits a mouse a foot from a monitor; a
  // console is watched from across a room, so ask for more there.
  void scale_text(float scale) const
  {
    ImGui::GetStyle().FontScaleMain = scale;
  }
};

} // namespace zod

#endif // _SDL3_IMGUI_HPP_
