#ifndef _SDL3_GPU_HPP_
#define _SDL3_GPU_HPP_

#include "sdl3-expected.hpp" // Result, fail, sdl_fail, ns::in_place
#include "sdl3-typedefs.hpp" // SurfacePtr
#include "sdl3-window.hpp"   // Window
#include <SDL3/SDL.h> // SDL_CreateGPUDevice, SDL_CreateGPUGraphicsPipeline etc.
#include <algorithm>  // std::max
#include <cstddef>    // std::byte
#include <cstring>    // std::memcpy
#include <filesystem> // std::filesystem::path
#include <map>        // std::map
#include <memory>     // std::unique_ptr
#include <span>       // std::span, std::as_bytes
#include <string>     // std::string
#include <vector>     // std::vector

namespace zod
{

// RAII for the objects SDL_GPU ("SDL-GPU") hands out.
//
// Every class here is built the same way as Window, Renderer and Texture: a
// static create() returning a Result, no copying and no moving, a destructor
// that releases what it holds, and an sdl() accessor for the SDL calls that
// want the raw handle.
//
// Unlike SDL_Window or SDL_Renderer, a GPU object is owned by a device, and
// releasing it needs that device as well as the object.  So each class keeps
// a pointer to its device beside its own handle.  Order of destruction
// matters: every object must go before the device that made it, and asking
// create() for a GpuDevice is what arranges that.  A GpuBuffer cannot be
// declared before the GpuDevice it was made from, so it is destroyed first.
//
// Images are the exception, and work as they do for the 2D renderer: a
// GpuTextureBank owns them, one copy of each file, and a GpuTexture is a
// handle to one.  The bank is what a loader of models - or of anything else
// that names a list of images - loads into.

// Memory the CPU can map and write to, used to get data into GPU memory.  It
// is the one thing here that stays a unique_ptr: a transfer buffer is pure
// scaffolding, alive only inside the create() that needs it, and never seen
// by a program that uses this header.
struct ReleaseGPUTransferBuffer
{
  SDL_GPUDevice* device = nullptr;
  void operator()(SDL_GPUTransferBuffer* t) const
  {
    SDL_ReleaseGPUTransferBuffer(device, t);
  }
};

using GpuTransferBufferPtr =
    std::unique_ptr<SDL_GPUTransferBuffer, ReleaseGPUTransferBuffer>;

// A texture in GPU memory, held by the GpuTextureBank below.  Releasing one
// needs its device, so the deleter carries the device beside the handle.
struct ReleaseGPUTexture
{
  SDL_GPUDevice* device = nullptr;
  void operator()(SDL_GPUTexture* t) const
  {
    SDL_ReleaseGPUTexture(device, t);
  }
};

using GpuTexturePtr = std::unique_ptr<SDL_GPUTexture, ReleaseGPUTexture>;

// A device, together with its claim on a window.  The two belong together:
// the window must be claimed before it can be drawn to, handed back before
// the device is destroyed, and the device destroyed before the window.
//
// Asking for a Window is what fixes that order.  A GpuDevice cannot be
// declared before the Window it needs, so it is destroyed before it, and the
// destructor below hands the window back first.
class GpuDevice
{
  struct Key {};   // private tag: only GpuDevice can name it

  // Debug mode must be off on the PS5 for now.  SDL's debug-mode binding
  // checks read a GraphicsPipelineCommonHeader at the start of each backend's
  // pipeline, which SDL3-playstation's AGC backend lacks (it is fixed
  // upstream), so they misread it and assert on the first draw.
#ifdef __PROSPERO__
  static constexpr bool debug_default = false;
#else
  static constexpr bool debug_default = true;
#endif

public:
  // Asking for SPIR-V is what selects the backend: Vulkan on Windows and
  // Linux, AGC on the PS5.  A null name lets SDL choose among those that can
  // accept the format.
  static Result<GpuDevice> create(
    const Window& window, bool debug_mode = debug_default,
    SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV)
  {
    SDL_GPUDevice* device = SDL_CreateGPUDevice(formats, debug_mode, nullptr);
    if (!device)
    {
      return sdl_fail("SDL_CreateGPUDevice");
    }

    if (!SDL_ClaimWindowForGPUDevice(device, window.sdl()))
    {
      SDL_DestroyGPUDevice(device);
      return sdl_fail("SDL_ClaimWindowForGPUDevice");
    }

    return Result<GpuDevice>(ns::in_place, Key{}, device, window.sdl());
  }

