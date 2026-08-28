#if defined(TARGET_XBOX)

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>

#include <hal/xbox.h>
#include <SDL.h>

#include <ultra64.h>

#include "controller_api.h"

#define STICK_DEADZONE 8000
#define STICK_FULL_SCALE 0x4000
#define C_STICK_DEADZONE 0x4000
#define BUTTON_DEADZONE 0x20

/*
 * nxdk SDL converts an Xbox trigger value 0..255 to:
 *
 *   ((value << 8) | value) - 0x8000
 *
 * Preserve the original port's strict:
 *
 *   value > 0x20
 */
#define TRIGGER_AXIS_DEADZONE \
    ((BUTTON_DEADZONE * SDL_JOYSTICK_AXIS_MAX) / 0xFF)

static SDL_GameController *sXboxController = NULL;
static SDL_JoystickID sXboxControllerInstance = -1;
static bool sXboxControllerInitialized = false;
static bool sXboxControllerEverConnected = false;

#ifdef ENABLE_RUMBLE
#define XBOX_RUMBLE_ON_STRENGTH 0xFFFF
#define XBOX_RUMBLE_REFRESH_MS 100

void controller_xbox_set_rumble(
    u8 low_frequency_enabled,
    u8 high_frequency_enabled
) {
    if (
        !sXboxControllerInitialized ||
        sXboxController == NULL
    ) {
        return;
    }

    const Uint16 low_strength =
        low_frequency_enabled ? XBOX_RUMBLE_ON_STRENGTH : 0;

    const Uint16 high_strength =
        high_frequency_enabled ? XBOX_RUMBLE_ON_STRENGTH : 0;

    const Uint32 duration =
        (low_frequency_enabled || high_frequency_enabled)
            ? XBOX_RUMBLE_REFRESH_MS
            : 0;

    SDL_GameControllerRumble(
        sXboxController,
        low_strength,
        high_strength,
        duration
    );
}

static void controller_xbox_shutdown(void) {
    controller_xbox_set_rumble(FALSE, FALSE);
}
#endif

static inline bool xbox_button_pressed(
    SDL_GameController *pad,
    SDL_GameControllerButton button
) {
    return SDL_GameControllerGetButton(
        pad,
        button
    ) != 0;
}

static inline bool xbox_trigger_pressed(
    SDL_GameController *pad,
    SDL_GameControllerAxis axis
) {
    return SDL_GameControllerGetAxis(
        pad,
        axis
    ) > TRIGGER_AXIS_DEADZONE;
}

static void controller_xbox_close_current(void) {
    if (sXboxController != NULL) {
#ifdef ENABLE_RUMBLE
        controller_xbox_set_rumble(FALSE, FALSE);
#endif
        SDL_GameControllerClose(
            sXboxController
        );

        sXboxController = NULL;
    }

    sXboxControllerInstance = -1;
}

static void controller_xbox_open_first(void) {
    if (sXboxController != NULL) {
        return;
    }

    const int count = SDL_NumJoysticks();

    for (int i = 0; i < count; ++i) {
        if (!SDL_IsGameController(i)) {
            continue;
        }

        SDL_GameController *controller =
            SDL_GameControllerOpen(i);

        if (controller == NULL) {
            continue;
        }

        SDL_Joystick *joystick =
            SDL_GameControllerGetJoystick(
                controller
            );

        if (joystick == NULL) {
            SDL_GameControllerClose(
                controller
            );

            continue;
        }

        sXboxController = controller;

        sXboxControllerInstance =
            SDL_JoystickInstanceID(
                joystick
            );

        sXboxControllerEverConnected = true;

        return;
    }
}

static void controller_xbox_update_device(void) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        if (
            event.type ==
                SDL_CONTROLLERDEVICEREMOVED &&
            sXboxController != NULL &&
            event.cdevice.which ==
                sXboxControllerInstance
        ) {
            controller_xbox_close_current();
        }
    }

    if (sXboxController == NULL) {
        controller_xbox_open_first();
    }

    SDL_GameControllerUpdate();
}

static void controller_xbox_init(void) {
    if (
        SDL_Init(
            SDL_INIT_GAMECONTROLLER
        ) != 0
    ) {
        return;
    }

    SDL_GameControllerEventState(
        SDL_ENABLE
    );

    sXboxControllerInitialized = true;

#ifdef ENABLE_RUMBLE
    atexit(controller_xbox_shutdown);
#endif

    controller_xbox_open_first();
}

