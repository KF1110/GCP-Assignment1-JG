#ifndef _SDL3_CONTEXT_HPP_
#define _SDL3_CONTEXT_HPP_

#include "sdl3-expected.hpp" // Result, fail, sdl_fail, ns::in_place
#include <SDL3/SDL.h>        // SDL_Init, SDL_CreateWindow etc.

namespace zod
{

class SDLContext
{
  struct Key {};   // private tag: only SDLContext can name it

public:
  static Result<SDLContext> create(Uint32 flags = SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)
  {
    // SDL_Quit() tears everything down however many contexts are alive, so a
    // second context would be shut down by the first one's destructor
    if (live_)
    {
      return fail("SDL is already initialised");
    }

    if (!SDL_Init(flags))
    {
      return sdl_fail("SDL_Init");
    }

    live_ = true;

    return Result<SDLContext>(ns::in_place, Key{});
  }

  SDLContext(Key) { } // You can't construct this; call create instead
  SDLContext(const SDLContext&) = delete;
  SDLContext& operator=(const SDLContext&) = delete;

  ~SDLContext()
  {
    SDL_Quit();
    live_ = false;
  }

private:
  static inline bool live_ = false; // an inline variable needs no .cpp file
};

} // namespace zod

#endif // _SDL3_CONTEXT_HPP_
