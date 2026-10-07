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
#ifndef SDL_playstation_h_
#define SDL_playstation_h_

#ifdef __cplusplus
extern "C" {
#endif

#include <system_service.h>
#include <user_service.h>
#include <np.h>

#define SDL_MAX_PLAYSTATION_USERS 16  /* 16, according to the docs. */

#define SDL_PROP_STORAGE_PLAYSTATION_MAX_BLOCKS_NUMBER "SDL.storage.playstation.maxblocks"

extern bool SDL_PLAYSTATION_InitUserServices(void);
extern void SDL_PLAYSTATION_QuitUserServices(void);
extern void SDL_PLAYSTATION_PumpUserServicesEvents(void);

extern int32_t SDL_PLAYSTATION_GetDefaultUser(void);

extern bool SDL_PLAYSTATION_InitUDS(void);
extern void SDL_PLAYSTATION_QuitUDS(void);
extern bool SDL_PLAYSTATION_SetDefaultActivity(void);
extern bool SDL_PLAYSTATION_GetUDS(SceNpUniversalDataSystemContext *ctx, SceNpUniversalDataSystemHandle *handle);

#ifdef __cplusplus
}
#endif

#endif // SDL_playstation_h_
