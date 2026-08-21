#ifdef TARGET_XBOX

#include <hal/video.h>
#include <hal/xbox.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <hal/debug.h>
#include <pbkit/pbkit.h>

#include "gfx_window_manager_api.h"
#include "gfx_xbox.h"
#include "macros.h"
#include "game/thread6.h"

#ifdef VERSION_EU
#define REFRESH_RATE REFRESH_50HZ
#else
#define REFRESH_RATE REFRESH_60HZ
#endif

int win_width;
int win_height;

static void gfx_xbox_wapi_init(const char *game_name, bool start_in_fullscreen) {
    int status;

    // try 720p first
    status = XVideoSetMode(1280, 720, 32, REFRESH_RATE);

    // fall back to 640x480
    if (!status) XVideoSetMode(640, 480, 32, REFRESH_RATE);

    if ((status = pb_init())) {
        debugPrint("gfx_xbox_wapi_init: pb_init failed: %d\n", status);
        while (1) Sleep(100);
    }

    pb_show_front_screen();

    win_width = pb_back_buffer_width();
    win_height = pb_back_buffer_height();

    debugPrint("gfx_xbox_wapi_init: resolution %dx%d\n", win_width, win_height);
}

static void gfx_xbox_wapi_set_keyboard_callbacks(bool (*on_key_down)(int scancode), bool (*on_key_up)(int scancode), void (*on_all_keys_up)(void)) {
}

static void gfx_xbox_wapi_set_fullscreen_changed_callback(void (*on_fullscreen_changed)(bool is_now_fullscreen)) {
}

static void gfx_xbox_wapi_set_fullscreen(bool enable) {
}

static void gfx_xbox_wapi_main_loop(void (*run_one_game_iter)(void)) {
    run_one_game_iter();
}

static void gfx_xbox_wapi_get_dimensions(uint32_t *width, uint32_t *height) {
    *width = win_width;
    *height = win_height;
}

#ifdef ENABLE_SHINDOU_TITLE_EASTER_EGG
extern void gfx_mark_dynamic_texture(const void *addr);

int gfx_xbox_capture_title_face_rgba16(uint16_t *image, int imageW, int imageH, int sampleW, int sampleH) {
    uint32_t *framebuffer;
    uint32_t framebufferWidth;
    uint32_t framebufferHeight;
    uint32_t framebufferPitchPixels;
    uint32_t activeWidth;
    uint32_t activeX;
    int iy;
    int ix;

    if (image == NULL || imageW <= 0 || imageH <= 0 || sampleW <= 0 || sampleH <= 0) {
        return 0;
    }

    framebuffer = pb_back_buffer();
    framebufferWidth = pb_back_buffer_width();
    framebufferHeight = pb_back_buffer_height();
    framebufferPitchPixels = pb_back_buffer_pitch() / sizeof(uint32_t);

    if (framebuffer == NULL || framebufferWidth == 0 || framebufferHeight == 0 ||
        framebufferPitchPixels < framebufferWidth) {
        return 0;
    }

    /*
     * gfx_pc preserves the original 4:3 coordinate space inside the native
     * Xbox framebuffer. At 640x480 the active width is 640 pixels. At
     * 1280x720 it is 960 pixels centered inside the 16:9 output.
     */
    activeWidth = (framebufferHeight * 4) / 3;
    if (activeWidth > framebufferWidth) {
        activeWidth = framebufferWidth;
    }
    activeX = (framebufferWidth - activeWidth) / 2;

    /*
     * pb_finished() advances pb_back_buffer() to the next free rotating
     * surface after a completed frame. The title geo callback runs before
     * gfx_xbox_wapi_start_frame() clears that surface for reuse, so it still
     * contains an older completed title image.
     */
    while (pb_busy()) {
    }

    for (iy = 0; iy < imageH; ++iy) {
        int logicalY0 = 80 + iy * sampleH;
        int logicalY1 = logicalY0 + sampleH;
        uint32_t nativeY0 = ((uint32_t) logicalY0 * framebufferHeight) / 240;
        uint32_t nativeY1 = (((uint32_t) logicalY1 * framebufferHeight) + 239) / 240;

        if (nativeY1 > framebufferHeight) {
            nativeY1 = framebufferHeight;
        }
        if (nativeY1 <= nativeY0) {
            nativeY1 = nativeY0 + 1;
        }

        for (ix = 0; ix < imageW; ++ix) {
            int logicalX0 = 120 + ix * sampleW;
            int logicalX1 = logicalX0 + sampleW;
            uint32_t nativeX0 = activeX + ((uint32_t) logicalX0 * activeWidth) / 320;
            uint32_t nativeX1 = activeX + (((uint32_t) logicalX1 * activeWidth) + 319) / 320;
            uint32_t y;
            uint32_t x;
            uint32_t r = 0;
            uint32_t g = 0;
            uint32_t b = 0;
            uint32_t count = 0;
            uint16_t rgba16;

            if (nativeX1 > activeX + activeWidth) {
                nativeX1 = activeX + activeWidth;
            }
            if (nativeX1 <= nativeX0) {
                nativeX1 = nativeX0 + 1;
            }

            for (y = nativeY0; y < nativeY1; ++y) {
                const uint32_t *row = framebuffer + y * framebufferPitchPixels;

                for (x = nativeX0; x < nativeX1; ++x) {
                    uint32_t pixel = row[x];

                    r += (pixel >> 19) & 0x1F;
                    g += (pixel >> 11) & 0x1F;
                    b += (pixel >> 3) & 0x1F;
                    count++;
                }
            }

            if (count == 0) {
                return 0;
            }

            r = (r + count / 2) / count;
            g = (g + count / 2) / count;
            b = (b + count / 2) / count;

            rgba16 = (uint16_t) ((r << 11) | (g << 6) | (b << 1) | 1);

            /*
             * gfx_pc imports RGBA16 texture bytes in N64 big-endian order.
             * Xbox is little-endian, so store the two bytes swapped.
             */
            image[imageW * iy + ix] = (uint16_t) ((rgba16 << 8) | (rgba16 >> 8));
        }
    }

    gfx_mark_dynamic_texture(image);
    return 1;
}
#endif