  GpuDevice(Key, SDL_GPUDevice* device, SDL_Window* window)
    : device_{device}, window_{window}
  {
  }

  GpuDevice(const GpuDevice&)            = delete;
  GpuDevice& operator=(const GpuDevice&) = delete;

  ~GpuDevice()
  {
    SDL_ReleaseWindowFromGPUDevice(device_, window_);
    SDL_DestroyGPUDevice(device_);
  }

  // The raw handle, for the SDL calls that want one
  SDL_GPUDevice* sdl() const { return device_; }

private:
  SDL_GPUDevice* device_;
  SDL_Window*    window_;
};

// How many of each kind of resource a shader uses.  SDL_GPU must be told
// these when the shader is created, and they must agree with the GLSL.  All
// default to zero, which suits a shader with no uniforms or textures; one
// with a single uniform buffer would pass {.uniform_buffers = 1}.
struct ShaderResources
{
  Uint32 samplers         = 0;
  Uint32 storage_textures = 0;
  Uint32 storage_buffers  = 0;
  Uint32 uniform_buffers  = 0;
};

// One compiled shader stage, read from a file of SPIR-V.
class GpuShader
{
  struct Key {};   // private tag: only GpuShader can name it

public:
  static Result<GpuShader> create(const GpuDevice&             device,
                                  const std::filesystem::path& path,
                                  SDL_GPUShaderStage           stage,
                                  const ShaderResources&       resources = {})
  {
    const std::string name = path.string();

    size_t size = 0;
    void*  code = SDL_LoadFile(name.c_str(), &size);
    if (!code)
    {
      return sdl_fail("SDL_LoadFile: " + name);
    }

    SDL_GPUShaderCreateInfo info{};
    info.code                 = static_cast<const Uint8*>(code);
    info.code_size            = size;
    info.entrypoint           = "main";
    info.format               = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage                = stage;
    info.num_samplers         = resources.samplers;
    info.num_storage_textures = resources.storage_textures;
    info.num_storage_buffers  = resources.storage_buffers;
    info.num_uniform_buffers  = resources.uniform_buffers;

    // Read out of the SPIR-V before it is handed over, for the one thing the
    // C++ and the GLSL cannot agree on by themselves: see
    // uniform_array_bytes() below.
    const std::size_t array_bytes = uniform_array_bytes(
      std::span{static_cast<const Uint32*>(code), size / sizeof(Uint32)});

    SDL_GPUShader* shader = SDL_CreateGPUShader(device.sdl(), &info);
    SDL_free(code); // the device has taken its own copy by now

    if (!shader)
    {
      return sdl_fail("SDL_CreateGPUShader: " + name);
    }

    return Result<GpuShader>(ns::in_place, Key{}, device.sdl(), shader,
                             array_bytes);
  }

  GpuShader(Key, SDL_GPUDevice* device, SDL_GPUShader* shader,
            std::size_t array_bytes)
    : device_{device}, shader_{shader}, array_bytes_{array_bytes}
  {
  }

  GpuShader(const GpuShader&)            = delete;
  GpuShader& operator=(const GpuShader&) = delete;

  ~GpuShader() { SDL_ReleaseGPUShader(device_, shader_); }

  // The raw handle, for the SDL calls that want one
  SDL_GPUShader* sdl() const { return shader_; }

