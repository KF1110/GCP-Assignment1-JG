// 3D Model Interaction on the PlayStation 5
//
// A glTF model, animated, lit, and seen from a camera that can be swung around
// it with the right stick. Everything it draws is read from a file: the
// meshes, the skeleton, the animations and the images that colour it, none of
// which appears anywhere in this program.
//
// Right stick: swing the camera around the model, and raise or lower it.
// Circle, or Escape, or closing the window: stop.
//
// This file makes things; game.cpp does things with them. Everything here is
// built in the order it has to be destroyed in reverse (a buffer cannot
// outlive the device that made it) and Zod's classes arrange that by being
// locals of main: the last one declared is the first one released.
//
// Each create() hands back a Result: the thing, or the reason there is no
// thing. Every create() result is checked; otherwise, a GPU program that
// carries on after a failed create() doesn't crash where the mistake was; it
// crashes three steps later, in code that is perfectly correct. Hard to debug.
//
// The resources it loads are the model under resources/models, and the shaders
// in shaders/, compiled to SPIR-V by CMake before the program runs.

#include "game.hpp"

#include "sdl3-uws/sdl3-context.hpp"     // SDLContext
#include "sdl3-uws/sdl3-expected.hpp"    // Result
#include "sdl3-uws/sdl3-texture-bank.hpp"// GpuTextureBank
#include <cstddef>                       // offsetof
#include <iostream>                      // std::cerr, std::cout
#include <vector>                        // std::vector

using namespace zod;