static void gfx_xbox_wapi_handle_events(void) {
}

static bool gfx_xbox_wapi_start_frame(void) {
    static DWORD lastPacedVblank;
    DWORD currentVblank;
    DWORD targetVblank;

    /*
     * Release 30 Hz pacing:
     *
     * Pace from pbkit's VBlank counter, not rounded millisecond timing.
     * SM64 advances one game frame per two 60 Hz refreshes.
     *
     * If rendering has already consumed two or more VBlanks, preserve the
     * proven late-frame catch-up behavior and start a new cadence from the
     * current display phase.
     *
     * PFIFO synchronization remains unchanged.
     */
    currentVblank = pb_get_vbl_counter();

    if (lastPacedVblank == 0) {
        targetVblank = currentVblank + 2;
    } else if ((DWORD)(currentVblank - lastPacedVblank) >= 2) {
        targetVblank = currentVblank;
    } else {
        targetVblank = lastPacedVblank + 2;
    }

    while ((int32_t)(pb_get_vbl_counter() - targetVblank) < 0) {
        pb_wait_for_vbl();
#ifdef ENABLE_RUMBLE
        rumble_scheduler_tick();
#endif
    }

    lastPacedVblank = pb_get_vbl_counter();

    pb_reset();
    pb_target_back_buffer();

    while (pb_busy());

    return true;
}

static void gfx_xbox_wapi_swap_buffers_begin(void) {
    while (pb_busy());
    while (pb_finished());
}

static void gfx_xbox_wapi_swap_buffers_end(void) {
}

static double gfx_xbox_wapi_get_time(void) {
    return 0.0;
}

struct GfxWindowManagerAPI gfx_xbox_wapi = {
    gfx_xbox_wapi_init,
    gfx_xbox_wapi_set_keyboard_callbacks,
    gfx_xbox_wapi_set_fullscreen_changed_callback,
    gfx_xbox_wapi_set_fullscreen,
    gfx_xbox_wapi_main_loop,
    gfx_xbox_wapi_get_dimensions,
    gfx_xbox_wapi_handle_events,
    gfx_xbox_wapi_start_frame,
    gfx_xbox_wapi_swap_buffers_begin,
    gfx_xbox_wapi_swap_buffers_end,
    gfx_xbox_wapi_get_time
};

#endif
