#ifndef _SDL3_RENDERER_HPP_
#define _SDL3_RENDERER_HPP_

#include "sdl3-expected.hpp" // Result, sdl_fail, ns::in_place
#include "sdl3-window.hpp"   // Window
#include <SDL3/SDL.h>        // SDL_CreateRenderer, SDL_DestroyRenderer

namespace zod
{

// A Window reference is proof enough: a Window cannot exist unless an
// SDLContext does, so there is no need to ask for the context again here.
// The Window is not stored - only outlived, by declaring this one after it.
class Renderer
{
  struct Key {};   // private tag: only Renderer can name it

public:
  static Result<Renderer> create(const Window& window, int logical_w,
                                 int logical_h, bool vsync = true)
  {
    SDL_Renderer* renderer = SDL_CreateRenderer(window.sdl(), nullptr);
    if (!renderer)
    {
      return sdl_fail("SDL_CreateRenderer");
    }

    const auto mode = SDL_LOGICAL_PRESENTATION_LETTERBOX; // Necessary on PS5
    if (!SDL_SetRenderLogicalPresentation(renderer, logical_w, logical_h, mode))
    {
      SDL_DestroyRenderer(renderer);
      return sdl_fail("SDL_SetRenderLogicalPresentation");
    }

    if (vsync && !SDL_SetRenderVSync(renderer, 1)) // VSync prevents tearing
    {
      SDL_DestroyRenderer(renderer);
      return sdl_fail("SDL_SetRenderVSync");
    }

    return Result<Renderer>(ns::in_place, Key{}, renderer);
  }

  Renderer(Key, SDL_Renderer* renderer) : renderer_{renderer} { }
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  ~Renderer() { SDL_DestroyRenderer(renderer_); }

  // The raw handle, for the SDL calls that want one
  SDL_Renderer* sdl() const { return renderer_; }

private:
  SDL_Renderer* renderer_;
};

} // namespace zod

#endif // _SDL3_RENDERER_HPP_
