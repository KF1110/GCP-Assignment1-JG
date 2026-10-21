// This file tells Visual Studio's IntelliSense three things about the PS5 that
// Sony's own IntelliSense header gets wrong. This is force-included after that
// header; see prospero-intellisense.props beside this file, which arranges it.
//
// None of it reaches the compiler: __INTELLISENSE__ is defined only while
// Visual Studio is parsing for the editor, so this file compiles to nothing.
// The __cplusplus test is there because forced includes reach C files too.
#if defined(__INTELLISENSE__) && defined(__cplusplus)

// 1. Which of the SDK's two C++ libraries is in use.
//
// The compiler chooses it from the language standard.  Asking it,
//
//   prospero-clang.exe -std=c++NN -x c++ -dM -E empty.cpp
//
//   -std=c++11, c++14, c++17   __SCE_LIBC_CXX_VERSION__ 1
//   -std=c++20, c++23          __SCE_LIBC_CXX_VERSION__ 2
//
// Sony's header is a snapshot of the macros the compiler defines by default,
// and the default is C++17 - so it says 1, whatever the project is set to.  It
// takes care to make __cplusplus follow the project's standard, through
// __INTELLISENSE_CPP20__; it just does not do the same for this macro, which
// the compiler derives from precisely that.  So the rule is repeated here.
//
// Left at 1, _cxxvers/config.h leaves _SCE_CXXVERS undefined, the C++20
// library's yvals_core.h takes its stock Microsoft path, and that begins by
// including <vcruntime.h>, which exists only in MSVC.  Every standard header
// includes yvals_core.h, so the whole library ends up half-parsed.
#undef __SCE_LIBC_CXX_VERSION__

#if __cplusplus >= 202002L
#define __SCE_LIBC_CXX_VERSION__ 2
#else
#define __SCE_LIBC_CXX_VERSION__ 1
#endif

// 2. Six type traits that are currently stubbed out as false.
//
// Sony's header carries a block of "Clang Language Extensions" for the parser,
// which defines twenty-seven traits this way:
//
//   #define __is_same(x) 0
//   #define __is_floating_point(x) 0
//   ...
//
// One parameter each, and always nought.  That was harmless for the older C++
// library, which never asked the compiler for them.  The C++20 library does,
// and then they are actively wrong: __is_same(_Ty1, _Ty2) is a two-argument
// use of a one-parameter macro, and what it yields is an int rather than a
// bool - which the parser reports at <concepts> line 36 as "atomic constraint
// must have type bool".  same_as underpins nearly every other concept, so
// everything built on it goes with it: std::span's constrained constructors,
// the comparison operators in <compare>, the specialisations in <numbers>
// (whose primary template is deliberately unusable, and shows up as
// _Invalid<double>).
//
// Of the twenty-seven, the C++20 library uses six.  Each is written out here.
// Nothing fancy is needed: these are the textbook definitions, and the parser
// has no trouble with them - it was only ever missing the builtins.
//
// Note the #undef before each one.  Sony's definitions already exist, so a
// plain #ifndef guard would skip every one of them.
namespace _Sce_intellisense
{
  template <class, class> inline constexpr bool same = false;
  template <class _Ty>    inline constexpr bool same<_Ty, _Ty> = true;

  template <class _Ty> struct no_cv                     { using type = _Ty; };
  template <class _Ty> struct no_cv<const _Ty>          { using type = _Ty; };
  template <class _Ty> struct no_cv<volatile _Ty>       { using type = _Ty; };
  template <class _Ty> struct no_cv<const volatile _Ty> { using type = _Ty; };

  template <class _Ty> using bare = typename no_cv<_Ty>::type;

  template <class _Ty, class... _Types>
  inline constexpr bool any_of = (same<_Ty, _Types> || ...);

  template <class _Ty>
  inline constexpr bool floating_point = any_of<bare<_Ty>, float, double, long double>;

  template <class _Ty>
  inline constexpr bool integral = any_of<bare<_Ty>, bool, char, signed char,
      unsigned char, wchar_t,
#ifdef __cpp_char8_t
      char8_t,
#endif
      char16_t, char32_t, short, unsigned short, int, unsigned int, long,
      unsigned long, long long, unsigned long long>;

  // A function type is the one thing const cannot be added to, which is how
  // this is told apart from an ordinary member without asking the compiler.
  template <class>      inline constexpr bool reference      = false;
  template <class _Ty>  inline constexpr bool reference<_Ty&>  = true;
  template <class _Ty>  inline constexpr bool reference<_Ty&&> = true;

  template <class>     inline constexpr bool is_const         = false;
  template <class _Ty> inline constexpr bool is_const<const _Ty> = true;

  template <class _Ty>
  inline constexpr bool function = !is_const<const _Ty> && !reference<_Ty>;

  template <class> inline constexpr bool member_pointer_impl = false;
  template <class _Ty, class _Class>
  inline constexpr bool member_pointer_impl<_Ty _Class::*> = true;

  template <class> inline constexpr bool member_function_impl = false;
  template <class _Ty, class _Class>
  inline constexpr bool member_function_impl<_Ty _Class::*> = function<_Ty>;

  template <class _Ty>
  inline constexpr bool member_pointer = member_pointer_impl<bare<_Ty>>;

  template <class _Ty>
  inline constexpr bool member_function_pointer = member_function_impl<bare<_Ty>>;

  template <class _Ty>
  inline constexpr bool member_object_pointer =
      member_pointer<_Ty> && !member_function_pointer<_Ty>;
}

#undef __is_same
#define __is_same(...) (::_Sce_intellisense::same<__VA_ARGS__>)

#undef __is_floating_point
#define __is_floating_point(...) (::_Sce_intellisense::floating_point<__VA_ARGS__>)

#undef __is_integral
#define __is_integral(...) (::_Sce_intellisense::integral<__VA_ARGS__>)

#undef __is_member_pointer
#define __is_member_pointer(...) (::_Sce_intellisense::member_pointer<__VA_ARGS__>)

#undef __is_member_function_pointer
#define __is_member_function_pointer(...) \
  (::_Sce_intellisense::member_function_pointer<__VA_ARGS__>)

#undef __is_member_object_pointer
#define __is_member_object_pointer(...) \
  (::_Sce_intellisense::member_object_pointer<__VA_ARGS__>)

// 3. One feature question answered wrongly for GLM.
//
// The same block of Sony's stubs the traits came from also has
//
//   #define __has_feature(x) 0
//
// so every question about the compiler's features is answered "no".  GLM asks
// one that matters, in detail/setup.hpp:
//
//   #elif (GLM_COMPILER & GLM_COMPILER_CLANG)
//   #  define GLM_HAS_CONSTEXPR __has_feature(cxx_relaxed_constexpr)
//
// It takes that branch because Sony's header defines __clang__.  Answered
// "no", GLM_CONSTEXPR expands to nothing, glm::vec3's constructor is not
// constexpr, and a constexpr glm::vec3 is an error - which the real compiler
// has no trouble with, since it answers "yes".
//
// So the question is answered per feature rather than wholesale.  Each one
// becomes an identifier; the ones named below are 1, and anything else is an
// unknown identifier, which a preprocessor conditional reads as 0 - exactly
// what Sony's stub gave.  To answer another, add a line.
//
// This only works inside #if and #elif, which is the only place anything in
// this project asks.
#undef __has_feature
#define __has_feature(x) _Sce_intellisense_feature_##x

#define _Sce_intellisense_feature_cxx_relaxed_constexpr 1

#endif // __INTELLISENSE__ && __cplusplus
