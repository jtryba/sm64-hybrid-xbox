#include <stdlib.h>
#include <stdio.h>

#ifdef TARGET_XBOX
#include <windows.h>
#endif

#ifdef TARGET_WEB
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "sm64.h"

#include "game/memory.h"
#include "audio/external.h"

#include "gfx/gfx_pc.h"
#include "gfx/gfx_opengl.h"
#include "gfx/gfx_direct3d11.h"
#include "gfx/gfx_direct3d12.h"
#include "gfx/gfx_dxgi.h"
#include "gfx/gfx_glx.h"
#include "gfx/gfx_sdl.h"
#include "gfx/gfx_xbox.h"
#include "gfx/gfx_dummy.h"

#include "audio/audio_api.h"
#include "audio/audio_wasapi.h"
#include "audio/audio_pulse.h"
#include "audio/audio_alsa.h"
#include "audio/audio_sdl.h"
#include "audio/audio_xbox.h"
#include "audio/audio_null.h"

#include "controller/controller_keyboard.h"

#include "configfile.h"
#include "utils.h"
#include "compat.h"

#define CONFIG_FILE "sm64config.txt"

OSMesg D_80339BEC;
OSMesgQueue gSIEventMesgQueue;

s8 gResetTimer;
s8 D_8032C648;
s8 gDebugLevelSelect;
s8 gShowProfiler;
s8 gShowDebugText;

static struct AudioAPI *audio_api;
static struct GfxWindowManagerAPI *wm_api;
static struct GfxRenderingAPI *rendering_api;

extern void gfx_run(Gfx *commands);
extern void thread5_game_loop(void *arg);
extern void create_next_audio_buffer(s16 *samples, u32 num_samples);
void game_loop_one_iteration(void);

void dispatch_audio_sptask(UNUSED struct SPTask *spTask) {
}

void set_vblank_handler(UNUSED s32 index, UNUSED struct VblankHandler *handler, UNUSED OSMesgQueue *queue, UNUSED OSMesg *msg) {
}

static uint8_t inited = 0;

#include "game/game_init.h" // for gGlobalTimer
void send_display_list(struct SPTask *spTask) {
    if (!inited) {
        return;
    }
    gfx_run((Gfx *)spTask->task.t.data_ptr);
}

#define printf

#ifdef VERSION_EU
#define SAMPLES_HIGH 656
#define SAMPLES_LOW 640
#else
#define SAMPLES_HIGH 544
#define SAMPLES_LOW 528
#endif

static s16 audio_buffer[SAMPLES_HIGH * 2 * 2];

#ifdef TARGET_XBOX
extern uint32_t g_xbox_perf_vblank1_ms;
extern uint32_t g_xbox_perf_vblank2_ms;
extern uint32_t g_xbox_perf_vblank_wait_count;
extern uint32_t g_xbox_perf_pace_elapsed_before_ms;
extern uint32_t g_xbox_perf_start_busy_ms;
extern uint32_t g_xbox_perf_swap_busy_ms;
extern uint32_t g_xbox_perf_swap_finished_ms;
extern uint32_t g_xbox_perf_title_capture_busy_ms;
extern uint32_t g_xbox_perf_draw_finish_ms;
extern uint32_t g_xbox_perf_draw_finish_count;

static FILE *s_xbox_perf_log;
static uint32_t s_xbox_perf_frame;

static void xbox_perf_log_open(void) {
    if (s_xbox_perf_log != NULL) {
        return;
    }

    s_xbox_perf_log = fopen("D:\\sm64_perf.csv", "w");
    if (s_xbox_perf_log == NULL) {
        return;
    }

    setvbuf(s_xbox_perf_log, NULL, _IOFBF, 64 * 1024);
    fprintf(s_xbox_perf_log,
        "frame,total_ms,game_ms,audio_ms,gfx_end_ms,"
        "vblank1_ms,vblank2_ms,vblank_wait_count,pace_elapsed_before_ms,"
        "start_busy_ms,swap_busy_ms,swap_finished_ms,"
        "draw_finish_ms,draw_finish_count,title_capture_busy_ms,"
        "audio_before,audio_after,flush_frame\n");
}
#endif

