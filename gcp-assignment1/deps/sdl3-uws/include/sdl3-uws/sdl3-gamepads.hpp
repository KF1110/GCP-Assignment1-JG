#ifndef _SDL3_GAMEPADS_HPP_
#define _SDL3_GAMEPADS_HPP_

#include <glm/glm.hpp>       // glm::vec2, glm::length
#include "sdl3-typedefs.hpp" // GamepadPtr
#include <SDL3/SDL.h>        // SDL_OpenGamepad, SDL_GetGamepadButton etc.
#include <bitset>            // std::bitset
#include <iterator>          // std::ssize
#include <utility>           // std::move
#include <vector>            // std::vector

namespace zod
{

// Which of a controller's two sticks is meant.  SDL orders its axes LEFTX,
// LEFTY, RIGHTX, RIGHTY; naming them here keeps that ordering in one place.
enum class Stick
{
  Left,
  Right
};

// Every controller SDL has announced, held open for as long as it stays
// plugged in, in the slot belonging to the player holding it.
//
// A controller must be *opened* before SDL reports its buttons at all. An
// unopened Steam Controller stays in the desktop mode it starts in, working
// the mouse and sending Escape from its B button - which quits any program
// that quits on Escape.
//
// Controllers are plugged in and pulled out while a program runs, so unlike a
// Window or a Renderer this is not built once in main. It lives in the
// application class (e.g. Game) and is given the events it polls for.
class Gamepads {
public:
  // A dead zone for each stick: a stick left alone rarely reads exactly zero,
  // so anything shorter than this counts as centred.  One argument sets the
  // left stick only, so pass two to change both.
  explicit Gamepads(float left_dead_zone = 0.1f, float right_dead_zone = 0.1f)
: dead_zone_left_{left_dead_zone}, dead_zone_right_{right_dead_zone} { }

  Gamepads(const Gamepads&)            = delete; // no copying, and so no
  Gamepads& operator=(const Gamepads&) = delete; // moving: it owns its pads

  // Called before the events are polled, once a frame: last frame's presses
  // are no longer new
  void begin_events()
  {
    for (auto& buttons : pressed_)
    {
      buttons.reset();
    }
  }

  // Hand it every event; it keeps the three it cares about.  Each GamepadPtr
  // closes its own controller, so there is nothing to do at the end.
  void handle(const SDL_Event& e)
  {
    if (e.type == SDL_EVENT_GAMEPAD_ADDED)
    {
      add(SDL_OpenGamepad(e.gdevice.which));
    }
    else if (e.type == SDL_EVENT_GAMEPAD_REMOVED)
    {
      const int player = slot_of(e.gdevice.which);
      if (player >= 0)
      {
        pads_[player].reset();    // closes it
        pressed_[player].reset(); // and forgets what it was pressing
      }
    }
    else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
    {
      const int player = slot_of(e.gbutton.which);
      if (player >= 0)
      {
        pressed_[player].set(e.gbutton.button); // a press vs. a hold
      }
    }
  }

  // Was this button pressed during this frame?  A press happens once, as the
  // button goes down.
  bool pressed(SDL_GamepadButton button, int player = 0) const
  {
    return in_range(player) && pressed_[player].test(button);
  }

  // Is it held down now?  Being held lasts for as long as the button is down.
  bool held(SDL_GamepadButton button, int player = 0) const
  {
    SDL_Gamepad* pad = sdl(player);
    return pad && SDL_GetGamepadButton(pad, button);
  }

  // How far one axis is pushed: -1 to +1 for a stick, 0 to +1 for a trigger,
  // and 0 when that player has no controller.
  //
  // SDL's range is -32768 to 32767, so dividing by 32768 keeps the result
  // within -1 to +1 and nothing needs clamping.  Being a power of two it
  // also divides exactly, which a compiler turns into a multiplication.
  float axis(SDL_GamepadAxis a, int player = 0) const
  {
    SDL_Gamepad* pad = sdl(player);
    return pad ? SDL_GetGamepadAxis(pad, a) / 32768.0f : 0.0f;
  }

  // A stick as the two-dimensional thing it is: right and down are positive,
  // as on screen.  Measuring both axes together keeps the dead zone the same
  // in every direction, where a test per axis would make the corners easier
  // to reach than the edges.
  glm::vec2 stick(Stick s, int player = 0) const
  {
    const bool      left = (s == Stick::Left);
    const glm::vec2 pushed{
      axis(left ? SDL_GAMEPAD_AXIS_LEFTX : SDL_GAMEPAD_AXIS_RIGHTX, player),
      axis(left ? SDL_GAMEPAD_AXIS_LEFTY : SDL_GAMEPAD_AXIS_RIGHTY, player)};

    const float dead_zone = left ? dead_zone_left_ : dead_zone_right_;
    const float length    = glm::length(pushed);
    if (length <= dead_zone)
    {
      return {0.0f, 0.0f};
    }

    // What is left is spread over the whole range, so the stick creeps away
    // from nothing as it leaves the dead zone rather than jumping, and is
    // never longer than 1, however it is pushed
    const float scale = (length - dead_zone) / (1.0f - dead_zone);
    return pushed * ((scale > 1.0f ? 1.0f : scale) / length);
  }

  // A player's controller, for the SDL calls that want one; null when that
  // player has none
  SDL_Gamepad* sdl(int player = 0) const
  {
    return in_range(player) ? pads_[player].get() : nullptr;
  }

private:
  bool in_range(int player) const
  {
    return player >= 0 && player < std::ssize(pads_); // ssize: signed, so no
  }                                                   // cast, and no warning

  // Put a newly opened controller in the slot SDL gave its player.  SDL
  // numbers every gamepad as it announces it, so one without a number is
  // not ours to guess at.
  void add(SDL_Gamepad* opened)
  {
    GamepadPtr pad{opened};
    const int  player = pad ? SDL_GetGamepadPlayerIndex(pad.get()) : -1;
    if (player < 0)
    {
      return;
    }

    if (player >= std::ssize(pads_))
    {
      pads_.resize(player + 1); // SDL numbers as it likes; follow along
      pressed_.resize(player + 1);
    }

    pressed_[player].reset();
    pads_[player] = std::move(pad); // closes whatever that slot held
  }

  // Which slot holds the controller an event came from?  An event names it
  // by its instance id, so the slots are searched; the reads above need no
  // search, being asked for a player directly.
  int slot_of(SDL_JoystickID id) const
  {
    for (int i = 0; i < std::ssize(pads_); ++i)
    {
      if (pads_[i] && SDL_GetGamepadID(pads_[i].get()) == id)
      {
        return i;
      }
    }
    return -1;
  }

  // Four controller slots to begin with; a fifth controller grows them.
  static constexpr int initial_slots_ = 4;

  std::vector<GamepadPtr> pads_ = std::vector<GamepadPtr>(initial_slots_);
  std::vector<std::bitset<SDL_GAMEPAD_BUTTON_COUNT>> pressed_ =
    std::vector<std::bitset<SDL_GAMEPAD_BUTTON_COUNT>>(initial_slots_);

  const float dead_zone_left_;
  const float dead_zone_right_;
};

} // namespace zod

#endif // _SDL3_GAMEPADS_HPP_
