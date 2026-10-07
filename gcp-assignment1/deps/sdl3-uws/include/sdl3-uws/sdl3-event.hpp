#ifndef _SDL3_EVENT_HPP_
#define _SDL3_EVENT_HPP_

#include <SDL3/SDL.h> // SDL_Event, SDL_EVENT_QUIT, SDLK_ESCAPE etc.

namespace zod
{

// The three ways a program is asked to stop: the window's close
// button, the Escape key, and a button on a gamepad.  SDL names the buttons
// after where they sit on the pad rather than after the symbol any one
// manufacturer prints on them, so East is Circle on a PlayStation pad, and B
// on an Xbox one.  Pass a different button to quit on that one instead.
//
// East, because a PlayStation reads Cross as "choose this" and Circle as "go
// back", and leaving the program is as far back as back goes.  That also
// leaves Cross free for whatever a program wants a button for - playing a
// sound, choosing an item in a menu - and the two then read as a player
// expects: Cross chooses, Circle backs out, and backing out of everything is
// the way to the desktop.
inline bool is_quit(const SDL_Event& e,
                    SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_EAST)
{
  return e.type == SDL_EVENT_QUIT ||
         (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) ||
         (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
          e.gbutton.button == button);
}

} // namespace zod

#endif // _SDL3_EVENT_HPP_
