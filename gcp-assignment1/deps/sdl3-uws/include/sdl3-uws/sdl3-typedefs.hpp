#ifndef _SDL3_TYPEDEFS_HPP_
#define _SDL3_TYPEDEFS_HPP_

#include <SDL3/SDL.h> // SDL_Init, SDL_CreateWindow etc.
#include <memory>     // std::unique_ptr

namespace zod
{

// Named deleters (not lambdas) give external linkage, avoiding -Wsubobject-linkage warnings.
//
// The window and renderer are no longer here: zod::Window and zod::Renderer
// own those, and pin them to a scope.  What is left is for resources that a
// scope-bound wrapper cannot express - one kept in a container rather than a
// scope (a texture, in the TextureBank's map of loaded images), one that is only
// scaffolding inside a create() (a surface), or one that is nullable and
// reseated while the program runs (a gamepad, unplugged and plugged back in).
struct DestroyTexture  { void operator()(SDL_Texture*  t) const { SDL_DestroyTexture(t);  } };
struct DestroySurface  { void operator()(SDL_Surface*  s) const { SDL_DestroySurface(s);  } };
struct CloseGamepad    { void operator()(SDL_Gamepad*  g) const { SDL_CloseGamepad(g);    } };

using TexturePtr  = std::unique_ptr<SDL_Texture,  DestroyTexture>;
using SurfacePtr  = std::unique_ptr<SDL_Surface,  DestroySurface>;
using GamepadPtr  = std::unique_ptr<SDL_Gamepad,  CloseGamepad>;

} // namespace zod

#endif // _SDL3_TYPEDEFS_HPP_
