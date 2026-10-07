#ifndef _SDL3_EXPECTED_HPP_
#define _SDL3_EXPECTED_HPP_

#if __has_include(<version>)
#include <version>
#endif

#ifdef __cpp_lib_expected
#include <expected>
namespace ns = std;
#else
#include "tl/expected.hpp"
namespace ns = tl;
#endif

#include <SDL3/SDL.h> // SDL_GetError
#include <string>     // std::string
#include <utility>    // std::move

namespace zod
{

template <class T>
using Result = ns::expected<T, std::string>;

// A failure with a message of your own
inline ns::unexpected<std::string> fail(std::string msg)
{
  return ns::unexpected(std::move(msg));
}

inline ns::unexpected<std::string> sdl_fail()
{
  return ns::unexpected(std::string{SDL_GetError()});
}

inline ns::unexpected<std::string> sdl_fail(std::string context)
{
  return ns::unexpected(std::move(context) + ": " + SDL_GetError());
}

} // namespace zod

#endif // _SDL3_EXPECTED_HPP_
