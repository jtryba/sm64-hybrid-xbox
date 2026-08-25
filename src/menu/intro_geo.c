#include <PR/ultratypes.h>

#include "engine/graph_node.h"
#include "game/memory.h"
#include "game/segment2.h"
#include "game/segment7.h"
#include "gfx_dimensions.h"
#include "intro_geo.h"
#include "sm64.h"
#include "textures.h"
#include "types.h"
#include "prevent_bss_reordering.h"

#ifdef ENABLE_SHINDOU_TITLE_EASTER_EGG
#include "audio/external.h"
#include "game/game_init.h"
#include "pc/gfx/gfx_xbox.h"
#endif

// frame counts for the zoom in, hold, and zoom out of title model
#define INTRO_STEPS_ZOOM_IN 20
#define INTRO_STEPS_HOLD_1 75
#define INTRO_STEPS_ZOOM_OUT 91

// background types
#define INTRO_BACKGROUND_SUPER_MARIO 0
#define INTRO_BACKGROUND_GAME_OVER 1

struct GraphNodeMore {
    /*0x00*/ struct GraphNode node;
    /*0x14*/ void *todo;
    /*0x18*/ u32 unk18;
};

// intro geo bss
s32 gGameOverFrameCounter;
s32 gGameOverTableIndex;
s16 gTitleZoomCounter;
s32 gTitleFadeCounter;

// intro screen background display lists for each of four 80x20 textures
const Gfx *introBackgroundDlRows[] = { title_screen_bg_dl_0A000130, title_screen_bg_dl_0A000148,
                                       title_screen_bg_dl_0A000160, title_screen_bg_dl_0A000178 };

// intro screen background texture X offsets
float introBackgroundOffsetX[] = {
    0.0, 80.0, 160.0, 240.0, 0.0, 80.0, 160.0, 240.0, 0.0, 80.0, 160.0, 240.0,
};

// intro screen background texture Y offsets
float introBackgroundOffsetY[] = {
    160.0, 160.0, 160.0, 160.0, 80.0, 80.0, 80.0, 80.0, 0.0, 0.0, 0.0, 0.0,
};

// table that points to either the "Super Mario 64" or "Game Over" tables
const u8 *const *introBackgroundTextureType[] = { mario_title_texture_table, game_over_texture_table };

s8 introBackgroundIndexTable[] = {
    INTRO_BACKGROUND_SUPER_MARIO, INTRO_BACKGROUND_SUPER_MARIO, INTRO_BACKGROUND_SUPER_MARIO,
    INTRO_BACKGROUND_SUPER_MARIO, INTRO_BACKGROUND_SUPER_MARIO, INTRO_BACKGROUND_SUPER_MARIO,
    INTRO_BACKGROUND_SUPER_MARIO, INTRO_BACKGROUND_SUPER_MARIO, INTRO_BACKGROUND_SUPER_MARIO,
    INTRO_BACKGROUND_SUPER_MARIO, INTRO_BACKGROUND_SUPER_MARIO, INTRO_BACKGROUND_SUPER_MARIO,
};

// only one table of indexes listed
s8 *introBackgroundTables[] = { introBackgroundIndexTable };

s8 gameOverBackgroundTable[] = {
    INTRO_BACKGROUND_GAME_OVER, INTRO_BACKGROUND_GAME_OVER, INTRO_BACKGROUND_GAME_OVER,
    INTRO_BACKGROUND_GAME_OVER, INTRO_BACKGROUND_GAME_OVER, INTRO_BACKGROUND_GAME_OVER,
    INTRO_BACKGROUND_GAME_OVER, INTRO_BACKGROUND_GAME_OVER, INTRO_BACKGROUND_GAME_OVER,
    INTRO_BACKGROUND_GAME_OVER, INTRO_BACKGROUND_GAME_OVER, INTRO_BACKGROUND_GAME_OVER,
};

// order of tiles that are flipped from "Game Over" to "Super Mario 64"
s8 gameOverBackgroundFlipOrder[] = { 0x00, 0x01, 0x02, 0x03, 0x07, 0x0B,
                                     0x0a, 0x09, 0x08, 0x04, 0x05, 0x06 };

