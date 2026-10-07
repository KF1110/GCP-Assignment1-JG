#ifndef _SDL3_CAMERA_HPP_
#define _SDL3_CAMERA_HPP_

#include <glm/ext/matrix_clip_space.hpp> // glm::perspectiveLH_ZO
#include <glm/ext/matrix_transform.hpp>  // glm::lookAtLH, glm::rotate
#include <glm/glm.hpp>                   // glm::vec3, glm::mat4, glm::radians
#include <algorithm>                     // std::clamp
#include <cmath>                         // std::atan2, std::asin, sin, cos

namespace zod
{

// Where a scene is seen from, and through what sort of lens.
//
// A camera can be built in the vertex shader, out of whatever few numbers the
// C++ happens to send - an angle here, a distance there.  That is fine while
// the camera is a line of arithmetic, and no longer fine once it is a thing in
// its own right that a game moves about, points at what matters and hands to
// more than one shader.  This is that thing.  It holds:
//
//   position, target, up   where it is, what it looks at, and which way is up
//   fov, aspect            how wide a view, and the shape of the window
//   near_plane, far_plane  how near and how far it can see
//
// and turns them into the two matrices every 3D program needs.
//
// Notice what is *not* here: no yaw, no pitch, no roll.  Angles are how a
// camera is *moved*, not what it is - orbit() and roll() below work in them -
// and storing them as well as a position would be storing the same fact
// twice.  Two copies of a fact disagree sooner or later: move the camera
// sideways and a stored yaw is a lie, so it would have to be worked out again
// from the position, through arithmetic that misbehaves at the poles.  With a
// position and a target there is nothing to disagree.
//
// It owns nothing - no device, no file, no memory - so it is not built like
// the rest of Zod.  There is no create() and no Result: it is a plain struct
// of numbers, which a program declares as a member and fills in.  Being an
// aggregate, it can also be written out in one go, Camera{...}, or by naming
// the fields that matter: Camera{.fov = glm::radians(60.0f)}.
//
// The matrices come from GLM, which is in deps for this: lookAtLH and
// perspectiveLH_ZO rather than the plain lookAt and perspective, because
// SDL_GPU's clip space is left-handed with z running from 0 at the near plane
// to 1 at the far one, where GLM's unsuffixed functions assume OpenGL's
// right-handed space with z from -1 to 1.  Those two names are the whole of
// the difference, and getting them wrong gives a picture that looks almost
// right - which is worse than one that looks wrong.
struct Camera
{
  glm::vec3 position{0.0f, 0.0f, -5.0f}; // where the eye is
  glm::vec3 target{0.0f, 0.0f, 0.0f};    // and what it is pointed at
  glm::vec3 up{0.0f, 1.0f, 0.0f};        // which way is up, for the eye

  float fov        = glm::radians(50.0f); // the vertical field of view
  float aspect     = 16.0f / 9.0f;        // the window's width over its height
  float near_plane = 0.1f;                // nearer than this is not drawn
  float far_plane  = 100.0f;              // nor further than this

  // How far the pitch may go, in orbit() below: just short of overhead, where
  // the eye and the up vector would line up and the view matrix give out.
  static constexpr float pitch_limit = 1.55f;

  // --- pointing it --------------------------------------------------------

  // Stand here, look at that.  This is the one way of aiming a camera that
  // needs no angles at all, which is why it is the one a program should reach
  // for first - to frame a model, to follow a player, to cut to a new view.
  void look_at(const glm::vec3& eye, const glm::vec3& at)
  {
    position = eye;
    target   = at;
  }

  // --- moving it ----------------------------------------------------------

  // Slide the whole camera: the eye and what it looks at, together.  Nothing
  // turns, so the view keeps its direction and simply shows somewhere else -
  // which is what a player walking about expects, and what a stick pushed
  // sideways should do.  This is the one to reach for first.
  void move(const glm::vec3& by)
  {
    position += by;
    target   += by;
  }

  // Slide the eye alone, leaving the target where it is, so the camera swings
  // as it goes to keep looking at the same place.  Useful for circling a
  // thing, or for raising the view without losing what it is pointed at.
  void move_position(const glm::vec3& by) { position += by; }

  // Slide the whole camera in its *own* directions rather than the world's:
  // x to its right, y its own up, z straight ahead.  Pushing a stick forward
  // should go where the camera is looking, not along whichever axis the world
  // happens to call z - and once the camera has been turned, those are no
  // longer the same thing.
  //
  // The three directions are worked out the way lookAtLH works them out, from
  // the direction being looked along and the up vector, so what moves and what
  // is seen cannot disagree.  Note that ahead includes the pitch: look down
  // and forward goes downwards, as a flying camera should.  For a camera that
  // walks instead, flatten ahead on to the ground before using it.
  void move_local(const glm::vec3& by)
  {
    const glm::vec3 ahead = forward();
    const glm::vec3 side  = glm::cross(up, ahead);
    if (glm::length(side) <= 0.0f)
    {
      return; // looking straight along up: there is no "right" to speak of
    }

    const glm::vec3 right = glm::normalize(side);
    const glm::vec3 above = glm::cross(ahead, right);

    move(by.x * right + by.y * above + by.z * ahead);
  }

  // Swing the eye around the target: yaw turns it about the up axis, pitch
  // lifts it over or drops it under.  Both are *changes*, in radians, which is
  // what a stick gives: the angles the camera is at now are read back out of
  // where it stands, changed, and turned into a new position at the same
  // distance.  Nothing is stored, so nothing can go stale.
  void orbit(float yaw, float pitch)
  {
    const glm::vec3 from     = position - target;
    const float     distance = glm::length(from);
    if (distance <= 0.0f)
    {
      return; // standing on the target: there is no direction to turn
    }

    // Where it is now, as two angles.  asin wants its argument within -1 to 1,
    // and division can leave it a hair outside, hence the clamp.
    const float was_yaw   = std::atan2(from.x, from.z);
    const float was_pitch = std::asin(std::clamp(from.y / distance,
                                                 -1.0f, 1.0f));

    const float now_yaw   = was_yaw + yaw;
    const float now_pitch = std::clamp(was_pitch + pitch, -pitch_limit,
                                       pitch_limit);

    position = target + distance * glm::vec3(
                 std::cos(now_pitch) * std::sin(now_yaw),
                 std::sin(now_pitch),
                 std::cos(now_pitch) * std::cos(now_yaw));
  }

  // Tilt the horizon: turn the up vector about the direction being looked
  // along, which leaves what is in view alone and rolls the picture in it.
  void roll(float angle)
  {
    const glm::mat4 turn = glm::rotate(glm::mat4{1.0f}, angle, forward());
    up = glm::normalize(glm::vec3{turn * glm::vec4{up, 0.0f}});
  }

  // --- asking it things ---------------------------------------------------

  float     distance() const { return glm::length(position - target); }
  glm::vec3 forward()  const { return glm::normalize(target - position); }

  // --- the two matrices ---------------------------------------------------

  // Where everything is, seen from here: the world turned and moved so that
  // the eye is at the origin looking along +z.
  glm::mat4 view() const { return glm::lookAtLH(position, target, up); }

  // How distance makes things smaller, and what is too near or too far to
  // draw at all.
  glm::mat4 projection() const
  {
    return glm::perspectiveLH_ZO(fov, aspect, near_plane, far_plane);
  }

  // The two together, which is what a shader usually wants: one matrix to put
  // beside the model's own.  Read it right to left - view first, then the
  // projection - as matrices always are.
  glm::mat4 view_projection() const { return projection() * view(); }
};

} // namespace zod

#endif // _SDL3_CAMERA_HPP_
