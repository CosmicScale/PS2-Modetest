#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <libpad.h>
#include <gsToolkit.h>
#include <string.h>
#include <timer.h>
#include "font.h"

#define VIDEO_MODES_COUNT 5
#define PATTERN_MODES_COUNT 5

extern void custom_gsKit_font_print_scaled(GSGLOBAL *gsGlobal, GSFONT *gsFont, float X, float Y, int Z,
                                           float scaleX, float scaleY, unsigned long color, const char *String);

extern unsigned char _binary_unscii_8_fnt_start[];
extern const unsigned char _binary_unscii_8_fnt_size[];

static char padBuf[256] __attribute__((aligned(64)));
static char actAlign[6];
static int actuators;

int port, slot;
u32 old_pad = 0;

struct SVideoMode {
    const char *sVideoMode;
    s16 Mode;
    s16 Interlace;
    s16 Field;
    u16 Height;
};

enum Pattern {
    CHECKERBOARD,
    WHITESCREEN
};

struct SPatternMode {
    enum Pattern Pattern;
    u16 Width;
};

struct SVideoMode videoModes[VIDEO_MODES_COUNT] = {
    // NTSC
    { "480i", GS_MODE_NTSC,      GS_INTERLACED,    GS_FIELD,  448},
    { "480p", GS_MODE_DTV_480P,  GS_NONINTERLACED, GS_FRAME,  448},
    { "240p", GS_MODE_NTSC,      GS_NONINTERLACED, GS_FRAME,  240},
    // PAL
    { "576i", GS_MODE_PAL,       GS_INTERLACED,    GS_FIELD,  512},
    { "288p", GS_MODE_PAL,       GS_NONINTERLACED, GS_FRAME,  288}
};

struct SPatternMode patternModes[PATTERN_MODES_COUNT] = {
    { CHECKERBOARD, 640},
    { CHECKERBOARD, 512},
    { CHECKERBOARD, 320},
    { CHECKERBOARD, 256},
    { WHITESCREEN, 512}
};

u8 iCurrentVideoMode = 0;
struct SVideoMode *pCurrentVideoMode = &videoModes[0];
u8 iCurrentPatternMode = 0;
struct SPatternMode *pCurrentPatternMode = &patternModes[0];
u8 iModeChange = 1;

// Load Modules
static void loadModules(void) {
    if (SifLoadModule("rom0:SIO2MAN", 0, NULL) < 0 || 
        SifLoadModule("rom0:PADMAN", 0, NULL) < 0) {
        SleepThread();
    }
}

// Wait for Pad to be ready
static void waitPadReady(int port, int slot) {
    int state;
    do {
        state = padGetState(port, slot);
    } while (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1);
}

