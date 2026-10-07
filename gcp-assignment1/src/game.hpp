#ifndef GAME_HPP
#define GAME_HPP

// The program's one class: everything that happens while the window is open.
//
// main() makes the things that have to be made - the window, the device, the
// shaders, the buffers, the model - and hands them to a Game, which borrows
// them for as long as it runs.  Game owns none of them: main outlives it, and
// Zod's classes release what they hold when main's locals go out of scope, in
// the reverse of the order they were declared.
//
// Four functions run in turn, once a frame, and nearly everything you add
// will go into one of them:
//
//   process_events  what the player did: the pad, the keyboard, the window
//   update          what has changed since the last frame
//   interface       what the ImGui HUD display panel should show
//   render          what the screen should now show
//
// The split matters.  Deciding things in render() works until the day
// something else needs to know what was decided.

#include "sdl3-uws/sdl3-camera.hpp"      // Camera
#include "sdl3-uws/sdl3-gamepads.hpp"    // Gamepads, Stick
#include "sdl3-uws/sdl3-gltf.hpp"        // GltfModel, Mat4
#include "sdl3-uws/sdl3-gpu.hpp"         // GpuDevice, GpuPipeline, GpuBuffer...
#include "sdl3-uws/sdl3-imgui.hpp"       // Gui
#include "sdl3-uws/sdl3-point-light.hpp" // PointLight
#include "sdl3-uws/sdl3-window.hpp"      // Window
#include <SDL3/SDL.h>                    // SDL_Window, SDL_GPUDevice etc.
#include <cstddef>                       // std::size_t
#include <span>                          // std::span

// How many lights the shader is given.  shaders/model.frag declares an array
// of exactly this many, and Game::render pushes exactly this many: change one
// and you must change the other, or the shader reads bytes that were never
// sent.  That is the quiet kind of mistake, so the static_assert in game.cpp
// is there to make at least the size of it loud.
constexpr int light_count = 1;

// One joint, as the vertex shader is given it: three rows of four numbers,
// rather than the four columns of four that a matrix holds.  A joint matrix's
// fourth row is always (0, 0, 0, 1), so sending it would be sending nothing -
// and there is a hard limit on what can be sent, which the next comment is
// about.
struct JointRows
{
  float row[3][4];
};

// How many joints the vertex shader has room for.  Everything pushed as a
// uniform has to fit in four kilobytes: a shader is shown the first 4096 bytes
// of what was pushed and reads nothing after that.  Three rows to a joint is
// forty-eight bytes instead of sixty-four, which is what makes eighty-four of
// them fit alongside the camera's matrix - enough for this raphael's
// fifty-seven.
constexpr std::size_t shader_joint_count = 84;

// What the vertex shader is given each frame: the camera, and then the pose
// the animation has reached.  The two static_asserts in game.cpp check that it
// is the size the shader expects, and that it fits.
struct Transform
{
  glm::mat4 view_projection; // from zod::Camera
  JointRows joints[shader_joint_count];
};

class Game
{
public:
  Game(const zod::Window& window, const zod::GpuDevice& device,
       const zod::GpuPipeline& pipeline, const zod::GpuBuffer& vertices,
       const zod::GpuBuffer& indices,
       std::span<SDL_GPUTexture* const> images, const zod::GpuSampler& sampler,
       const zod::GpuDepthTarget& depth, const zod::Gui& gui,
       zod::GltfModel& model);

  // The whole of the program's life: loop until something asks to stop.
  void run();

private:
  void process_events(bool& running);
  void update();
  void interface();  // the ImGui window, worked out before anything is drawn
  void render();

  // All borrowed: main owns them, and outlives this Game.  images_ is a view
  // of main's vector of handles, and the textures themselves belong to the
  // bank declared beside it there.
  SDL_Window*                      window_;
  SDL_GPUDevice*                   device_;
  SDL_GPUGraphicsPipeline*         pipeline_;
  SDL_GPUBuffer*                   vertices_;
  SDL_GPUBuffer*                   indices_;
  std::span<SDL_GPUTexture* const> images_;
  SDL_GPUSampler*                  sampler_;
  SDL_GPUTexture*                  depth_;
  zod::GltfModel&                  model_;

  // The lights, worked out from the model's size in the constructor and then
  // left alone.  zod::PointLight is laid out as the fragment shader reads it,
  // so render() pushes the array as it stands.
  zod::PointLight lights_[light_count];

  zod::Gamepads pads_;
  zod::Camera   camera_;          // where we view from, and through what lens

  float       panel_pad_ = 8.0f;  // the gap from the corner to the ImGui panel
  float       radius_    = 1.0f;  // the model's, for the near and far planes
  std::size_t animation_ = 0;     // which of the model's animations is playing
  float       time_      = 0.0f;  // how far into it, in seconds
  Uint64      last_tick_ = SDL_GetTicks();
};

#endif // GAME_HPP