void produce_one_frame(void) {
#ifdef TARGET_XBOX
    DWORD perfFrameStart = GetTickCount();
    DWORD perfGameStart;
    DWORD perfGameEnd;
    DWORD perfAudioEnd;
    DWORD perfFrameEnd;
    int perfAudioAfter;
    int perfFlush = 0;

    xbox_perf_log_open();
#endif

    gfx_start_frame();

#ifdef TARGET_XBOX
    perfGameStart = GetTickCount();
#endif

    game_loop_one_iteration();

#ifdef TARGET_XBOX
    perfGameEnd = GetTickCount();
#endif

    int samples_left = audio_api->buffered();
    u32 num_audio_samples = samples_left < audio_api->get_desired_buffered() ? SAMPLES_HIGH : SAMPLES_LOW;
    //printf("Audio samples: %d %u\n", samples_left, num_audio_samples);
    for (int i = 0; i < 2; i++) {
        /*if (audio_cnt-- == 0) {
            audio_cnt = 2;
        }
        u32 num_audio_samples = audio_cnt < 2 ? 528 : 544;*/
        create_next_audio_buffer(audio_buffer + i * (num_audio_samples * 2), num_audio_samples);
    }
    //printf("Audio samples before submitting: %d\n", audio_api->buffered());
    audio_api->play((u8 *)audio_buffer, 2 * num_audio_samples * 4);

#ifdef TARGET_XBOX
    perfAudioEnd = GetTickCount();
#endif

    gfx_end_frame();

#ifdef TARGET_XBOX
    perfFrameEnd = GetTickCount();
    perfAudioAfter = audio_api->buffered();

    if (s_xbox_perf_log != NULL) {
        /*
         * Flush every 600 frames so abrupt resets do not lose the whole run.
         * Mark the flush frame explicitly; its timing must not be interpreted
         * as a renderer-originated stall.
         */
        perfFlush = ((s_xbox_perf_frame + 1) % 600) == 0;

        fprintf(s_xbox_perf_log,
            "%lu,%lu,%lu,%lu,%lu,"
            "%lu,%lu,%lu,%lu,%lu,%lu,%lu,"
            "%lu,%lu,%lu,%d,%d,%d\n",
            (unsigned long)s_xbox_perf_frame,
            (unsigned long)(perfFrameEnd - perfFrameStart),
            (unsigned long)(perfGameEnd - perfGameStart),
            (unsigned long)(perfAudioEnd - perfGameEnd),
            (unsigned long)(perfFrameEnd - perfAudioEnd),
            (unsigned long)g_xbox_perf_vblank1_ms,
            (unsigned long)g_xbox_perf_vblank2_ms,
            (unsigned long)g_xbox_perf_vblank_wait_count,
            (unsigned long)g_xbox_perf_pace_elapsed_before_ms,
            (unsigned long)g_xbox_perf_start_busy_ms,
            (unsigned long)g_xbox_perf_swap_busy_ms,
            (unsigned long)g_xbox_perf_swap_finished_ms,
            (unsigned long)g_xbox_perf_draw_finish_ms,
            (unsigned long)g_xbox_perf_draw_finish_count,
            (unsigned long)g_xbox_perf_title_capture_busy_ms,
            samples_left,
            perfAudioAfter,
            perfFlush);

        if (perfFlush) {
            fflush(s_xbox_perf_log);
        }
    }

    s_xbox_perf_frame++;
#endif
}

#ifdef TARGET_WEB
static void em_main_loop(void) {
}

static void request_anim_frame(void (*func)(double time)) {
    EM_ASM(requestAnimationFrame(function(time) {
        dynCall("vd", $0, [time]);
    }), func);
}