  // How many bytes the longest array in this shader takes, or zero if it
  // declares none.  Arrays of vec4 and of mat4 are what this recognises, as
  // those are what a uniform block of any size is made of.
  //
  // This is here for one reason.  A shader's uniform block is written out
  // twice - once in GLSL and once as the struct the C++ pushes - and nothing
  // checks that the two agree.  A matrix in the wrong place is loud, but an
  // array being the wrong *length* is quiet: the program pushes a pose of one
  // size, the shader reads one of another, and the picture is simply wrong.
  // Comparing this with the size the C++ declares turns that into a message at
  // startup.
  std::size_t uniform_array_bytes() const { return array_bytes_; }

private:
  // SPIR-V is a list of instructions, each one a word holding a length and an
  // opcode, followed by that many words less one.  Walking it is simple
  // enough to do here, and this walk need recognise only four types and a
  // constant: a 32-bit float, a vector of four of them, a matrix of four of
  // those, and an array of either - whose length is itself an id, naming the
  // constant that says how long it is.  The opcode numbers are from the
  // SPIR-V specification, section 3.37.
  //
  // The sizes are std140's, which is the layout a uniform block uses: a vec4
  // is sixteen bytes, and a mat4 is four columns of that.
  static std::size_t uniform_array_bytes(std::span<const Uint32> words)
  {
    constexpr Uint32 magic          = 0x07230203u; // the little-endian one
    constexpr Uint32 op_type_float  = 22;
    constexpr Uint32 op_type_vector = 23;
    constexpr Uint32 op_type_matrix = 24;
    constexpr Uint32 op_type_array  = 28;
    constexpr Uint32 op_constant    = 43;

    if (words.size() < 5 || words[0] != magic)
    {
      return 0; // not SPIR-V, or the wrong way round: nothing to say
    }

    // Every result in a module has an id below the bound the header gives, so
    // these four say what each id turned out to be, and nothing is hashed.
    const std::size_t   bound = words[3];
    std::vector<char>   is_float(bound, 0);
    std::vector<char>   is_vec4(bound, 0);
    std::vector<char>   is_mat4(bound, 0);
    std::vector<Uint32> value(bound, 0);

    const auto known = [bound](Uint32 id) { return id < bound; };

    std::size_t longest = 0;
    for (std::size_t i = 5; i < words.size();)
    {
      const Uint32 length = words[i] >> 16;
      const Uint32 opcode = words[i] & 0xffffu;
      if (length == 0 || i + length > words.size())
      {
        break; // malformed, and not worth a word: this is only a check
      }

      switch (opcode)
      {
        case op_type_float: // result id, width in bits
          if (length >= 3 && words[i + 2] == 32 && known(words[i + 1]))
          {
            is_float[words[i + 1]] = 1;
          }
          break;

        case op_type_vector: // result id, component type, how many
          if (length >= 4 && words[i + 3] == 4 && known(words[i + 1]) &&
              known(words[i + 2]) && is_float[words[i + 2]])
          {
            is_vec4[words[i + 1]] = 1;
          }
          break;

        case op_type_matrix: // result id, column type, how many columns
          if (length >= 4 && words[i + 3] == 4 && known(words[i + 1]) &&
              known(words[i + 2]) && is_vec4[words[i + 2]])
          {
            is_mat4[words[i + 1]] = 1;
          }
          break;

        case op_constant: // type, result id, the value itself
          if (length >= 4 && known(words[i + 2]))
          {
            value[words[i + 2]] = words[i + 3];
          }
          break;

        case op_type_array: // result id, element type, the length's id
          if (length >= 4 && known(words[i + 2]) && known(words[i + 3]) &&
              (is_vec4[words[i + 2]] || is_mat4[words[i + 2]]))
          {
            const std::size_t each = is_mat4[words[i + 2]] ? 64 : 16;
            longest = std::max<std::size_t>(longest, value[words[i + 3]] * each);
          }
          break;

        default:
          break;
      }

      i += length;
    }

    return longest;
  }

