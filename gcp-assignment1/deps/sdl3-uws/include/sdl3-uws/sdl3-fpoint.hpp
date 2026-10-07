#ifndef _FPOINT_HPP_
#define _FPOINT_HPP_

#include <SDL3/SDL.h>
#include <cmath>   // std::sin, std::cos, std::sqrt
#include <ostream> // std::ostream::operator<<

namespace zod
{

struct FPoint;
inline float dot(const FPoint& a, const FPoint& b);

struct FPoint : public ::SDL_FPoint
{
  using self_t = FPoint;

  FPoint() = default;
  FPoint(const self_t&) = default;
  FPoint(self_t&&) = default;
  FPoint(float x, float y) : ::SDL_FPoint{x, y} {}
  ~FPoint() = default;

  self_t &operator=(const self_t&) = default;
  self_t &operator=(self_t&&) = default;
  self_t &operator=(const float f) { x=f; y=f; return *this; }

  bool operator==(const self_t& r) const { return x==r.x && y==r.y; }

  // copy constructor and assignment from an ::SDL_FPoint
  FPoint(const ::SDL_FPoint& r) : ::SDL_FPoint{r.x, r.y} {}
  self_t &operator=(const ::SDL_FPoint& r) { x=r.x; y=r.y; return *this; }

  self_t operator+(const float f) const { return {x+f,y+f}; }
  self_t operator-(const float f) const { return {x-f,y-f}; }
  self_t operator*(const float f) const { return {x*f,y*f}; }
  friend self_t operator*(const float f, const self_t& r) { return r*f; }
  self_t operator/(const float f) const { return {x/f,y/f}; }

  friend self_t operator+(const self_t& l, const self_t& r) {
    return {l.x+r.x, l.y+r.y};
  }
  friend self_t operator-(const self_t& l, const self_t& r) {
    return {l.x-r.x, l.y-r.y};
  }

  self_t &operator+=(const self_t &r) { x+=r.x; y+=r.y; return *this; }
  self_t &operator/=(const self_t &r) { x/=r.x; y/=r.y; return *this; }

  self_t &operator*=(const float f) { x*=f; y*=f; return *this; }
  self_t &operator/=(const float f) { x/=f; y/=f; return *this; }

  float length()         const { return std::sqrt(dot(*this,*this)); }
  float length_squared() const { return           dot(*this,*this);  }
  void normalise() { (*this) /= (length() == 0 ? 1 : length()); }
};

inline float dot(const FPoint& a, const FPoint& b) { return a.x*b.x+a.y*b.y; }

inline std::ostream &operator<<(std::ostream &os, const FPoint& v)
{
  os << '(' << v.x << ',' << v.y << ')';
  return os;
}

} // namespace zod

#endif // _FPOINT_HPP_
