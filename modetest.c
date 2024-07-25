#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <libpad.h>

static char padBuf[256] __attribute__((aligned(64)));
static char actAlign[6];
static int actuators;

int port, slot;
u32 old_pad = 0;

struct SMode {
    const char *sMode;
    s16 Mode;
    s16 Interlace;
    s16 Field;
    int Width;
    int Height;
};

struct SMode modes[] = {
    // NTSC
    { "480i", GS_MODE_NTSC,      GS_INTERLACED,    GS_FIELD,  640,  448},
    { "480p", GS_MODE_DTV_480P,  GS_NONINTERLACED, GS_FRAME,  640,  448},
    { "480i", GS_MODE_NTSC,      GS_INTERLACED,    GS_FIELD,  512,  448},
    { "480p", GS_MODE_DTV_480P,  GS_NONINTERLACED, GS_FRAME,  512,  448},
    // PAL
    { "576i", GS_MODE_PAL,       GS_INTERLACED,    GS_FIELD,  640,  512},
    { "576i", GS_MODE_PAL,       GS_INTERLACED,    GS_FIELD,  512,  512},
    // PS1
    { "240p", GS_MODE_NTSC,      GS_NONINTERLACED, GS_FRAME,  512,  240},
    { "240p", GS_MODE_NTSC,      GS_NONINTERLACED, GS_FRAME,  320,  240},
    { "240p", GS_MODE_NTSC,      GS_NONINTERLACED, GS_FRAME,  256,  240},
};

int iCurrentMode = 0;
struct SMode *pCurrentMode = &modes[0];
int iModeChange = 1;

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

// Print display mode
void print_mode(GSGLOBAL *gsGlobal) {
    printf("Mode: %s %dx%d %s memory: %dKiB\n",
           pCurrentMode->sMode,
           gsGlobal->Width,
           gsGlobal->Height,
           pCurrentMode->Field == GS_FRAME ? "GS_FRAME" : "GS_FIELD",
           gsGlobal->CurrentPointer / 1024);
}

// Render function
void render(GSGLOBAL *gsGlobal) {
    const u64 clWhite = GS_SETREG_RGBAQ(255, 255, 255, 0, 0);
    const u64 clBlack = GS_SETREG_RGBAQ(0, 0, 0, 0, 0);

    gsKit_queue_reset(gsGlobal->Per_Queue);

    gsKit_mode_switch(gsGlobal, GS_PERSISTENT);
    gsKit_clear(gsGlobal, clBlack);

    float pixelSize = 1.0f;

    for (float y = 0; y < gsGlobal->Height; y += pixelSize) {
        for (float x = 0; x < gsGlobal->Width; x += pixelSize) {
            u64 color = ((int)x + (int)y) % 2 == 0 ? clWhite : clBlack;
            gsKit_prim_sprite(gsGlobal, x, y, x + pixelSize, y + pixelSize, 1, color);
        }
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

        if (new_pad & PAD_R1) {
            iCurrentMode = (iCurrentMode + 1) % 9;
            iModeChange = 1;
        }
        if (new_pad & PAD_L1) {
            iCurrentMode = (iCurrentMode + 8) % 9;  // To wrap around correctly when decrementing
            iModeChange = 1;
        }
    }
}

int main(int argc, char *argv[]) {
    GSGLOBAL *gsGlobal = gsKit_init_global();

    pad_init();

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsGlobal->DoubleBuffering = GS_SETTING_OFF;
    gsGlobal->ZBuffering = GS_SETTING_OFF;

    while (1) {
        if (iModeChange != 0) {
            iModeChange = 0;
            pCurrentMode = &modes[iCurrentMode];

            gsGlobal->PSM = GS_PSM_CT16;
            gsGlobal->PSMZ = GS_PSMZ_16;
            gsGlobal->Mode = pCurrentMode->Mode;
            gsGlobal->Interlace = pCurrentMode->Interlace;
            gsGlobal->Field = pCurrentMode->Field;
            gsGlobal->Width = pCurrentMode->Width;
            gsGlobal->Height = (pCurrentMode->Interlace == GS_INTERLACED && pCurrentMode->Field == GS_FRAME) ?
                               pCurrentMode->Height / 2 : pCurrentMode->Height;

            gsKit_vram_clear(gsGlobal);
            gsKit_init_screen(gsGlobal);

            print_mode(gsGlobal);

            render(gsGlobal);
        }

        gsKit_sync_flip(gsGlobal);
        get_pad(gsGlobal);
    }

    return 0;
}