namespace
{

// The model this program draws.  A glTF file names its own buffer and images,
// relative to the directory it sits in, so moving it means moving all of them.
constexpr const char* model_path =
  RESOURCE_DIR "/models/raphael/Hellbrush.gltf";

// The window, in the shape a PlayStation 5 uses.  On a PS5 the system ignores
// this and gives every window the whole screen; on a desktop it is what you
// get.
constexpr int window_width  = 1280;
constexpr int window_height = 720;

// --- the pieces main asks for, each in a function of its own ---------------
//
// Several of Zod's create() calls want an SDL structure filled in first, and
// filling one in takes a dozen lines that say nothing about what this program
// is.  Each one is put in a function here, so that main below reads as the
// list of things that have to exist, in the order they have to exist in.
//
// Each of these returns a Result, and returns it by value from the create()
// that made it.  That is allowed even though the objects cannot be copied or
// moved: the compiler is required to build the returned object directly where
// the caller is putting it.

// The sampler: how the model's images are read when a triangle is bigger or
// smaller on the screen than the image is in pixels.  LINEAR blends between
// neighbouring pixels of the image; CLAMP_TO_EDGE stops a coordinate just
// outside the image wrapping round to the other side of it.
Result<GpuSampler> create_sampler(const GpuDevice& device)
{
  SDL_GPUSamplerCreateInfo info{};
  info.min_filter     = SDL_GPU_FILTER_LINEAR;
  info.mag_filter     = SDL_GPU_FILTER_LINEAR;
  info.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

  return GpuSampler::create(device, info);
}

// Somewhere to keep how far away each pixel's nearest triangle is: one value
// per pixel, so it is the size of the window.  A character is not convex - a
// near arm can be in front of its chest - so back-face culling alone cannot
// decide what is hidden, and this is what does.
Result<GpuDepthTarget> create_depth_target(const GpuDevice& device,
                                           const Window& window)
{
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(window.sdl(), &w, &h);

  return GpuDepthTarget::create(device, w, h);
}

// The pipeline: the whole of the fixed state a draw call needs, settled once
// here rather than at every draw.  The shapes it describes, the attributes and
// the colour target, are locals; and the create info only points at them, so
// everything it points at has to live until the create() at the end.  That's
// the reason this is one function rather than several.
Result<GpuPipeline> create_pipeline(const GpuDevice& device,
                                    const Window& window,
                                    const GpuShader& vertex_shader,
                                    const GpuShader& fragment_shader,
                                    const GpuDepthTarget& depth)
{
  // A vertex is fifteen floats: where it is, which way its surface faces,
  // where it is in the image, which four joints move it, and how much each of
  // them has a say.  The locations match the `layout(location = ...)` lines at
  // the top of shaders/model.vert.
  SDL_GPUVertexBufferDescription buffer{};
  buffer.slot       = 0;
  buffer.pitch      = sizeof(GltfModel::Vertex);
  buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

  SDL_GPUVertexAttribute attributes[5]{};
  attributes[0].location = 0;
  attributes[0].format   = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
  attributes[0].offset   = offsetof(GltfModel::Vertex, x);
  attributes[1].location = 1;
  attributes[1].format   = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
  attributes[1].offset   = offsetof(GltfModel::Vertex, nx);
  attributes[2].location = 2;
  attributes[2].format   = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
  attributes[2].offset   = offsetof(GltfModel::Vertex, u);
  attributes[3].location = 3;
  attributes[3].format   = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
  attributes[3].offset   = offsetof(GltfModel::Vertex, joints);
  attributes[4].location = 4;
  attributes[4].format   = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
  attributes[4].offset   = offsetof(GltfModel::Vertex, weights);

  // One colour target, whose format must match the window's swapchain
  SDL_GPUColorTargetDescription colour{};
  colour.format = SDL_GetGPUSwapchainTextureFormat(device.sdl(), window.sdl());

  SDL_GPUGraphicsPipelineCreateInfo info{};
  info.vertex_shader   = vertex_shader.sdl();
  info.fragment_shader = fragment_shader.sdl();
  info.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

  info.target_info.num_color_targets         = 1;
  info.target_info.color_target_descriptions = &colour;
  info.target_info.has_depth_stencil_target  = true;
  info.target_info.depth_stencil_format      = depth.format();

  info.vertex_input_state.vertex_buffer_descriptions = &buffer;
  info.vertex_input_state.num_vertex_buffers         = 1;
  info.vertex_input_state.vertex_attributes          = attributes;
  info.vertex_input_state.num_vertex_attributes      = 5;

  // Keep a fragment only if it's nearer than what is already there, and record
  // its depth when it is. That pair is the whole of hidden surface removal.
  info.depth_stencil_state.enable_depth_test  = true;
  info.depth_stencil_state.enable_depth_write = true;
  info.depth_stencil_state.compare_op         = SDL_GPU_COMPAREOP_LESS;

  // A glTF's triangles are wound anticlockwise seen from outside, as the
  // format requires, so the ones that come out clockwise on screen are facing
  // away and need not be drawn at all.
  info.rasterizer_state.cull_mode  = SDL_GPU_CULLMODE_BACK;
  info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

  return GpuPipeline::create(device, info);
}

// Every image the model is coloured with, uploaded in turn, and its handle
// kept so that a part can be drawn with the one it names.
//
// This asks the bank rather than GpuTexture::create because of the loop: a
// GpuTexture cannot be copied or moved, so a run of them cannot go in a
// vector - but the handles can, since what owns the textures is the bank.
Result<std::vector<SDL_GPUTexture*>> upload_images(GpuTextureBank& bank,
                                                   const GltfModel& model)
{
  std::vector<SDL_GPUTexture*> images;

  for (const GltfModel::Image& image : model.images())
  {
    auto texture = bank.load(image.name, image.bytes);
    if (!texture)
    {
      return fail(texture.error());
    }
    images.push_back(*texture);
  }

  return images;
}

// What the file turned out to hold, printed once at startup.  Useful when a
// model does not look the way it should, and the numbers the ImGui panel is
// asked for are all in here.
void describe(const GltfModel& model)
{
  std::cout << "vertices: "   << model.vertices().size()    << '\n'
            << "triangles: "  << model.indices().size() / 3 << '\n'
            << "parts: "      << model.parts().size()       << '\n'
            << "images: "     << model.images().size()      << '\n'
            << "joints: "     << model.joint_count()        << '\n'
            << "animations: " << model.animation_count()    << " (";

  for (std::size_t a = 0; a < model.animation_count(); ++a)
  {
    std::cout << (a ? ", " : "") << model.animation_name(a);
  }

  std::cout << ')' << std::endl;
}

} // namespace

