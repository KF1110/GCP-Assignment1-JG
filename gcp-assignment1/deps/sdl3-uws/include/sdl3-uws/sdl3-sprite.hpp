#ifndef _SDL3_SPRITE_HPP_
#define _SDL3_SPRITE_HPP_

#include "sdl3-sprite-animator.hpp"
#include <string>

namespace zod
{

struct AABB
{
  float x, y, w, h;

  bool overlaps(const AABB& b) const
  {
    return x < b.x + b.w && x + w > b.x &&
           y < b.y + b.h && y + h > b.y;
  }

  // Returns the lower-centre foot region, suitable for 2D depth-sort collision.
  AABB foot() const
  {
    return {x + w * 0.3f, y + h * 0.625f, w * 0.4f, h * 0.25f};
  }
};

class Sprite
{
public:
  // vx and vy are velocities in pixels/second; positive vx moves right,
  // positive vy moves down.
  Sprite(const SpriteSheet& sheet, const std::string& tag_name, float x,
         float y, float vx = 0.0f, float vy = 0.0f, float scale = 1.0f)
    : x_{x}, y_{y}, vx_{vx}, vy_{vy}, scale_{scale}, animator_{sheet}
  {
    if (!tag_name.empty())
      animator_.play(tag_name);
  }

  void play(const std::string& tag_name)
  {
    animator_.play(tag_name);
  }

  const std::string& tag_name() const
  {
    return animator_.tag_name();
  }

  // Advance animation and position by dt_ms milliseconds.
  void update(float dt_ms)
  {
    animator_.update(dt_ms);
    float dt_sec = dt_ms / 1000.0f;
    x_ += vx_ * dt_sec;
    y_ += vy_ * dt_sec;
  }

  // Loop the sprite around the window edges.
  void wrap(float window_w, float window_h)
  {
    float fw = animator_.frame_width()  * scale_;
    float fh = animator_.frame_height() * scale_;
    if (vx_ > 0.0f && x_ > window_w)  x_ = -fw;
    else if (vx_ < 0.0f && x_ < -fw)  x_ = window_w;
    if (vy_ > 0.0f && y_ > window_h)  y_ = -fh;
    else if (vy_ < 0.0f && y_ < -fh)  y_ = window_h;
  }

  bool overlaps(const Sprite& other) const
  {
    return aabb().foot().overlaps(other.aabb().foot());
  }

  AABB aabb() const
  {
    return {x_, y_, animator_.frame_width()  * scale_,
                    animator_.frame_height() * scale_};
  }

  void render(SDL_Renderer* renderer) const
  {
    animator_.render(renderer, x_, y_, scale_, flip_h_);
  }

  float x_, y_, vx_, vy_, scale_;
  bool  flip_h_ = false;

private:
  SpriteAnimator animator_;
};

} // namespace zod

#endif // _SDL3_SPRITE_HPP_