// Initialize Pad
void pad_init() {
    SifInitRpc(0);
    loadModules();
    padInit(0);

    port = 0; // 0 -> Connector 1, 1 -> Connector 2
    slot = 0; // Always zero if not using multitap

    if (padPortOpen(port, slot, padBuf) == 0) {
        SleepThread();
    }

    waitPadReady(port, slot);

    int modes = padInfoMode(port, slot, PAD_MODETABLE, -1);
    if (modes == 0) return;

    for (int i = 0; i < modes; i++) {
        if (padInfoMode(port, slot, PAD_MODETABLE, i) == PAD_TYPE_DUALSHOCK) {
            break;
        }
        if (i >= modes) return;
    }

    if (padInfoMode(port, slot, PAD_MODECUREXID, 0) == 0) {
        return;
    }

    padSetMainMode(port, slot, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
    waitPadReady(port, slot);
    actuators = padInfoAct(port, slot, -1, 0);

    if (actuators != 0) {
        actAlign[0] = 0;
        actAlign[1] = 1;
        for (int i = 2; i < 6; i++) actAlign[i] = 0xff;

        waitPadReady(port, slot);
        padSetActAlign(port, slot, actAlign);
    }

    waitPadReady(port, slot);
}

const char *current_pattern_label() {
    switch (pCurrentPatternMode->Pattern) {
        case(CHECKERBOARD): return "checkerboard";
        case(WHITESCREEN): return "whitescreen";
        default: return "unexpected";
    }
}

// Print display mode
void print_mode(GSGLOBAL *gsGlobal) {
    printf("Mode: %s %s %dx%d %s memory: %dKiB\n",
           current_pattern_label(),
           pCurrentVideoMode->sVideoMode,
           gsGlobal->Width,
           gsGlobal->Height,
           pCurrentVideoMode->Field == GS_FRAME ? "GS_FRAME" : "GS_FIELD",
           gsGlobal->CurrentPointer / 1024);
}

void flush_queue(GSGLOBAL *gsGlobal) {
    gsKit_queue_exec(gsGlobal);
    gsKit_finish();
    gsKit_queue_reset(gsGlobal->Per_Queue);
}

const u64 clWhite = GS_SETREG_RGBAQ(255, 255, 255, 0, 0);
const u64 clBlack = GS_SETREG_RGBAQ(0, 0, 0, 0, 0);
const u64 clYellow = GS_SETREG_RGBAQ(255, 255, 0, 0, 0);

char mode_str[30];

void show_mode(GSGLOBAL *gsGlobal, GSFONT *gsFont) {
    sprintf(mode_str, "%s %s %dx%d",
            pCurrentVideoMode->sVideoMode,
            current_pattern_label(),
            gsGlobal->Width,
            gsGlobal->Height);

    float native_box_width = 210.0f;
    float box_proportional_width = native_box_width / 256.0f;
    float box_width = gsGlobal->Width * box_proportional_width;
    float scale_x = box_width / native_box_width;

    float native_box_height = 10.0f;
    float box_proportional_height = native_box_height / native_box_width;
    float box_height = gsGlobal->Height * box_proportional_height;
    float scale_y = box_height / native_box_height;

    float box_x1 = (gsGlobal->Width - box_width) / 2;
    float box_x2 = gsGlobal->Width - box_x1;
    float box_y1 = (gsGlobal->Height - box_height) / 2;
    float box_y2 = gsGlobal->Height - box_y1;
    gsKit_prim_sprite(gsGlobal, box_x1, box_y1, box_x2, box_y2, 2, clBlack);

    float native_text_width = gsFont->CharWidth * strlen(mode_str);
    float native_text_height = gsFont->CharHeight;
    float text_width = native_text_width * scale_x;
    float text_height = native_text_height * scale_y;
    float text_x = (gsGlobal->Width / 2.0f) - (text_width / 2.0f);
    float text_y = (gsGlobal->Height / 2.0f) - (text_height / 2.0f);
    custom_gsKit_font_print_scaled(gsGlobal, gsFont, text_x, text_y, 3, scale_x, scale_y, clYellow, mode_str);
}

void draw_checkerboard(GSGLOBAL *gsGlobal) {
    for (u16 y = 0; y < gsGlobal->Height; y++) {
        for (u16 x = 0; x < gsGlobal->Width; x++) {
            u64 color = (x + y) % 2 == 0 ? clWhite : clBlack;
            gsKit_prim_point(gsGlobal, x, y, 1, color);
        }
        if ((y > 0) && ((y % 10) == 0)) flush_queue(gsGlobal);
    }
}

void draw_whitescreen(GSGLOBAL *gsGlobal) {
    gsKit_clear(gsGlobal, clWhite);
}

u8 iShowMode = 1;
u8 iModeVisibilityChange = 0;
s32 hideModeTimerId = -1;

u64 hide_mode(s32 id, u64 scheduled_time, u64 actual_time, void *arg, void *pc_value) {
    iShowMode = 0;
    iModeVisibilityChange = 1;
    return 0;
}

void set_hide_mode_timer() {
    hideModeTimerId = AllocTimerCounter();
    StartTimerCounter(hideModeTimerId);
    SetTimerHandler(hideModeTimerId, Sec2TimerBusClock(3), hide_mode, NULL);
}

void unset_hide_mode_timer() {
    StopTimerCounter(hideModeTimerId);
    FreeTimerCounter(hideModeTimerId);
}

// Render function
void render(GSGLOBAL *gsGlobal, GSFONT *gsFont) {
    gsKit_mode_switch(gsGlobal, GS_PERSISTENT);
    gsKit_queue_reset(gsGlobal->Per_Queue);

    switch (pCurrentPatternMode->Pattern) {
        case(CHECKERBOARD):
            draw_checkerboard(gsGlobal);
            break;
        case(WHITESCREEN):
            draw_whitescreen(gsGlobal);
            break;
    }

    if (iShowMode) {
        flush_queue(gsGlobal);
        show_mode(gsGlobal, gsFont);
    }

    gsKit_queue_exec(gsGlobal);
}

// Get Pad Input
void get_pad(GSGLOBAL *gsGlobal) {
    struct padButtonStatus buttons;
    u32 paddata;
    u32 new_pad;
    int ret;

    do {
        ret = padGetState(port, slot);
    } while (ret != PAD_STATE_STABLE && ret != PAD_STATE_FINDCTP1);

    if (padRead(port, slot, &buttons)) {
        paddata = 0xffff ^ buttons.btns;
        new_pad = paddata & ~old_pad;
        old_pad = paddata;

        if (new_pad & PAD_R2) {
            iCurrentVideoMode = (iCurrentVideoMode + 1) % VIDEO_MODES_COUNT;
            iModeChange = 1;
        }
        if (new_pad & PAD_L2) {
            iCurrentVideoMode = (iCurrentVideoMode + (VIDEO_MODES_COUNT - 1)) % VIDEO_MODES_COUNT;
            iModeChange = 1;
        }
        if (new_pad & PAD_CIRCLE) {
            iCurrentPatternMode = (iCurrentPatternMode + 1) % PATTERN_MODES_COUNT;
            iModeChange = 1;
        }
        if (new_pad & PAD_SQUARE) {
            iCurrentPatternMode = (iCurrentPatternMode + (PATTERN_MODES_COUNT - 1)) % PATTERN_MODES_COUNT;
            iModeChange = 1;
        }
        if (new_pad & PAD_CROSS) {
            iShowMode = 1;
            iModeVisibilityChange = 1;
            unset_hide_mode_timer();
            set_hide_mode_timer();
        }
    }
}

int main(int argc, char *argv[]) {
    GSGLOBAL *gsGlobal = gsKit_init_global();
    GSFONT *gsFont = gsKit_init_font_raw(GSKIT_FTYPE_FNT, _binary_unscii_8_fnt_start, (int)_binary_unscii_8_fnt_size);

    pad_init();

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsGlobal->DoubleBuffering = GS_SETTING_OFF;
    gsGlobal->ZBuffering = GS_SETTING_OFF;

    while (1) {
        if (iModeChange) {
            iModeChange = 0;
            iShowMode = 1;
            unset_hide_mode_timer();

            pCurrentVideoMode = &videoModes[iCurrentVideoMode];
            pCurrentPatternMode = &patternModes[iCurrentPatternMode];

            gsGlobal->PSM = GS_PSM_CT16;
            gsGlobal->PSMZ = GS_PSMZ_16;
            gsGlobal->Mode = pCurrentVideoMode->Mode;
            gsGlobal->Interlace = pCurrentVideoMode->Interlace;
            gsGlobal->Field = pCurrentVideoMode->Field;
            gsGlobal->Width = pCurrentPatternMode->Width;
            gsGlobal->Height = (pCurrentVideoMode->Interlace == GS_INTERLACED && pCurrentVideoMode->Field == GS_FRAME) ?
                               pCurrentVideoMode->Height / 2 : pCurrentVideoMode->Height;

            gsKit_vram_clear(gsGlobal);
            gsKit_init_screen(gsGlobal);
            gsKit_font_upload_raw(gsGlobal, gsFont);

            print_mode(gsGlobal);
            render(gsGlobal, gsFont);

            set_hide_mode_timer();
        }

        if (iModeVisibilityChange) {
            iModeVisibilityChange = 0;
            render(gsGlobal, gsFont);
        }

        gsKit_sync_flip(gsGlobal);
        get_pad(gsGlobal);
    }

    return 0;
}