int main()
{
  // SDL itself. Audio is asked for here as well as video and the pad: a
  // device cannot be opened for a subsystem that was never started.
  auto sdl = SDLContext::create(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD |
                                SDL_INIT_AUDIO);
  if (!sdl)
  {
    std::cerr << sdl.error() << std::endl;
    return -1;
  }

  auto window = Window::create(*sdl, "3D Model Interaction", window_width,
                               window_height);
  if (!window)
  {
    std::cerr << window.error() << std::endl;
    return -2;
  }

  auto device = GpuDevice::create(*window);
  if (!device)
  {
    std::cerr << device.error() << std::endl;
    return -3;
  }
  std::cout << "GPU backend: " << SDL_GetGPUDeviceDriver(device->sdl())
            << std::endl;

  // Dear ImGui, started after the window and the device it draws with, and so
  // shut down before them.
  auto gui = Gui::create(*window, *device);
  if (!gui)
  {
    std::cerr << gui.error() << std::endl;
    return -4;
  }

  // The model. This is the only step that knows what a glTF file is; from
  // here on it's just vertices, images and a list of animations.
  auto model = GltfModel::create(model_path);
  if (!model)
  {
    std::cerr << model.error() << std::endl;
    return -5;
  }
  if (model->joint_count() > shader_joint_count)
  {
    std::cerr << "the model has " << model->joint_count()
              << " joints, and the shader has room for " << shader_joint_count
              << std::endl;
    return -6;
  }
  describe(*model);

  auto vertex_shader = GpuShader::create(*device,
                                         SHADER_DIR "/model.vert.spv",
                                         SDL_GPU_SHADERSTAGE_VERTEX,
                                         ShaderResources{.uniform_buffers = 1});
  if (!vertex_shader)
  {
    std::cerr << vertex_shader.error() << std::endl;
    return -7;
  }

  // The skeleton is written out twice - as joints[3 * 84] in the GLSL, and as
  // Transform::joints in game.hpp; and nothing makes the two agree.  A
  // disagreement is quiet: the pose would be read from the wrong place and the
  // model drawn in a heap.  So the shader is asked how big its array is, by
  // reading its SPIR-V, and the answer is checked against what is pushed.
  if (vertex_shader->uniform_array_bytes() != sizeof(Transform::joints))
  {
    std::cerr << "model.vert has room for "
              << vertex_shader->uniform_array_bytes() << " bytes of joints, "
              << "and this program pushes " << sizeof(Transform::joints)
              << ": the two have to agree" << std::endl;
    return -8;
  }

  // The fragment shader reads two things: the image it samples, and the block
  // of lights it is pushed.
  auto fragment_shader =
    GpuShader::create(*device, SHADER_DIR "/model.frag.spv",
                      SDL_GPU_SHADERSTAGE_FRAGMENT,
                      ShaderResources{.samplers = 1, .uniform_buffers = 1});
  if (!fragment_shader)
  {
    std::cerr << fragment_shader.error() << std::endl;
    return -9;
  }

  // Made before the pipeline, because the pipeline has to be told its format,
  // and asking the target for it is one way the two cannot disagree.
  auto depth = create_depth_target(*device, *window);
  if (!depth)
  {
    std::cerr << depth.error() << std::endl;
    return -10;
  }

  auto pipeline = create_pipeline(*device, *window, *vertex_shader,
                                  *fragment_shader, *depth);
  if (!pipeline)
  {
    std::cerr << pipeline.error() << std::endl;
    return -11;
  }

  // One buffer for every mesh in the file, and one index buffer beside it.
  // What tells the parts apart is not where their bytes are but what each draw
  // call asks for.
  auto vertices = GpuBuffer::create(*device, model->vertices());
  if (!vertices)
  {
    std::cerr << vertices.error() << std::endl;
    return -12;
  }

  // The same call with a different usage: this buffer is read as indices
  // rather than as vertices, and the GPU is told which when it's bound.
  auto indices = GpuBuffer::create(*device, model->indices(),
                                   SDL_GPU_BUFFERUSAGE_INDEX);
  if (!indices)
  {
    std::cerr << indices.error() << std::endl;
    return -13;
  }

  auto bank = GpuTextureBank::create(*device);
  if (!bank)
  {
    std::cerr << bank.error() << std::endl;
    return -14;
  }

  auto images = upload_images(*bank, *model);
  if (!images)
  {
    std::cerr << images.error() << std::endl;
    return -15;
  }

  auto sampler = create_sampler(*device);
  if (!sampler)
  {
    std::cerr << sampler.error() << std::endl;
    return -16;
  }

  Game game{*window,  *device, *pipeline, *vertices, *indices,
            *images,  *sampler, *depth,   *gui,      *model};
  game.run();

  return 0;
}
