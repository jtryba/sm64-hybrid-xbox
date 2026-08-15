#ifdef TARGET_XBOX

#ifndef GFX_XBOX_H
#define GFX_XBOX_H

#include <stdint.h>

#include "gfx_rendering_api.h"
#include "gfx_window_manager_api.h"

extern struct GfxRenderingAPI gfx_xbox_rapi;
extern struct GfxWindowManagerAPI gfx_xbox_wapi;

#ifdef ENABLE_SHINDOU_TITLE_EASTER_EGG
int gfx_xbox_capture_title_face_rgba16(uint16_t *image, int imageW, int imageH, int sampleW, int sampleH);
#endif

#endif

#endif