Gfx *geo_title_screen(s32 sp50, struct GraphNode *sp54, UNUSED void *context) {
    struct GraphNode *graphNode; // sp4c
    Gfx *displayList;            // sp48
    Gfx *displayListIter;        // sp44
    Mtx *scaleMat;               // sp40
    f32 *scaleTable1;            // sp3c
    f32 *scaleTable2;            // sp38
    f32 scaleX;                  // sp34
    f32 scaleY;                  // sp30
    f32 scaleZ;                  // sp2c
    graphNode = sp54;
    displayList = NULL;
    displayListIter = NULL;
    scaleTable1 = segmented_to_virtual(intro_seg7_table_0700C790);
    scaleTable2 = segmented_to_virtual(intro_seg7_table_0700C880);
    if (sp50 != 1) {
        gTitleZoomCounter = 0;
    } else if (sp50 == 1) {
        graphNode->flags = (graphNode->flags & 0xFF) | 0x100;
        scaleMat = alloc_display_list(sizeof(*scaleMat));
        displayList = alloc_display_list(4 * sizeof(*displayList));
        displayListIter = displayList;
        if (gTitleZoomCounter >= 0 && gTitleZoomCounter < INTRO_STEPS_ZOOM_IN) {
            scaleX = scaleTable1[gTitleZoomCounter * 3];
            scaleY = scaleTable1[gTitleZoomCounter * 3 + 1];
            scaleZ = scaleTable1[gTitleZoomCounter * 3 + 2];
        } else if (gTitleZoomCounter >= INTRO_STEPS_ZOOM_IN && gTitleZoomCounter < INTRO_STEPS_HOLD_1) {
            scaleX = 1.0f;
            scaleY = 1.0f;
            scaleZ = 1.0f;
        } else if (gTitleZoomCounter >= INTRO_STEPS_HOLD_1
                   && gTitleZoomCounter < INTRO_STEPS_ZOOM_OUT) {
            scaleX = scaleTable2[(gTitleZoomCounter - INTRO_STEPS_HOLD_1) * 3];
            scaleY = scaleTable2[(gTitleZoomCounter - INTRO_STEPS_HOLD_1) * 3 + 1];
            scaleZ = scaleTable2[(gTitleZoomCounter - INTRO_STEPS_HOLD_1) * 3 + 2];
        } else {
            scaleX = 0.0f;
            scaleY = 0.0f;
            scaleZ = 0.0f;
        }
        guScale(scaleMat, scaleX, scaleY, scaleZ);
        gSPMatrix(displayListIter++, scaleMat, G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
        gSPDisplayList(displayListIter++, &intro_seg7_dl_0700B3A0);
        gSPPopMatrix(displayListIter++, G_MTX_MODELVIEW);
        gSPEndDisplayList(displayListIter);
        gTitleZoomCounter++;
    }
    return displayList;
}

Gfx *geo_fade_transition(s32 sp40, struct GraphNode *sp44, UNUSED void *context) {
    struct GraphNode *graphNode = sp44; // sp3c
    Gfx *displayList = NULL;            // sp38
    Gfx *displayListIter = NULL;        // sp34
    if (sp40 != 1) {
        gTitleFadeCounter = 0; // D_801B985C
    } else if (sp40 == 1) {
        displayList = alloc_display_list(5 * sizeof(*displayList));
        displayListIter = displayList;
        gSPDisplayList(displayListIter++, dl_proj_mtx_fullscreen);
        gDPSetEnvColor(displayListIter++, 255, 255, 255, gTitleFadeCounter);
        if (gTitleFadeCounter == 255) {
            if (0) {
            }
            graphNode->flags = (graphNode->flags & 0xFF) | 0x100;
            gDPSetRenderMode(displayListIter++, G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
        } else {
            graphNode->flags = (graphNode->flags & 0xFF) | 0x500;
            gDPSetRenderMode(displayListIter++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
            if (0) {
            }
        }
        gSPDisplayList(displayListIter++, &intro_seg7_dl_0700C6A0);
        gSPEndDisplayList(displayListIter);
        if (gTitleZoomCounter >= 0x13) {
            gTitleFadeCounter += 0x1a;
            if (gTitleFadeCounter >= 0x100) {
                gTitleFadeCounter = 0xFF;
            }
        }
    }
    return displayList;
}

static Gfx *intro_backdrop_one_image_at(f32 x, f32 y, s8 backgroundType) {
    Mtx *mtx;                         // sp5c
    Gfx *displayList;                 // sp58
    Gfx *displayListIter;             // sp54
    const u8 *const *vIntroBgTable;   // sp50
    s32 i;                            // sp4c
    mtx = alloc_display_list(sizeof(*mtx));
    displayList = alloc_display_list(36 * sizeof(*displayList));
    displayListIter = displayList;
    vIntroBgTable = segmented_to_virtual(introBackgroundTextureType[backgroundType]);
    guTranslate(mtx, x, y, 0.0f);
    gSPMatrix(displayListIter++, mtx, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH);
    gSPDisplayList(displayListIter++, &title_screen_bg_dl_0A000118);
    for (i = 0; i < 4; ++i) {
        gDPLoadTextureBlock(displayListIter++, vIntroBgTable[i], G_IM_FMT_RGBA, G_IM_SIZ_16b, 80, 20, 0, 
                            G_TX_CLAMP, G_TX_CLAMP, 7, 6, G_TX_NOLOD, G_TX_NOLOD)    
        gSPDisplayList(displayListIter++, introBackgroundDlRows[i]);
    }
    gSPPopMatrix(displayListIter++, G_MTX_MODELVIEW);
    gSPEndDisplayList(displayListIter);
    return displayList;
}

Gfx *intro_backdrop_one_image(s32 index, s8 *backgroundTable) {
    return intro_backdrop_one_image_at(introBackgroundOffsetX[index], introBackgroundOffsetY[index],
                                       backgroundTable[index]);
}

#ifdef WIDESCREEN

static s32 intro_backdrop_left_extra_columns(void) {
    f32 leftEdge = GFX_DIMENSIONS_FROM_LEFT_EDGE(0);

    if (leftEdge >= 0.0f) {
        return 0;
    }

    return (s32) ceilf(-leftEdge / 80.0f);
}

static s32 intro_backdrop_right_extra_columns(void) {
    f32 rightEdge = GFX_DIMENSIONS_FROM_RIGHT_EDGE(0);

    if (rightEdge <= SCREEN_WIDTH) {
        return 0;
    }

    return (s32) ceilf((rightEdge - SCREEN_WIDTH) / 80.0f);
}

static void intro_backdrop_append_widescreen_sides(Gfx **displayListIter, s8 *backgroundTable,
                                                   s32 leftColumns, s32 rightColumns) {
    s32 column;
    s32 row;

    for (column = 1; column <= leftColumns; ++column) {
        f32 x = -80.0f * column;

        for (row = 0; row < 3; ++row) {
            s32 edgeIndex = row * 4;
            gSPDisplayList((*displayListIter)++,
                           intro_backdrop_one_image_at(x, introBackgroundOffsetY[edgeIndex],
                                                       backgroundTable[edgeIndex]));
        }
    }

    for (column = 0; column < rightColumns; ++column) {
        f32 x = SCREEN_WIDTH + 80.0f * column;

        for (row = 0; row < 3; ++row) {
            s32 edgeIndex = row * 4 + 3;
            gSPDisplayList((*displayListIter)++,
                           intro_backdrop_one_image_at(x, introBackgroundOffsetY[edgeIndex],
                                                       backgroundTable[edgeIndex]));
        }
    }
}

#endif


Gfx *geo_intro_backdrop(s32 sp48, struct GraphNode *sp4c, UNUSED void *context) {
    struct GraphNodeMore *graphNode; // sp44
    s32 index;                       // sp40
    s8 *backgroundTable;             // sp3c
    Gfx *displayList;                // sp38
    Gfx *displayListIter;            // sp34
    s32 i;                           // sp30
#ifdef WIDESCREEN
    s32 leftColumns;
    s32 rightColumns;
#endif
    graphNode = (struct GraphNodeMore *) sp4c;
    index = graphNode->unk18 & 0xff; // TODO: word at offset 0x18 of struct GraphNode
    backgroundTable = introBackgroundTables[index];
    displayList = NULL;
    displayListIter = NULL;
    if (sp48 == 1) {
#ifdef WIDESCREEN
        leftColumns = intro_backdrop_left_extra_columns();
        rightColumns = intro_backdrop_right_extra_columns();
        displayList = alloc_display_list((16 + (leftColumns + rightColumns) * 3) * sizeof(*displayList));
#else
        displayList = alloc_display_list(16 * sizeof(*displayList));
#endif
        displayListIter = displayList;
        graphNode->node.flags = (graphNode->node.flags & 0xFF) | 0x100;
        gSPDisplayList(displayListIter++, &dl_proj_mtx_fullscreen);
        gSPDisplayList(displayListIter++, &title_screen_bg_dl_0A000100);
        for (i = 0; i < 12; ++i) {
            gSPDisplayList(displayListIter++, intro_backdrop_one_image(i, backgroundTable));
        }
#ifdef WIDESCREEN
        intro_backdrop_append_widescreen_sides(&displayListIter, backgroundTable, leftColumns, rightColumns);
#endif
        gSPDisplayList(displayListIter++, &title_screen_bg_dl_0A000190);
        gSPEndDisplayList(displayListIter);
    }
    return displayList;
}

Gfx *geo_game_over_tile(s32 sp40, struct GraphNode *sp44, UNUSED void *context) {
    struct GraphNode *graphNode; // sp3c
    Gfx *displayList;            // sp38
    Gfx *displayListIter;        // sp34
    s32 j;                       // sp30
    s32 i;                       // sp2c
#ifdef WIDESCREEN
    s32 leftColumns;
    s32 rightColumns;
#endif
    graphNode = sp44;
    displayList = NULL;
    displayListIter = NULL;
    if (sp40 != 1) {
        gGameOverFrameCounter = 0;
        gGameOverTableIndex = -2;
        for (i = 0; i < (s32) sizeof(gameOverBackgroundTable); ++i) {
            gameOverBackgroundTable[i] = INTRO_BACKGROUND_GAME_OVER;
        }
    } else {
#ifdef WIDESCREEN
        leftColumns = intro_backdrop_left_extra_columns();
        rightColumns = intro_backdrop_right_extra_columns();
        displayList = alloc_display_list((16 + (leftColumns + rightColumns) * 3) * sizeof(*displayList));
#else
        displayList = alloc_display_list(16 * sizeof(*displayList));
#endif
        displayListIter = displayList;
        if (gGameOverTableIndex == -2) {
            if (gGameOverFrameCounter == 180) {
                gGameOverTableIndex++;
                gGameOverFrameCounter = 0;
            }
        } else {
            // transition tile from "Game Over" to "Super Mario 64"
            if (gGameOverTableIndex != 11 && !(gGameOverFrameCounter & 0x1)) {
                gGameOverTableIndex++;
                gameOverBackgroundTable[gameOverBackgroundFlipOrder[gGameOverTableIndex]] =
                    INTRO_BACKGROUND_SUPER_MARIO;
            }
        }
        if (gGameOverTableIndex != 11) {
            gGameOverFrameCounter++;
        }
        graphNode->flags = (graphNode->flags & 0xFF) | 0x100;
        gSPDisplayList(displayListIter++, &dl_proj_mtx_fullscreen);
        gSPDisplayList(displayListIter++, &title_screen_bg_dl_0A000100);
        for (j = 0; j < (s32) sizeof(gameOverBackgroundTable); ++j) {
            gSPDisplayList(displayListIter++, intro_backdrop_one_image(j, gameOverBackgroundTable));
        }
#ifdef WIDESCREEN
        intro_backdrop_append_widescreen_sides(&displayListIter, gameOverBackgroundTable, leftColumns, rightColumns);
#endif
        gSPDisplayList(displayListIter++, &title_screen_bg_dl_0A000190);
        gSPEndDisplayList(displayListIter);
    }
    return displayList;
}

#ifdef ENABLE_SHINDOU_TITLE_EASTER_EGG

extern Gfx title_screen_bg_shindou_face_begin_dl[];
extern Gfx title_screen_bg_shindou_face_end_dl[];

static s8 sShindouFaceVisible[] = {
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 0, 0, 0, 0, 1, 1,
    1, 1, 0, 0, 0, 0, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
};

static s8 sShindouFaceToggleOrder[] = {
     0,  1,  2,  3,  4,  5,  6,  7,
    15, 23, 31, 39, 47, 46, 45, 44,
    43, 42, 41, 40, 32, 24, 16,  8,
     9, 10, 11, 12, 13, 14, 22, 30,
    38, 37, 36, 35, 34, 33, 25, 17,
};

static s8 sShindouFaceCounter;

static s32 intro_shindou_face_left_extra_columns(void) {
#ifdef WIDESCREEN
    f32 leftEdge = GFX_DIMENSIONS_FROM_LEFT_EDGE(0);

    if (leftEdge < 0.0f) {
        return (s32) ceilf(-leftEdge / 40.0f);
    }
#endif
    return 0;
}

static s32 intro_shindou_face_right_extra_columns(void) {
#ifdef WIDESCREEN
    f32 rightEdge = GFX_DIMENSIONS_FROM_RIGHT_EDGE(0);

    if (rightEdge > SCREEN_WIDTH) {
        return (s32) ceilf((rightEdge - SCREEN_WIDTH) / 40.0f);
    }
#endif
    return 0;
}

static void intro_gen_shindou_face_texrect(Gfx **displayListIter, s32 leftColumns, s32 rightColumns) {
    s32 column;
    s32 x;
    s32 y;

    for (y = 0; y < 6; ++y) {
        for (column = 1; column <= leftColumns; ++column) {
            if (sShindouFaceVisible[y * 8] != 0) {
                x = -40 * column;
                gSPTextureRectangle((*displayListIter)++, x << 2, (y * 40) << 2,
                                    (x + 39) << 2, (y * 40 + 39) << 2, 0,
                                    0, 0, 4 << 10, 1 << 10);
            }
        }

        for (x = 0; x < 8; ++x) {
            if (sShindouFaceVisible[y * 8 + x] != 0) {
                gSPTextureRectangle((*displayListIter)++, (x * 40) << 2, (y * 40) << 2,
                                    (x * 40 + 39) << 2, (y * 40 + 39) << 2, 0,
                                    0, 0, 4 << 10, 1 << 10);
            }
        }

        for (column = 0; column < rightColumns; ++column) {
            if (sShindouFaceVisible[y * 8 + 7] != 0) {
                x = SCREEN_WIDTH + 40 * column;
                gSPTextureRectangle((*displayListIter)++, x << 2, (y * 40) << 2,
                                    (x + 39) << 2, (y * 40 + 39) << 2, 0,
                                    0, 0, 4 << 10, 1 << 10);
            }
        }
    }
}

static Gfx *intro_draw_shindou_face(u16 *image, s32 imageW, s32 imageH) {
    Gfx *displayList;
    Gfx *displayListIter;
    s32 leftColumns;
    s32 rightColumns;

    leftColumns = intro_shindou_face_left_extra_columns();
    rightColumns = intro_shindou_face_right_extra_columns();

    displayList = alloc_display_list((136 + (leftColumns + rightColumns) * 18) * sizeof(*displayList));
    if (displayList == NULL) {
        return NULL;
    }

    displayListIter = displayList;

    gSPDisplayList(displayListIter++, title_screen_bg_shindou_face_begin_dl);
    gDPLoadTextureBlock(displayListIter++, image, G_IM_FMT_RGBA, G_IM_SIZ_16b, imageW, imageH, 0,
                        G_TX_CLAMP | G_TX_NOMIRROR, G_TX_CLAMP | G_TX_NOMIRROR,
                        6, 6, G_TX_NOLOD, G_TX_NOLOD);
    intro_gen_shindou_face_texrect(&displayListIter, leftColumns, rightColumns);
    gSPDisplayList(displayListIter++, title_screen_bg_shindou_face_end_dl);
    gSPEndDisplayList(displayListIter);

    return displayList;
}

static u16 *intro_sample_shindou_face_framebuffer(s32 imageW, s32 imageH, s32 sampleW, s32 sampleH) {
    u16 *image;

    image = alloc_display_list(imageW * imageH * sizeof(*image));
    if (image == NULL) {
        return NULL;
    }

    if (!gfx_xbox_capture_title_face_rgba16(image, imageW, imageH, sampleW, sampleH)) {
        return NULL;
    }

    return image;
}

Gfx *geo_intro_face_easter_egg(s32 state, struct GraphNode *graphNode, UNUSED void *context) {
    u16 *image;
    Gfx *displayList;
    s32 i;

    displayList = NULL;

    if (state == GEO_CONTEXT_CREATE) {
        sShindouFaceCounter = 0;
    }

    if (state != 1) {
        for (i = 0; i < 48; ++i) {
            sShindouFaceVisible[i] = 0;
        }
    } else {
        if (sShindouFaceCounter == 0) {
            if (gPlayer1Controller->buttonPressed & Z_TRIG) {
                play_sound(SOUND_MENU_STAR_SOUND, gDefaultSoundArgs);
                sShindouFaceVisible[0] ^= 1;
                sShindouFaceCounter++;
            }
        } else {
            sShindouFaceVisible[sShindouFaceToggleOrder[sShindouFaceCounter++]] ^= 1;
            if (sShindouFaceCounter >= 40) {
                sShindouFaceCounter = 0;
            }
        }

        if (sShindouFaceVisible[0] == 1 || sShindouFaceVisible[17] == 1) {
            image = intro_sample_shindou_face_framebuffer(40, 40, 2, 2);
            if (image != NULL) {
                graphNode->flags = (graphNode->flags & 0xFF) | 0x100;
                displayList = intro_draw_shindou_face(image, 40, 40);
            }
        }
    }

    return displayList;
}

#endif
