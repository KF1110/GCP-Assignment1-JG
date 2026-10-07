#ifndef _SDL3_SPRITE_SHEET_HPP_
#define _SDL3_SPRITE_SHEET_HPP_

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>
#include <SDL3/SDL.h> // SDL_Init, SDL_CreateWindow etc.
#include <SDL3/SDL_rect.h>
#include "sdl3-expected.hpp" // Result, fail, sdl_fail, ns::in_place
#include "sdl3-texture.hpp"  // Texture (and, through it, TextureBank)

namespace zod
{

struct Frame
{
  std::string name;
  SDL_FRect   rect;     // Source rectangle in the atlas
  int         duration; // Duration in milliseconds
};

struct FrameTag
{
  std::string name;
  int         from;
  int         to;
  std::string direction; // "forward", "reverse", "pingpong"
};

// A Sprite remembers the sheet it animates, by pointer, so a sheet has to
// stay where it was made: like the other Zod resources it can be neither
// copied nor moved, and lives until the end of the scope that created it.
class SpriteSheet
{
  struct Key {};   // private tag: only SpriteSheet can name it

public:
  // Everything that can fail happens here, so that the constructor below is
  // handed nothing but finished, valid parts
  static Result<SpriteSheet> create(TextureBank&                 bank,
                                    const std::filesystem::path& json_path)
  {
    std::ifstream file(json_path);
    if (!file.is_open())
    {
      return fail("Failed to open JSON file: " + json_path.string());
    }

    // allow_exceptions = false: a parse error gives a "discarded" value instead
    const nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded())
    {
      return fail("Failed to parse JSON file: " + json_path.string());
    }

    const auto        meta = j.find("meta");
    const std::string image =
      meta != j.end() ? string_or(*meta, "image", "") : std::string{};
    if (image.empty())
    {
      return fail("JSON has no meta.image field: " + json_path.string());
    }

    const std::filesystem::path image_path = json_path.parent_path() / image;

    // The bank loads and keeps the image named by the JSON.  NEAREST is what
    // pixel art wants: crisp pixels when scaled up, rather than blurred ones.
    const auto texture =
      Texture::create(bank, image_path, SDL_SCALEMODE_NEAREST);
    if (!texture)
    {
      return fail(texture.error());
    }

    return Result<SpriteSheet>(ns::in_place, Key{}, texture->sdl(),
                               read_frames(j), read_tags(j));
  }

  SpriteSheet(Key, SDL_Texture* texture, std::vector<Frame> frames,
              std::vector<FrameTag> frame_tags)
    : texture_{texture}, frames_{std::move(frames)},
      frame_tags_{std::move(frame_tags)} { }
  SpriteSheet(const SpriteSheet&) = delete;
  SpriteSheet& operator=(const SpriteSheet&) = delete;

  const Frame& get_frame(size_t index) const
  {
    static const Frame none{"", {0.0f, 0.0f, 0.0f, 0.0f}, 100};
    return index < frames_.size() ? frames_[index] : none;
  }

  size_t frame_count() const
  {
    return frames_.size();
  }

  const FrameTag* find_tag(const std::string& name) const
  {
    for (const auto& tag : frame_tags_)
    {
      if (tag.name == name)
      {
        return &tag;
      }
    }
    return nullptr;
  }

  SDL_Texture* texture() const
  {
    return texture_;
  }

  void render_frame(SDL_Renderer* renderer, size_t frame_index,
                    const SDL_FRect& dest) const
  {
    if (frame_index >= frames_.size())
    {
      return;
    }
    const Frame&    frame = frames_[frame_index];
    const SDL_FRect src = frame.rect;
    SDL_RenderTexture(renderer, texture_, &src, &dest);
  }

  void render_frame(SDL_Renderer* renderer, size_t frame_index, float x,
                    float y, float scale = 1.0f, bool flip_h = false) const
  {
    if (frame_index >= frames_.size())
    {
      return;
    }
    const Frame&    frame = frames_[frame_index];
    const SDL_FRect src   = frame.rect;
    SDL_FRect       dest{x, y, frame.rect.w * scale, frame.rect.h * scale};
    if (flip_h)
    {
      SDL_RenderTextureRotated(renderer, texture_, &src, &dest, 0.0,
                               nullptr, SDL_FLIP_HORIZONTAL);
    }
    else
    {
      SDL_RenderTexture(renderer, texture_, &src, &dest);
    }
  }

private:
  // Non-throwing JSON lookups: a missing or wrongly typed field gives dflt
  static float number_or(const nlohmann::json& j, const char* key, float dflt)
  {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<float>() : dflt;
  }

  static std::string string_or(const nlohmann::json& j, const char* key,
                               const char* dflt)
  {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : dflt;
  }

  static void add_frame(std::vector<Frame>& frames, const std::string& name,
                        const nlohmann::json& frame_json)
  {
    const auto rect_json = frame_json.find("frame");
    if (rect_json == frame_json.end())
    {
      return;
    }

    Frame frame;
    frame.name = name;
    frame.rect.x = number_or(*rect_json, "x", 0.0f);
    frame.rect.y = number_or(*rect_json, "y", 0.0f);
    frame.rect.w = number_or(*rect_json, "w", 0.0f);
    frame.rect.h = number_or(*rect_json, "h", 0.0f);
    frame.duration =
      static_cast<int>(number_or(frame_json, "duration", 100.0f));
    frames.push_back(frame);
  }

  static std::vector<Frame> read_frames(const nlohmann::json& j)
  {
    std::vector<Frame> frames;

    const auto frames_json = j.find("frames");
    if (frames_json == j.end())
    {
      return frames;
    }

    if (frames_json->is_array()) // Aseprite's "array" sprite sheet layout
    {
      for (const auto& frame_json : *frames_json)
      {
        add_frame(frames, string_or(frame_json, "filename", ""), frame_json);
      }
    }
    else if (frames_json->is_object()) // ... and its "hash" layout
    {
      for (const auto& [name, frame_json] : frames_json->items())
      {
        add_frame(frames, name, frame_json);
      }
    }

    return frames;
  }

  static std::vector<FrameTag> read_tags(const nlohmann::json& j)
  {
    std::vector<FrameTag> frame_tags;

    const auto meta = j.find("meta");
    if (meta == j.end())
    {
      return frame_tags;
    }

    const auto tags_json = meta->find("frameTags");
    if (tags_json == meta->end() || !tags_json->is_array())
    {
      return frame_tags;
    }

    for (const auto& tag_json : *tags_json)
    {
      FrameTag tag;
      tag.name = string_or(tag_json, "name", "");
      tag.from = static_cast<int>(number_or(tag_json, "from", 0.0f));
      tag.to = static_cast<int>(number_or(tag_json, "to", 0.0f));
      tag.direction = string_or(tag_json, "direction", "forward");
      frame_tags.push_back(tag);
    }

    return frame_tags;
  }

  // Owned by the TextureBank, which outlives this sheet; nothing to free here
  SDL_Texture*          texture_;
  std::vector<Frame>    frames_;
  std::vector<FrameTag> frame_tags_;
};

} // namespace zod

#endif // _SDL3_SPRITE_SHEET_HPP_
