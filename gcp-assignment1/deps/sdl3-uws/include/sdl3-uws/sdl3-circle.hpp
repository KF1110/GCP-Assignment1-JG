#ifndef _SDL3_CIRCLE_HPP_
#define _SDL3_CIRCLE_HPP_

#include <SDL3/SDL.h> // SDL_FPoint
#include <numbers>    // std::numbers::pi

namespace zod
{

// A circle, on either of SDL's drawing APIs, is a fan of thin triangles: each
// is made of the centre and two neighbouring points on the circumference.
// This header gives those points; how they are drawn is left to the caller,
// whether that is SDL_RenderGeometry on a 2D renderer or a vertex buffer on
// the GPU.

// constexpr std::cos & std::sin only appear in C++26. This one uses the Taylor
// series, 1 - x^2/2! + x^4/4! - ..., each term made from the one before. 20
// terms are accurate to ~1e-14 for x within 2*pi of zero: ideal for a circle.
constexpr double ccos(double x)
{
  double term = 1.0, sum = 1.0;
  for (int k = 1; k < 20; ++k)
  {
    term *= -x * x / ((2 * k - 1) * (2 * k));
    sum += term;
  }
  return sum;
}

// A sine wave is a cosine wave a quarter-turn later
constexpr double csin(double x) { return ccos(x - std::numbers::pi / 2); }

// The i-th of `segments` evenly spaced points around a circle.  Being
// constexpr, the same call serves a circle built while the program runs and
// one built while it compiles.  Asking for point `segments` gives point 0
// again, which is what closes the last triangle of the fan.
constexpr SDL_FPoint circle_point(SDL_FPoint centre, float radius, int i,
                                  int segments)
{
  const double angle = 2.0 * std::numbers::pi * i / segments;
  return {static_cast<float>(centre.x + radius * ccos(angle)),
          static_cast<float>(centre.y + radius * csin(angle))};
}

} // namespace zod

#endif // _SDL3_CIRCLE_HPP_
