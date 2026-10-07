#include "game.hpp"

#include "sdl3-uws/sdl3-event.hpp" // is_quit
#include <algorithm>               // std::max
#include <cmath>                   // std::sin, std::cos
#include <numbers>                 // std::numbers::pi_v

using namespace zod;

static_assert(sizeof(Transform) == 64 + shader_joint_count * sizeof(JointRows));
static_assert(sizeof(Transform) <= 4096, "a uniform push has 4KB to fit in");

namespace
{

// A depth value further away than anything, to clear the depth buffer to.  The
// projection puts the near plane at 0 and the far plane at 1.
constexpr float far_away = 1.0f;

// How fast the right stick swings the camera: radians per second at full push
constexpr float turn_rate = 2.0f;

// The lens, and the space left around the model once it is framed by the camera
constexpr float field_of_view = 45.0f * std::numbers::pi_v<float> / 180.0f;
constexpr float margin        = 1.1f;

// Where the light stands, measured from the middle of the model in multiples
// of the model's own radius: off to one side, well above, and a little towards
// the opening view.  Said in radii rather than in units because a glTF file
// settles its own scale, and the same three numbers then suit any model.
constexpr glm::vec3 light_offset{2.0f, 3.0f, -2.0f};

// How bright the light is where the model is.  The shader divides by the
// distance squared, so the number pushed to it is this multiplied by that
// distance squared - which makes `brightness` mean "full brightness at the
// middle of the model, facing the light square on".
constexpr float brightness = 1.0f;

// The ImGui panel's text, as a fraction of the window's height, and the height
// of ImGui's own font in the pixels it was built for.  A PS5 window is 2160
// lines tall and a desktop one 720, and the panel should be the same size on
// the screen in both - which is a fraction of the height, not a pixel count.
constexpr float text_height_fraction = 0.030f;
constexpr float imgui_font_height    = 13.0f;

// A joint matrix, turned into the three rows the shader is sent.  Mat4 holds
// its sixteen numbers by column, so taking one number from each column gives
// one row.
JointRows rows_of(const Mat4& m)
{
  JointRows out{};
  for (int r = 0; r < 3; ++r)
  {
    for (int c = 0; c < 4; ++c)
    {
      out.row[r][c] = m.m[c * 4 + r];
    }
  }
  return out;
}

} // namespace

Game::Game(const Window& window, const GpuDevice& device,
           const GpuPipeline& pipeline, const GpuBuffer& vertices,
           const GpuBuffer& indices, std::span<SDL_GPUTexture* const> images,
           const GpuSampler& sampler, const GpuDepthTarget& depth,
           const Gui& gui, GltfModel& model)
  : window_{window.sdl()}, device_{device.sdl()}, pipeline_{pipeline.sdl()},
    vertices_{vertices.sdl()}, indices_{indices.sdl()}, images_{images},
    sampler_{sampler.sdl()}, depth_{depth.sdl()}, model_{model}
{
  // The window cannot be resized, so its shape is measured once, here, and
  // becomes the camera's aspect ratio.  It is the one part of a camera that is
  // not the camera's own business: it belongs to the window.
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(window_, &w, &h);
  camera_.aspect = static_cast<float>(w) / static_cast<float>(h);
  camera_.fov    = field_of_view;

  // ImGui's text and the spacing around it, scaled from the window's height so
  // that the panel is readable on a television as well as on a monitor.
  const float text_height = h * text_height_fraction;
  const float scale       = text_height / imgui_font_height;

  gui.scale_text(scale);
  ImGui::GetStyle().ScaleAllSizes(scale); // in place, so exactly once
  panel_pad_ = text_height;

  // Where to sit, worked out rather than chosen.  A ball of radius r fills a
  // field of view of angle a from r / sin(a / 2) away, so that distance frames
  // the model whatever size it turns out to be; the margin leaves a little
  // room around it, for the corners the ball does not reach.
  const GltfModel::Sphere& ball = model_.bounds();

  radius_ = ball.radius;
  const float distance = margin * radius_ / std::sin(field_of_view / 2.0f);

  // The middle of the model, with its z negated as every vertex's is in the
  // vertex shader: the camera has to stand in the same space as the thing it
  // is looking at, and that is the left-handed one.
  const glm::vec3 middle{ball.x, ball.y, -ball.z};

  // A three-quarter view from a little above, which is how a character is
  // usually first shown.  look_at wants a place to stand rather than two
  // angles, so the angles are turned into one here and then forgotten:
  // zod::Camera keeps a position and a target, and nothing else.
  constexpr float yaw = 0.6f, pitch = 0.3f;
  const glm::vec3 towards{ std::sin(yaw) * std::cos(pitch),
                           std::sin(pitch),
                          -std::cos(yaw) * std::cos(pitch)};

  camera_.look_at(middle + distance * towards, middle);

  // The light, put where light_offset says and left there.  Both lines are in
  // the same space as the camera above - the left-handed one, with the model's
  // z already negated - because a light has to stand in the space of the
  // surfaces it lights.
  const glm::vec3 offset = radius_ * light_offset;

  lights_[0].position = middle + offset;

  // Bright enough to deliver `brightness` at the middle of the model, given
  // the 1/d² the fragment shader applies to it.  dot(offset, offset) is the
  // distance squared, which is the one the shader divides by.
  lights_[0].intensity = brightness * glm::dot(offset, offset);
}

