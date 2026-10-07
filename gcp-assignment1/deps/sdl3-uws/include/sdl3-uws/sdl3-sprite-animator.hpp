#ifndef _SDL3_SPRITE_ANIMATOR_HPP_
#define _SDL3_SPRITE_ANIMATOR_HPP_

#include "sdl3-sprite-sheet.hpp"

namespace zod
{

class SpriteAnimator
{
public:
  explicit SpriteAnimator(const SpriteSheet& sheet)
    : sheet_{&sheet}, current_frame_{0}, elapsed_{0.0f}, tag_{nullptr},
      ping_pong_forward_{true}
  {
  }

  void play(const std::string& tag_name)
  {
    const FrameTag* tag = sheet_->find_tag(tag_name);
    if (tag)
    {
      tag_ = tag;
      current_frame_ = tag->from;
      elapsed_ = 0.0f;
      ping_pong_forward_ = true;
    }
  }

  void update(float dt_ms)
  {
    if (!tag_ || tag_->from == tag_->to)
    {
      return;
    }

    elapsed_ += dt_ms;

    const Frame& frame = sheet_->get_frame(current_frame_);
    float        duration = static_cast<float>(frame.duration);

    while (elapsed_ >= duration)
    {
      elapsed_ -= duration;
      advance_frame();
    }
  }

  size_t current_frame() const
  {
    return current_frame_;
  }

  const std::string& tag_name() const
  {
    static const std::string empty;
    return tag_ ? tag_->name : empty;
  }

  float frame_width() const
  {
    return sheet_->get_frame(current_frame_).rect.w;
  }

  float frame_height() const
  {
    return sheet_->get_frame(current_frame_).rect.h;
  }

  void render(SDL_Renderer* renderer, float x, float y,
              float scale = 1.0f, bool flip_h = false) const
  {
    sheet_->render_frame(renderer, current_frame_, x, y, scale, flip_h);
  }

private:
  void advance_frame()
  {
    if (!tag_)
    {
      return;
    }

    if (tag_->direction == "forward")
    {
      current_frame_++;
      if (current_frame_ > static_cast<size_t>(tag_->to))
      {
        current_frame_ = tag_->from;
      }
    }
    else if (tag_->direction == "reverse")
    {
      if (current_frame_ <= static_cast<size_t>(tag_->from))
      {
        current_frame_ = tag_->to;
      }
      else
      {
        current_frame_--;
      }
    }
    else if (tag_->direction == "pingpong")
    {
      if (ping_pong_forward_)
      {
        current_frame_++;
        if (current_frame_ >= static_cast<size_t>(tag_->to))
        {
          ping_pong_forward_ = false;
        }
      }
      else
      {
        if (current_frame_ <= static_cast<size_t>(tag_->from))
        {
          ping_pong_forward_ = true;
          current_frame_++;
        }
        else
        {
          current_frame_--;
        }
      }
    }
  }

  const SpriteSheet* sheet_;
  size_t             current_frame_;
  float              elapsed_;
  const FrameTag*    tag_;
  bool               ping_pong_forward_;
};

} // namespace zod

#endif // _SDL3_SPRITE_ANIMATOR_HPP_
