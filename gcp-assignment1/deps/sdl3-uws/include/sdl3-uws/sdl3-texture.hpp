#ifndef _SDL3_TEXTURE_HPP_
#define _SDL3_TEXTURE_HPP_

#include "sdl3-expected.hpp"     // Result, fail, ns::in_place
#include "sdl3-texture-bank.hpp" // TextureBank
#include <SDL3/SDL.h>            // SDL_Texture
#include <filesystem>            // std::filesystem::path

namespace zod
{

// An image, loaded and ready to draw.  The TextureBank holds it - one copy of
// each file, for as long as the bank is alive - and this is the proof that it
// loaded, and the handle to draw it with.
class Texture
{
  struct Key {};   // private tag: only Texture can name it

public:
  static Result<Texture> create(TextureBank&                 bank,
                                const std::filesystem::path& png_path,
                                SDL_ScaleMode mode = SDL_SCALEMODE_LINEAR)
  {
    const auto texture = bank.load(png_path, mode);
    if (!texture)
    {
      return fail(texture.error());
    }

    return Result<Texture>(ns::in_place, Key{}, *texture);
  }

  Texture(Key, SDL_Texture* texture) : texture_{texture} { }
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;

  // The raw handle, for the SDL calls that want one
  SDL_Texture* sdl() const { return texture_; }

private:
  SDL_Texture* texture_; // Owned by the bank, which outlives this
};

} // namespace zod

#endif // _SDL3_TEXTURE_HPP_