void Game::run()
{
  bool running = true;

  while (running)
  {
    process_events(running);
    update();
    interface();
    render();
  }
}

// What the player did.  Every event goes to ImGui as well, which is how it
// learns about the mouse and the window.
void Game::process_events(bool& running)
{
  pads_.begin_events(); // last frame's presses are old news

  SDL_Event e;
  while (SDL_PollEvent(&e))
  {
    pads_.handle(e); // keep every controller open
    ImGui_ImplSDL3_ProcessEvent(&e);

    // Circle, Escape, or the window's close button.
    if (is_quit(e, SDL_GAMEPAD_BUTTON_EAST))
    {
      running = false;
    }
  }
}

// What has changed since the last frame.  Each control is a rate multiplied by
// the time since the last frame, so the camera moves at the same speed however
// fast the program runs.
void Game::update()
{
  const Uint64 now = SDL_GetTicks();
  const float  dt  = (now - last_tick_) / 1000.0f;
  last_tick_       = now;

  // The right stick swings the camera around the model.  Pushing right takes
  // the camera to the right, so the model appears to turn to the left under
  // it; pushing away from you raises the camera.  Camera::orbit keeps the
  // target still and the distance to it unchanged, and stops short of directly
  // overhead, where there would be no telling which way is up (gimbal lock).
  const glm::vec2 right = pads_.stick(Stick::Right);
  if (right.x != 0.0f || right.y != 0.0f)
  {
    camera_.orbit(-right.x * turn_rate * dt, -right.y * turn_rate * dt);
  }

  // The clip planes follow the camera, so that the depth buffer spends its
  // precision on the model rather than on the empty space around it.  Three
  // radii of room either side is enough for any pose; the near plane never
  // reaches zero, where a perspective projection has nothing left to say.
  const float distance = camera_.distance();
  camera_.near_plane   = std::max(distance - 3.0f * radius_, radius_ * 0.05f);
  camera_.far_plane    = distance + 3.0f * radius_;

  time_ += dt; // GltfModel::pose wraps this round the animation's length
}

// The interface, worked out before anything is drawn: ImGui's three NewFrames,
// then the widgets, then Render, which turns them into triangles.  Those stay
// valid until the next NewFrame, which is why this can happen here and the
// drawing of them later.
void Game::interface()
{
  ImGui_ImplSDLGPU3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  ImGui::SetNextWindowPos(ImVec2(panel_pad_, panel_pad_), ImGuiCond_Always);
  ImGui::Begin("Model", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
               ImGuiWindowFlags_AlwaysAutoResize |
               ImGuiWindowFlags_NoSavedSettings);

  ImGui::Text("Nothing to report yet.");

  ImGui::End();

  ImGui::Render();
}