static void on_anim_frame(double time) {
    static double target_time;

    time *= 0.03; // milliseconds to frame count (33.333 ms -> 1)

    if (time >= target_time + 10.0) {
        // We are lagging 10 frames behind, probably due to coming back after inactivity,
        // so reset, with a small margin to avoid potential jitter later.
        target_time = time - 0.010;
    }

    for (int i = 0; i < 2; i++) {
        // If refresh rate is 15 Hz or something we might need to generate two frames
        if (time >= target_time) {
            produce_one_frame();
            target_time = target_time + 1.0;
        }
    }

    request_anim_frame(on_anim_frame);
}
#endif

static void save_config(void) {
    configfile_save(get_config_filename());
}

static void on_fullscreen_changed(bool is_now_fullscreen) {
    configFullscreen = is_now_fullscreen;
}

void main_func(void) {
#ifdef USE_SYSTEM_MALLOC
    main_pool_init();
    gGfxAllocOnlyPool = alloc_only_pool_init();
#else
    static u64 pool[0x165000/8 / 4 * 8];
    main_pool_init(pool, pool + sizeof(pool) / sizeof(pool[0]));
#endif
    gEffectsMemoryPool = mem_pool_init(0x4000, MEMORY_POOL_LEFT);

    configfile_load(get_config_filename());
    atexit(save_config);

#ifdef TARGET_WEB
    emscripten_set_main_loop(em_main_loop, 0, 0);
    request_anim_frame(on_anim_frame);
#endif

#if defined(ENABLE_DX12)
    rendering_api = &gfx_direct3d12_api;
    wm_api = &gfx_dxgi_api;
#elif defined(ENABLE_DX11)
    rendering_api = &gfx_direct3d11_api;
    wm_api = &gfx_dxgi_api;
#elif defined(ENABLE_OPENGL)
    rendering_api = &gfx_opengl_api;
    #if defined(__linux__) || defined(__BSD__)
        wm_api = &gfx_glx;
    #else
        wm_api = &gfx_sdl;
    #endif
#elif defined(TARGET_XBOX)
    rendering_api = &gfx_xbox_rapi;
    wm_api = &gfx_xbox_wapi;
#elif defined(ENABLE_GFX_DUMMY)
    rendering_api = &gfx_dummy_renderer_api;
    wm_api = &gfx_dummy_wm_api;
#endif

    gfx_init(wm_api, rendering_api, "Super Mario 64 PC-Port", configFullscreen);
    
    wm_api->set_fullscreen_changed_callback(on_fullscreen_changed);
    wm_api->set_keyboard_callbacks(keyboard_on_key_down, keyboard_on_key_up, keyboard_on_all_keys_up);

#ifdef TARGET_XBOX
    if (audio_api == NULL && audio_xbox.init()) {
        audio_api = &audio_xbox;
    }
#endif
#if HAVE_WASAPI
    if (audio_api == NULL && audio_wasapi.init()) {
        audio_api = &audio_wasapi;
    }
#endif
#if HAVE_PULSE_AUDIO
    if (audio_api == NULL && audio_pulse.init()) {
        audio_api = &audio_pulse;
    }
#endif
#if HAVE_ALSA
    if (audio_api == NULL && audio_alsa.init()) {
        audio_api = &audio_alsa;
    }
#endif
#ifdef TARGET_WEB
    if (audio_api == NULL && audio_sdl.init()) {
        audio_api = &audio_sdl;
    }
#endif
    if (audio_api == NULL) {
        audio_api = &audio_null;
    }

    audio_init();
    sound_init();

    thread5_game_loop(NULL);
#ifdef TARGET_WEB
    /*for (int i = 0; i < atoi(argv[1]); i++) {
        game_loop_one_iteration();
    }*/
    inited = 1;
#else
    inited = 1;
    while (1) {
        wm_api->main_loop(produce_one_frame);
    }
#endif
}

#if (defined(_WIN32) || defined(_WIN64)) && !defined(TARGET_XBOX)
#include <windows.h>
int WINAPI WinMain(UNUSED HINSTANCE hInstance, UNUSED HINSTANCE hPrevInstance, UNUSED LPSTR pCmdLine, UNUSED int nCmdShow) {
    main_func();
    return 0;
}
#else
int main(UNUSED int argc, UNUSED char *argv[]) {
    main_func();
    return 0;
}
#endif
