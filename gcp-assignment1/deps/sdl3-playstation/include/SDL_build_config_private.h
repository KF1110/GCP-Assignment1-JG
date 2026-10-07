/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#ifndef SDL_build_config_private_h_
#define SDL_build_config_private_h_
#define SDL_build_config_h_

/**
 *  \file SDL_build_config_private.h
 *
 *  This is a configuration that can be used to build SDL for PlayStation.
 */


#define HAVE_GCC_ATOMICS 1
#define HAVE_GCC_SYNC_LOCK_TEST_AND_SET 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_NANOSLEEP 1

#define HAVE_LIBC 1
#define HAVE_STDINT_H 1
#define HAVE_FLOAT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_MATH_H 1
#define HAVE_STDARG_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_WCHAR_H 1

/* C library functions */
/* !!! FIXME: check these. */

/* The system has malloc() and friends, but the default heap is too small to
   be useful (for example, it'll fail to allocate a 4K surface), even though
   the system has 16 gigabytes of RAM. As such, we've turned on SDL's
   internal allocator (backed by dlmalloc) to implement SDL_malloc and 
   friends. malloc() itself is still usable by games, and while it's easy
   enough to override it with Sony's hooks to allow a larger heap, it didn't
   feel appropriate for SDL to do that for you. As such, by default, 
   SDL_malloc uses a different heap than malloc().
   An app, if they really wanted to, can use SDL_SetMemoryFunctions() to make
   SDL use the system malloc instead, and _also_ override the system malloc
   using Sony's hooks, to give everything a unified, larger heap. Or they
   can rebuild SDL with this `#if 0` set to 1. */
#if 0
#define HAVE_MALLOC 1
#define HAVE_CALLOC 1
#define HAVE_REALLOC 1
#define HAVE_FREE 1
#endif

#define HAVE_ALLOCA 1
#define HAVE_QSORT 1
#define HAVE_ABS 1
#define HAVE_MEMSET 1
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMCMP 1
#define HAVE_STRLEN 1
#define HAVE_STRCHR 1
#define HAVE_STRRCHR 1
#define HAVE_STRSTR 1
#define HAVE_STRTOL 1
#define HAVE_STRTOUL 1
#define HAVE_STRTOD 1
#define HAVE_ATOI 1
#define HAVE_ATOF 1
#define HAVE_STRCMP 1
#define HAVE_STRNCMP 1
#define HAVE_STRCASECMP 1
#define HAVE_STRNCASECMP 1
#define HAVE_ACOS   1
#define HAVE_ACOSF  1
#define HAVE_ASIN   1
#define HAVE_ASINF  1
#define HAVE_ATAN   1
#define HAVE_ATANF  1
#define HAVE_ATAN2  1
#define HAVE_ATAN2F 1
#define HAVE_CEILF  1
#define HAVE_COPYSIGN  1
#define HAVE_COS    1
#define HAVE_COSF   1
#define HAVE_EXP    1
#define HAVE_EXPF   1
#define HAVE_FABS   1
#define HAVE_FABSF  1
#define HAVE_FLOOR  1
#define HAVE_FLOORF 1
#define HAVE_FMOD   1
#define HAVE_FMODF  1
#define HAVE_LOG    1
#define HAVE_LOGF   1
#define HAVE_LOG10    1
#define HAVE_LOG10F   1
#define HAVE_POW    1
#define HAVE_POWF   1
#define HAVE_SIN    1
#define HAVE_SINF   1
#define HAVE_SQRT   1
#define HAVE_SQRTF  1
#define HAVE_TAN    1
#define HAVE_TANF   1
#define HAVE_STRTOLL 1
#define HAVE_VSSCANF 1
#define HAVE_SCALBN 1
#define HAVE_SCALBNF    1
#define HAVE_M_PI 1
#define HAVE_O_CLOEXEC 1

/* Enable various audio drivers */
#define SDL_AUDIO_DRIVER_PRIVATE 1
#define SDL_AUDIO_DRIVER_DUMMY 1
#define SDL_AUDIO_DRIVER_DISK 1

/* Enable various input drivers */
#define SDL_JOYSTICK_PRIVATE 1
#define SDL_HAPTIC_DISABLED 1

/* Enable sensor driver */
#define SDL_SENSOR_DUMMY 1

/* Enable various shared object loading systems */
#define SDL_LOADSO_PRIVATE 1

/* Disable dialog system */
#define SDL_DIALOG_DISABLED 1

/* Disable HIDAPI */
#define SDL_HIDAPI_DISABLED 1

/* Enable various process systems */
#define SDL_PROCESS_DUMMY 1

/* Enable various threading systems */
#define SDL_THREAD_PTHREAD 1
#define SDL_THREAD_PTHREAD_RECURSIVE_MUTEX 1

/* Enable RTC system */
#define SDL_TIME_PRIVATE 1

/* Enable various timer systems */
#define SDL_TIMER_PRIVATE 1

/* Enable various video drivers */
#define SDL_VIDEO_DRIVER_PRIVATE 1
#define SDL_VIDEO_DRIVER_DUMMY 1

/* Enable the Agc GPU driver */
#define SDL_GPU_PRIVATE 1

/* Enable the SDL_GPU render driver */
#define SDL_VIDEO_RENDER_GPU 1

/* Enable the filesystem driver */
#define SDL_FILESYSTEM_PRIVATE 1
#define SDL_FSOPS_PRIVATE 1

/* Enable the camera driver */
#define SDL_CAMERA_DRIVER_DUMMY 1

/* Enable the storage driver */
#define SDL_STORAGE_PRIVATE 1

/* Enable tray systems */
#define SDL_TRAY_DUMMY 1

// Workaround for PS only supporting exit, not _exit
#ifdef _exit
#undef _exit
#endif
#define _exit exit

// Platform name
#define SDL_PLATFORM_PRIVATE_NAME "PlayStation 5"

// Controller definitions
#define SDL_PRIVATE_GAMEPAD_DEFINITIONS \
    "050000004c050000e60c000000000000,DualSense,platform:PlayStation 5,a:b11,b:b10,x:b12,y:b9,back:b13,start:b2,leftstick:b0,rightstick:b1,leftshoulder:b7,rightshoulder:b8,dpup:b3,dpdown:b5,dpleft:b6,dpright:b4,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:+a4,righttrigger:+a5,",

#define SDL_PLATFORM_PRIVATE_ASSERT
#define SDL_PRIVATE_PROMPTASSERTION() \
    return SDL_ASSERTION_BREAK;

#endif /* SDL_build_config_private_h_ */