// What the screen should now show.
void Game::render()
{
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
  if (!cmd)
  {
    return;
  }

  SDL_GPUTexture* swapchain = nullptr;
  if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &swapchain,
                                             nullptr, nullptr))
  {
    SDL_SubmitGPUCommandBuffer(cmd);
    return;
  }

  if (swapchain) // null when the window is minimised: nothing to draw into
  {
    ImDrawData* panel = ImGui::GetDrawData();

    // This has to happen before any render pass begins, and the backend will
    // draw nothing without it: it is where ImGui's vertices are uploaded.
    ImGui_ImplSDLGPU3_PrepareDrawData(panel, cmd);

    SDL_GPUColorTargetInfo colour{};
    colour.texture     = swapchain;
    colour.clear_color = SDL_FColor{0.12f, 0.12f, 0.14f, 1.0f}; // near-black
    colour.load_op     = SDL_GPU_LOADOP_CLEAR;
    colour.store_op    = SDL_GPU_STOREOP_STORE;

    // The depth buffer is cleared to the far plane every frame, so that the
    // first thing drawn at a pixel is nearer than "nothing".  Its results are
    // not wanted after the pass, which is what DONT_CARE means.
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture     = depth_;
    depth.clear_depth = far_away;
    depth.load_op     = SDL_GPU_LOADOP_CLEAR;
    depth.store_op    = SDL_GPU_STOREOP_DONT_CARE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colour, 1, &depth);
    SDL_BindGPUGraphicsPipeline(pass, pipeline_);

    SDL_GPUBufferBinding binding{};
    binding.buffer = vertices_;
    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);

    // The corners are drawn in the order the index buffer gives, rather than
    // straight through.  Thirty-two bits per index: glTF files use bytes or
    // shorts where they can, and GltfModel widens them all to one type so that
    // this call never has to ask which.
    SDL_GPUBufferBinding index_binding{};
    index_binding.buffer = indices_;
    SDL_BindGPUIndexBuffer(pass, &index_binding,
                           SDL_GPU_INDEXELEMENTSIZE_32BIT);

    // The camera, and the pose the animation has reached.  The joint matrices
    // are worked out on the CPU because a joint's position depends on its
    // parent's, and a vertex shader cannot walk a skeleton.  All of it is
    // pushed once: every part of the model is in the same pose, seen by the
    // same camera.
    Transform transform{};
    transform.view_projection = camera_.view_projection();

    const std::span<const Mat4> pose = model_.pose(animation_, time_);
    for (std::size_t j = 0; j < pose.size(); ++j)
    {
      transform.joints[j] = rows_of(pose[j]);
    }

    SDL_PushGPUVertexUniformData(cmd, 0, &transform, sizeof(transform));

    // ...and the lights, to the other stage.  Vertex and fragment uniforms are
    // pushed by different calls into slots of their own, which is why both can
    // be slot 0.  shining() rather than the light itself: a light that has
    // been switched off is handed over with no intensity, so the shader has
    // nothing to ask about and no branch to take.
    PointLight shining[light_count];
    for (int i = 0; i < light_count; ++i)
    {
      shining[i] = lights_[i].shining();
    }

    SDL_PushGPUFragmentUniformData(cmd, 0, shining, sizeof(shining));

    // One draw per part, with the part's own image bound first.  first_index
    // is where in the index buffer this part's triangles are, and base_vertex
    // what its index 0 counts from. This is what lets every mesh share one
    // pair of buffers without renumbering.
    for (const GltfModel::Part& part : model_.parts())
    {
      SDL_GPUTextureSamplerBinding texture{};
      texture.texture = images_[part.image];
      texture.sampler = sampler_;
      SDL_BindGPUFragmentSamplers(pass, 0, &texture, 1);

      SDL_DrawGPUIndexedPrimitives(pass, part.index_count, 1, part.first_index,
                                   part.base_vertex, 0);
    }

    SDL_EndGPURenderPass(pass);

    // The panel goes on top, in a second pass with no depth buffer.  ImGui's
    // pipeline is built without one - it has no use for depth - and a pipeline
    // may only be used in a pass whose targets match it.  LOAD rather than
    // CLEAR keeps what the first pass drew.
    SDL_GPUColorTargetInfo over{};
    over.texture  = swapchain;
    over.load_op  = SDL_GPU_LOADOP_LOAD;
    over.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* panel_pass =
      SDL_BeginGPURenderPass(cmd, &over, 1, nullptr);

    ImGui_ImplSDLGPU3_RenderDrawData(panel, cmd, panel_pass);

    SDL_EndGPURenderPass(panel_pass);
  }

  SDL_SubmitGPUCommandBuffer(cmd);
}
