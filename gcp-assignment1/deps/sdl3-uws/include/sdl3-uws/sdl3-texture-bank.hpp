#ifndef _SDL3_TEXTURE_BANK_HPP_
#define _SDL3_TEXTURE_BANK_HPP_

#include "sdl3-expected.hpp" // Result, fail, sdl_fail, ns::in_place
#include "sdl3-renderer.hpp" // Renderer
#include "sdl3-typedefs.hpp" // SurfacePtr, TexturePtr
#include <SDL3/SDL.h>        // SDL_LoadPNG, SDL_CreateTextureFromSurface
#include <filesystem>        // std::filesystem::path
#include <map>               // std::map
#include <utility>           // std::move

namespace zod
{

// Where the images live.  SDL destroys a renderer's textures along with the
// renderer, so a bank declared after its Renderer - and therefore destroyed
// before it - keeps them in the order SDL needs, with no code to say so.
//
// A texture is loaded once: asking for the same file twice is the same
// texture.  Nothing is ever taken back out, so a texture handed out by
// Texture::create is good until the bank goes.
class TextureBank
{
  struct Key {};   // private tag: only TextureBank can name it

public:
  static Result<TextureBank> create(Renderer& renderer)
  {
    return Result<TextureBank>(ns::in_place, Key{}, renderer.sdl());
  }

  TextureBank(Key, SDL_Renderer* renderer) : renderer_{renderer} { }
  TextureBank(const TextureBank&) = delete;
  TextureBank& operator=(const TextureBank&) = delete;

  // Texture::create is how this is used; it wraps what comes back
  Result<SDL_Texture*> load(const std::filesystem::path& png_path,
                            SDL_ScaleMode                mode)
  {
    // One spelling per file, so "images/../images/knight.png" is not a second
    const std::filesystem::path path = png_path.lexically_normal();

    const auto loaded = textures_.find(path);
    if (loaded != textures_.end())
    {
      SDL_Texture* texture = loaded->second.get();

      // Handing back a texture that is already scaled the other way would be
      // a surprise, so say so rather than quietly ignoring the argument
      SDL_ScaleMode loaded_mode = SDL_SCALEMODE_LINEAR;
      SDL_GetTextureScaleMode(texture, &loaded_mode);
      if (loaded_mode != mode)
      {
        return fail("Already loaded with another scale mode: " + path.string());
      }

      return texture;
    }

    // The surface is only scaffolding: it is gone by the time this returns
    SurfacePtr surface{SDL_LoadPNG(path.string().c_str())};
    if (!surface)
    {
      return sdl_fail("SDL_LoadPNG: " + path.string());
    }

    TexturePtr texture{SDL_CreateTextureFromSurface(renderer_, surface.get())};
    if (!texture)
    {
      return sdl_fail("SDL_CreateTextureFromSurface: " + path.string());
    }

    if (!SDL_SetTextureScaleMode(texture.get(), mode))
    {
      return sdl_fail("SDL_SetTextureScaleMode: " + path.string());
    }

    const auto added = textures_.emplace(path, std::move(texture));

    return added.first->second.get();
  }

  // How many images have been loaded so far
  std::size_t size() const { return textures_.size(); }

private:
  SDL_Renderer* renderer_;
  // Keyed by the file it was loaded from, so asking for that file again finds
  // the texture already made.  A game needs a handful of images, not
  // thousands, and a few string comparisons beat hashing a whole path.
  std::map<std::filesystem::path, TexturePtr> textures_;
};

} // namespace zod

#endif // _SDL3_TEXTURE_BANK_HPP_
