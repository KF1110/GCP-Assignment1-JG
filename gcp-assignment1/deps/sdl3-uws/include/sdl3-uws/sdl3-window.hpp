#ifndef _SDL3_WINDOW_HPP_
#define _SDL3_WINDOW_HPP_

#include "sdl3-context.hpp"  // SDLContext
#include "sdl3-expected.hpp" // Result, sdl_fail, ns::in_place
#include <SDL3/SDL.h>        // SDL_CreateWindow, SDL_DestroyWindow

namespace zod
{

// Asking for an SDLContext is how this class proves SDL_Init has been called:
// there is no other way to obtain one.  Declare the context first in main, and
// its destructor - SDL_Quit - is then guaranteed to run after this one.
class Window
{
  struct Key {};   // private tag: only Window can name it

public:
  // No flags by default, so the window is the size it was asked for and
  // stays that way.  That is worth relying on: a program can then measure the
  // window once, as it starts, with SDL_GetWindowSizeInPixels, rather than
  // asking again every frame in case it has changed.
  static Result<Window> create(const SDLContext&, const char* title, int w,
                               int h, SDL_WindowFlags flags = 0)
  {
    SDL_Window* window = SDL_CreateWindow(title, w, h, flags);
    if (!window)
    {
      return sdl_fail("SDL_CreateWindow");
    }

    return Result<Window>(ns::in_place, Key{}, window);
  }

  Window(Key, SDL_Window* window) : window_{window} { }
  Window(const Window&) = delete;            // Built in place by create(), and
  Window& operator=(const Window&) = delete; // never moved: it stays put

  ~Window() { SDL_DestroyWindow(window_); }

  // The raw handle, for the SDL calls that want one
  SDL_Window* sdl() const { return window_; }

private:
  SDL_Window* window_;
};

} // namespace zod

#endif // _SDL3_WINDOW_HPP_
