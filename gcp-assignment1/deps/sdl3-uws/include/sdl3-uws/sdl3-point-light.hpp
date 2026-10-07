#ifndef _SDL3_POINT_LIGHT_HPP_
#define _SDL3_POINT_LIGHT_HPP_

#include <glm/glm.hpp> // glm::vec3

namespace zod
{

// A light that sits somewhere and shines in every direction - a bare bulb.
//
// A light like this is often carried as four loose floats, with a comment
// beside them explaining why the fourth is there.  Once a scene has more than
// one of them that will not do, so here it is once:
//
//   position   where the bulb hangs, in world space
//   intensity  how bright it is at one unit away
//   colour     what colour it shines, each channel from 0 to 1
//   enabled_   whether it is switched on
//
// The one surprise is `intensity` sitting between the two vectors rather than
// after them, and a bool at the end.  That is not tidiness, it is *layout*:
// this struct is meant to be pushed to a shader as it stands, and a shader
// reads a uniform block under rules of its own.  GLSL's std140 and HLSL's
// cbuffers both lay a block out in sixteen-byte rows, and both start a vec3
// on a fresh row rather than letting it straddle two.  So a vec3 is followed
// by four bytes of *something* whatever this struct says, and the choice is
// only whether that something carries something worth having.  Here it does:
// each light is two rows, position and intensity in the first, colour and the
// switch in the second, and the shader's
//
//   struct PointLight { vec3 position; float intensity; vec3 colour; float unused; };
//
// matches it byte for byte - which is what lets an array of these be pushed
// in one call and read as an array there.  A shader has no use for `enabled_`
// and never reads that last float: switching a light off is done by handing
// the shader no intensity, which is what shining() below is for.
//
// Brightness is "at one unit away" because light from a point thins as it
// spreads: twice as far, four times the area, a quarter as much caught.  The
// shader divides by the distance squared, so intensity is not a number near
// 1 - a light three units from what it lights has nine tenths of itself
// spent on getting there.
//
// Like zod::Camera, this owns nothing - no device, no memory - so it is not
// built like the rest of Zod.  There is no create() and no Result: it is a
// plain struct of numbers that a program declares, fills in, and moves about
// as it pleases.  Being an aggregate it can be written out in one go, or by
// naming only the fields that matter: PointLight{.intensity = 6.0f}.
struct PointLight
{
  glm::vec3 position{0.0f, 0.0f, 0.0f}; // where it is, in world space
  float     intensity = 4.0f;           // how bright, one unit away

  glm::vec3 colour{1.0f, 1.0f, 1.0f};   // what colour it shines: white
  bool      enabled_ = true;            // or is it switched off?

  // What a shader should be given.  A light that is off is handed over with no
  // intensity at all, and that is the whole of what "off" means: a shader is
  // then spared asking, and every light is the same shape whether it is
  // shining or not - which matters when they arrive as an array and a branch
  // taken per light would have to be taken per pixel as well.
  //
  // Push this rather than the light itself, and `enabled_` means what it says
  // wherever a light is used.
  PointLight shining() const
  {
    PointLight out = *this;
    if (!enabled_)
    {
      out.intensity = 0.0f;
    }
    return out;
  }
};

// What the comment above is about: two sixteen-byte rows, with nothing the
// compiler had to add beyond the three bytes after the bool - which is why
// `enabled_` is last, where those three have nowhere else to be.  A shader is
// handed these bytes as they are, so if this ever fails the shader is reading
// something else.
static_assert(sizeof(PointLight) == 32);

} // namespace zod

#endif // _SDL3_POINT_LIGHT_HPP_