static void controller_xbox_read(
    OSContPad *pad
) {
    if (!sXboxControllerInitialized) {
        return;
    }

    controller_xbox_update_device();

    SDL_GameController *xpad =
        sXboxController;

    if (xpad == NULL) {
        if (sXboxControllerEverConnected) {
            pad->errnum = CONT_NO_RESPONSE_ERROR;
        }

        return;
    }

    const bool xpad_black =
        xbox_button_pressed(
            xpad,
            SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
        );

    const bool xpad_ltrig =
        xbox_trigger_pressed(
            xpad,
            SDL_CONTROLLER_AXIS_TRIGGERLEFT
        );

    const bool xpad_rtrig =
        xbox_trigger_pressed(
            xpad,
            SDL_CONTROLLER_AXIS_TRIGGERRIGHT
        );

    /*
     * Preserve the original dashboard reboot:
     *
     * Back + Black + left trigger + right trigger
     */
    if (
        xbox_button_pressed(
            xpad,
            SDL_CONTROLLER_BUTTON_BACK
        ) &&
        xpad_black &&
        xpad_ltrig &&
        xpad_rtrig
    ) {
#ifdef ENABLE_RUMBLE
        controller_xbox_set_rumble(FALSE, FALSE);
#endif
        XReboot();
    }

    /*
     * Preserve Baseline-B mappings.
     */

    if (
        xbox_button_pressed(
            xpad,
            SDL_CONTROLLER_BUTTON_A
        )
    ) {
        pad->button |= A_BUTTON;
    }

    if (
        xbox_button_pressed(
            xpad,
            SDL_CONTROLLER_BUTTON_X
        )
    ) {
        pad->button |= B_BUTTON;
    }

    if (
        xbox_button_pressed(
            xpad,
            SDL_CONTROLLER_BUTTON_LEFTSHOULDER
        )
    ) {
        pad->button |= L_TRIG;
    }

    if (
        xpad_black ||
        xpad_rtrig
    ) {
        pad->button |= R_TRIG;
    }

    if (xpad_ltrig) {
        pad->button |= Z_TRIG;
    }

    if (
        xbox_button_pressed(
            xpad,
            SDL_CONTROLLER_BUTTON_START
        )
    ) {
        pad->button |= START_BUTTON;
    }

    /*
     * nxdk SDL changes Xbox Y from its original signed
     * orientation to SDL orientation using bitwise NOT.
     *
     * Undo that transformation so the existing SM64 Xbox
     * behavior sees the same values as Baseline B.
     */

    const int16_t lx =
        SDL_GameControllerGetAxis(
            xpad,
            SDL_CONTROLLER_AXIS_LEFTX
        );

    const int16_t ly =
        (int16_t)~SDL_GameControllerGetAxis(
            xpad,
            SDL_CONTROLLER_AXIS_LEFTY
        );

    const int16_t rx =
        SDL_GameControllerGetAxis(
            xpad,
            SDL_CONTROLLER_AXIS_RIGHTX
        );

    const int16_t ry =
        (int16_t)~SDL_GameControllerGetAxis(
            xpad,
            SDL_CONTROLLER_AXIS_RIGHTY
        );

    /*
     * Treat the right stick like the N64's four discrete C buttons.
     *
     * Keep the existing 0x4000 cardinal activation distance, but use
     * a circular center dead zone so the four directional regions are
     * larger around the diagonals than the old per-axis square gate.
     *
     * Once outside the dead zone, only the dominant axis contributes
     * a C button. This prevents an ordinary diagonal stick position
     * from pressing two adjacent C buttons at once.
     */
    const uint32_t rx_abs =
        (rx < 0) ? (uint32_t)(-(int32_t)rx) : (uint32_t)rx;

    const uint32_t ry_abs =
        (ry < 0) ? (uint32_t)(-(int32_t)ry) : (uint32_t)ry;

    const uint32_t c_stick_magnitude_sq =
        rx_abs * rx_abs +
        ry_abs * ry_abs;

    if (
        c_stick_magnitude_sq >
            (uint32_t)C_STICK_DEADZONE *
            (uint32_t)C_STICK_DEADZONE
    ) {
        if (rx_abs >= ry_abs) {
            if (rx < 0) {
                pad->button |= L_CBUTTONS;
            } else {
                pad->button |= R_CBUTTONS;
            }
        } else {
            if (ry < 0) {
                pad->button |= D_CBUTTONS;
            } else {
                pad->button |= U_CBUTTONS;
            }
        }
    }

    const float lx_float = (float)lx;
    const float ly_float = (float)ly;

    const float magnitude =
        sqrtf(
            lx_float * lx_float +
            ly_float * ly_float
        );

    if (magnitude > STICK_DEADZONE) {
        float scale = 1.0f;

        if (magnitude < STICK_FULL_SCALE) {
            const float scaled_magnitude =
                (magnitude - STICK_DEADZONE) *
                STICK_FULL_SCALE /
                (STICK_FULL_SCALE - STICK_DEADZONE);

            scale =
                scaled_magnitude /
                magnitude;
        }

        pad->stick_x =
            (s8)(
                lx_float *
                scale /
                0x100
            );

        pad->stick_y =
            (s8)(
                ly_float *
                scale /
                0x100
            );
    }
}

struct ControllerAPI controller_xbox = {
    controller_xbox_init,
    controller_xbox_read
};

#endif