  SDL_GPUDevice* device_;
  SDL_GPUShader* shader_;
  std::size_t    array_bytes_;
};

// Everything the GPU needs to know to draw: which two shaders to run, how a
// vertex is laid out, what it is drawing into, and how to rasterise it.
class GpuPipeline
{
  struct Key {};   // private tag: only GpuPipeline can name it

public:
  // The create-info is filled in by the caller, and read only during this
  // call.  SDL copies the vertex buffer descriptions, vertex attributes and
  // colour target descriptions it points at, so all of those may be locals
  // that go out of scope the moment create() returns.
  //
  // A shader is named in the info by its raw handle, so both GpuShaders must
  // still be alive here; neither is needed afterwards, as the pipeline has
  // taken what it needs from them.
  static Result<GpuPipeline> create(
    const GpuDevice& device, const SDL_GPUGraphicsPipelineCreateInfo& info)
  {
    SDL_GPUGraphicsPipeline* pipeline =
      SDL_CreateGPUGraphicsPipeline(device.sdl(), &info);
    if (!pipeline)
    {
      return sdl_fail("SDL_CreateGPUGraphicsPipeline");
    }

    return Result<GpuPipeline>(ns::in_place, Key{}, device.sdl(), pipeline);
  }

  GpuPipeline(Key, SDL_GPUDevice* device, SDL_GPUGraphicsPipeline* pipeline)
    : device_{device}, pipeline_{pipeline}
  {
  }

  GpuPipeline(const GpuPipeline&)            = delete;
  GpuPipeline& operator=(const GpuPipeline&) = delete;

  ~GpuPipeline() { SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_); }

  // The raw handle, for the SDL calls that want one
  SDL_GPUGraphicsPipeline* sdl() const { return pipeline_; }

private:
  SDL_GPUDevice*           device_;
  SDL_GPUGraphicsPipeline* pipeline_;
};

// A run of bytes in GPU memory, which the CPU cannot write to directly.
//
// Vertices are the usual contents, and the default below says so, but the
// same SDL_GPUBuffer also holds the indices of an indexed draw, the arguments
// of an indirect one, or data a shader reads for itself, which is why neither
// SDL's name nor this one mentions vertices.
class GpuBuffer
{
  struct Key {};   // private tag: only GpuBuffer can name it

public:
  // Copy a contiguous range into new GPU memory.
  //
  // Data is any contiguous range of trivially copyable elements: a
  // std::array, a std::vector, a std::span.  The element type is never named
  // here, so a program's own Vertex needs no mention: one holding a position
  // and a colour, and one holding a position and a texture coordinate, both
  // arrive here as bytes.  What the bytes mean is settled elsewhere, by the
  // vertex attributes given to the pipeline.
  template <class Data>
  static Result<GpuBuffer> create(
    const GpuDevice& device, const Data& data,
    SDL_GPUBufferUsageFlags usage = SDL_GPU_BUFFERUSAGE_VERTEX)
  {
    return from_bytes(device, std::as_bytes(std::span{data}), usage);
  }

  GpuBuffer(Key, SDL_GPUDevice* device, SDL_GPUBuffer* buffer)
    : device_{device}, buffer_{buffer}
  {
  }

  GpuBuffer(const GpuBuffer&)            = delete;
  GpuBuffer& operator=(const GpuBuffer&) = delete;

  ~GpuBuffer() { SDL_ReleaseGPUBuffer(device_, buffer_); }

  // The raw handle, for the SDL calls that want one
  SDL_GPUBuffer* sdl() const { return buffer_; }

private:
  // The work, once the element type has been forgotten.  There is no RAII to
  // lean on part-way through: this object cannot be moved, so it is only
  // built once everything has succeeded, and the buffer is released by hand
  // on the paths that fail.
  static Result<GpuBuffer> from_bytes(const GpuDevice&           device,
                                      std::span<const std::byte> data,
                                      SDL_GPUBufferUsageFlags    usage)
  {
    const auto size = static_cast<Uint32>(data.size_bytes());

    // The destination, which only the GPU can reach
    SDL_GPUBufferCreateInfo buffer_info{};
    buffer_info.usage = usage;
    buffer_info.size  = size;

    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device.sdl(), &buffer_info);
    if (!buffer)
    {
      return sdl_fail("SDL_CreateGPUBuffer");
    }

    // The go-between, which the CPU can map into its own memory and write to
    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size  = size;

    GpuTransferBufferPtr transfer{
      SDL_CreateGPUTransferBuffer(device.sdl(), &transfer_info),
      ReleaseGPUTransferBuffer{device.sdl()}};
    if (!transfer)
    {
      SDL_ReleaseGPUBuffer(device.sdl(), buffer);
      return sdl_fail("SDL_CreateGPUTransferBuffer");
    }

    void* mapped = SDL_MapGPUTransferBuffer(device.sdl(), transfer.get(),
                                            false);
    if (!mapped)
    {
      SDL_ReleaseGPUBuffer(device.sdl(), buffer);
      return sdl_fail("SDL_MapGPUTransferBuffer");
    }
    std::memcpy(mapped, data.data(), size);
    SDL_UnmapGPUTransferBuffer(device.sdl(), transfer.get());

    // The copy itself is a GPU command, so it goes in a command buffer too
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device.sdl());
    if (!cmd)
    {
      SDL_ReleaseGPUBuffer(device.sdl(), buffer);
      return sdl_fail("SDL_AcquireGPUCommandBuffer");
    }

    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = transfer.get();
    source.offset          = 0;

    SDL_GPUBufferRegion destination{};
    destination.buffer = buffer;
    destination.offset = 0;
    destination.size   = size;

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUBuffer(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);

    if (!SDL_SubmitGPUCommandBuffer(cmd))
    {
      SDL_ReleaseGPUBuffer(device.sdl(), buffer);
      return sdl_fail("SDL_SubmitGPUCommandBuffer");
    }

    // The transfer buffer is released as this returns, possibly before the
    // GPU has done the copy.  That is safe: SDL waits until it is no longer
    // in use.
    return Result<GpuBuffer>(ns::in_place, Key{}, device.sdl(), buffer);
  }

  SDL_GPUDevice* device_;
  SDL_GPUBuffer* buffer_;
};

// Where the images in GPU memory live.  A GPU texture is owned by its device,
// and must be released before the device is destroyed; a bank declared after
// its GpuDevice - and so destroyed before it - arranges that for every
// texture at once, with no code to say so.
//
// An image is loaded once: asking for the same file - or the same name, for an
// image that came as bytes rather than as a file - is the same texture.
// Nothing is ever taken back out, so a texture handed out by GpuTexture is
// good until the bank goes.  This is the GPU twin of zod::TextureBank, and
// the place a model loader hands its list of images to.
class GpuTextureBank
{
  struct Key {};   // private tag: only GpuTextureBank can name it

public:
  static Result<GpuTextureBank> create(const GpuDevice& device)
  {
    return Result<GpuTextureBank>(ns::in_place, Key{}, device.sdl());
  }

  GpuTextureBank(Key, SDL_GPUDevice* device) : device_{device} { }
  GpuTextureBank(const GpuTextureBank&) = delete;
  GpuTextureBank& operator=(const GpuTextureBank&) = delete;

  // An image from a file.  GpuTexture::create is how this is used for a single
  // image; it wraps what comes back.
  //
  // The image is loaded into an SDL_Surface on the CPU, then uploaded into
  // GPU memory through a transfer buffer, in the same two steps a vertex
  // buffer takes: map and copy, then a copy pass.  Unlike the other classes
  // here nothing is handed an SDL create-info: the size and format are worked
  // out from the file.
  Result<SDL_GPUTexture*> load(const std::filesystem::path& png_path)
  {
    // One spelling per file, so "images/../images/stone.png" is not a second
    const std::filesystem::path path = png_path.lexically_normal();

    const auto found = textures_.find(path);
    if (found != textures_.end())
    {
      return found->second.get();
    }

    const std::string name = path.string();
    if (name.ends_with(".jpg") || name.ends_with(".jpeg"))
    {
      return fail(no_jpeg(name));
    }

    SurfacePtr loaded{SDL_LoadPNG(name.c_str())};
    if (!loaded)
    {
      return sdl_fail("SDL_LoadPNG: " + name);
    }

    return upload(path, std::move(loaded), name);
  }

  // An image from bytes already in memory, under a name of the caller's
  // choosing - which is what it is remembered by, so asking for that name
  // again gives the same texture.
  //
  // A model may carry its images inside itself rather than beside itself: a
  // .glb keeps them in its one buffer, and this is the overload a loader hands
  // those bytes to.  SDL_IOFromConstMem makes the bytes look like a file, so
  // the only difference from the overload above is where the PNG came from.
  Result<SDL_GPUTexture*> load(const std::string&         name,
                               std::span<const std::byte> png)
  {
    const std::filesystem::path path = name;

    const auto found = textures_.find(path);
    if (found != textures_.end())
    {
      return found->second.get();
    }

    // The first three bytes of a JPEG, which is not a thing SDL can decode.
    // There is no point handing these to SDL_LoadPNG to find that out.
    if (png.size() >= 3 && png[0] == std::byte{0xFF} &&
        png[1] == std::byte{0xD8} && png[2] == std::byte{0xFF})
    {
      return fail(no_jpeg(name));
    }

    SDL_IOStream* io = SDL_IOFromConstMem(png.data(), png.size_bytes());
    if (!io)
    {
      return sdl_fail("SDL_IOFromConstMem: " + name);
    }

    // true: the stream is closed by SDL on the way out, whether or not the
    // bytes turned out to be a PNG
    SurfacePtr loaded{SDL_LoadPNG_IO(io, true)};
    if (!loaded)
    {
      return sdl_fail("SDL_LoadPNG_IO: " + name);
    }

    return upload(path, std::move(loaded), name);
  }

  // How many images have been loaded so far
  std::size_t size() const { return textures_.size(); }

private:
  // SDL reads PNG and BMP, and no JPEG at all - SDL_LoadPNG and SDL_LoadBMP
  // are the whole of it - so a JPEG is said to be one, rather than left to
  // fail later as bytes that are not a PNG.
  static std::string no_jpeg(const std::string& name)
  {
    return name + " is a JPEG, and SDL reads only PNG and BMP: convert the "
                  "image to a PNG first";
  }

  // What is the same however the image arrived: the surface, converted to the
  // one format the GPU is told to expect, and copied into GPU memory.
  Result<SDL_GPUTexture*> upload(const std::filesystem::path& key,
                                 SurfacePtr loaded, const std::string& name)
  {
    // A PNG arrives in whatever format the file used, which may have no alpha.
    // The GPU is told below to expect four bytes per pixel in R, G, B, A
    // order, which is what SDL_PIXELFORMAT_RGBA32 means on any endianness, so
    // convert rather than assume.  SDL_ConvertSurface makes a new surface even
    // when the format already matches.
    SurfacePtr image{SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32)};
    if (!image)
    {
      return sdl_fail("SDL_ConvertSurface: " + name);
    }

    const auto   width  = static_cast<Uint32>(image->w);
    const auto   height = static_cast<Uint32>(image->h);
    const Uint32 size   = width * height * 4;

    // A texture is not a plain run of bytes like a buffer: the GPU is free to
    // store it however suits it, so it must be told the size, the pixel
    // format, and that a shader will sample it.  One mip level and one layer:
    // no mipmaps, and a single image rather than an array.
    SDL_GPUTextureCreateInfo texture_info{};
    texture_info.type                 = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture_info.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width                = width;
    texture_info.height               = height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels           = 1;

    GpuTexturePtr texture{SDL_CreateGPUTexture(device_, &texture_info),
                          ReleaseGPUTexture{device_}};
    if (!texture)
    {
      return sdl_fail("SDL_CreateGPUTexture: " + name);
    }

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size  = size;

    GpuTransferBufferPtr transfer{
      SDL_CreateGPUTransferBuffer(device_, &transfer_info),
      ReleaseGPUTransferBuffer{device_}};
    if (!transfer)
    {
      return sdl_fail("SDL_CreateGPUTransferBuffer");
    }

    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer.get(),
                                            false);
    if (!mapped)
    {
      return sdl_fail("SDL_MapGPUTransferBuffer");
    }

    // A surface's rows are pitch bytes apart, which is at least the width in
    // bytes but may be more, as a surface is free to pad each row.  The
    // transfer buffer wants them packed, so the copy is done a row at a time.
    auto*       out = static_cast<Uint8*>(mapped);
    const auto* in  = static_cast<const Uint8*>(image->pixels);
    for (Uint32 row = 0; row < height; ++row)
    {
      std::memcpy(out + row * width * 4, in + row * image->pitch, width * 4);
    }
    SDL_UnmapGPUTransferBuffer(device_, transfer.get());

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd)
    {
      return sdl_fail("SDL_AcquireGPUCommandBuffer");
    }

    // Where the bytes come from: packed rows of width pixels, height of them
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer.get();
    source.offset          = 0;
    source.pixels_per_row  = width;
    source.rows_per_layer  = height;

    // ...and where they go: the whole of mip level 0 of the only layer.  A
    // region can be a smaller rectangle, which is how a sprite sheet is
    // packed one sprite at a time.  d is the depth, 1 for a 2D texture.
    SDL_GPUTextureRegion destination{};
    destination.texture = texture.get();
    destination.w       = width;
    destination.h       = height;
    destination.d       = 1;

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);

    if (!SDL_SubmitGPUCommandBuffer(cmd))
    {
      return sdl_fail("SDL_SubmitGPUCommandBuffer");
    }

    const auto added = textures_.emplace(key, std::move(texture));

    // The surfaces and the transfer buffer are released as this returns, the
    // last of them possibly before the GPU has done the copy.  That is safe:
    // SDL waits until the transfer buffer is no longer in use.
    return added.first->second.get();
  }

  SDL_GPUDevice* device_;
  // Keyed by where it was loaded from, so asking for that file - or that name -
  // again finds the texture already uploaded.  A model needs a handful of
  // images, not thousands, and a few string comparisons beat hashing a whole
  // path.
  std::map<std::filesystem::path, GpuTexturePtr> textures_;
};

// An image in GPU memory, which a shader reads through a sampler.  The bank
// holds it - one copy of each file, for as long as the bank is alive - and
// this is the proof that it loaded, and the handle to draw with.
class GpuTexture
{
  struct Key {};   // private tag: only GpuTexture can name it

public:
  static Result<GpuTexture> create(GpuTextureBank&              bank,
                                   const std::filesystem::path& png_path)
  {
    const auto texture = bank.load(png_path);
    if (!texture)
    {
      return fail(texture.error());
    }

    return Result<GpuTexture>(ns::in_place, Key{}, *texture);
  }

  GpuTexture(Key, SDL_GPUTexture* texture) : texture_{texture} { }
  GpuTexture(const GpuTexture&)            = delete;
  GpuTexture& operator=(const GpuTexture&) = delete;

  // The raw handle, for the SDL calls that want one
  SDL_GPUTexture* sdl() const { return texture_; }

private:
  SDL_GPUTexture* texture_; // Owned by the bank, which outlives this
};

// Somewhere for the GPU to record how far away each pixel is, so that a nearer
// triangle drawn later can be told to keep out of the way of a further one
// drawn earlier.  One object needs no such thing - back-face culling hides the
// far side of anything convex - but two do, whichever order they are drawn in.
//
// It is a texture like any other, only never read by a shader: it is written
// by the depth test, which the pipeline switches on, and thrown away at the end
// of each frame.  Its size must match the window's, since there is one depth
// per pixel drawn.
class GpuDepthTarget
{
  struct Key {};   // private tag: only GpuDepthTarget can name it

public:
  static Result<GpuDepthTarget> create(
    const GpuDevice& device, int width, int height,
    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT)
  {
    // Not every GPU offers every depth format, so ask before asking for one.
    // D32_FLOAT is a float per pixel; D16_UNORM is half the memory and often
    // enough, and D24_UNORM_S8_UINT is what a program wanting a stencil buffer
    // would use.
    const auto usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    if (!SDL_GPUTextureSupportsFormat(device.sdl(), format,
                                      SDL_GPU_TEXTURETYPE_2D, usage))
    {
      return fail("This GPU has no such depth format");
    }

    SDL_GPUTextureCreateInfo info{};
    info.type                 = SDL_GPU_TEXTURETYPE_2D;
    info.format               = format;
    info.usage                = usage;
    info.width                = static_cast<Uint32>(width);
    info.height               = static_cast<Uint32>(height);
    info.layer_count_or_depth = 1;
    info.num_levels           = 1;

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device.sdl(), &info);
    if (!texture)
    {
      return sdl_fail("SDL_CreateGPUTexture (depth)");
    }

    return Result<GpuDepthTarget>(ns::in_place, Key{}, device.sdl(), texture,
                                  format);
  }

  GpuDepthTarget(Key, SDL_GPUDevice* device, SDL_GPUTexture* texture,
                 SDL_GPUTextureFormat format)
    : device_{device}, texture_{texture}, format_{format}
  {
  }

  GpuDepthTarget(const GpuDepthTarget&)            = delete;
  GpuDepthTarget& operator=(const GpuDepthTarget&) = delete;

  ~GpuDepthTarget() { SDL_ReleaseGPUTexture(device_, texture_); }

  // The raw handle, for the SDL calls that want one
  SDL_GPUTexture* sdl() const { return texture_; }

  // The pipeline has to be told which format it will be drawing into
  SDL_GPUTextureFormat format() const { return format_; }

private:
  SDL_GPUDevice*       device_;
  SDL_GPUTexture*      texture_;
  SDL_GPUTextureFormat format_;
};

// How a shader reads a texture: the filtering, and what happens outside 0 to
// 1.  It is a separate object from the texture it is used on, so one sampler
// can serve many textures, and one texture be read in several ways.
class GpuSampler
{
  struct Key {};   // private tag: only GpuSampler can name it

public:
  static Result<GpuSampler> create(const GpuDevice&                device,
                                   const SDL_GPUSamplerCreateInfo& info)
  {
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device.sdl(), &info);
    if (!sampler)
    {
      return sdl_fail("SDL_CreateGPUSampler");
    }

    return Result<GpuSampler>(ns::in_place, Key{}, device.sdl(), sampler);
  }

  GpuSampler(Key, SDL_GPUDevice* device, SDL_GPUSampler* sampler)
    : device_{device}, sampler_{sampler}
  {
  }

  GpuSampler(const GpuSampler&)            = delete;
  GpuSampler& operator=(const GpuSampler&) = delete;

  ~GpuSampler() { SDL_ReleaseGPUSampler(device_, sampler_); }

  // The raw handle, for the SDL calls that want one
  SDL_GPUSampler* sdl() const { return sampler_; }

private:
  SDL_GPUDevice*  device_;
  SDL_GPUSampler* sampler_;
};

} // namespace zod

#endif // _SDL3_GPU_HPP_
