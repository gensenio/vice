/* Headless capture adapter for the unmodified plus4emu C API.
 * Build with plus4lib headers and libplus4emu; no emulator internals used.
 */
#include "plus4emu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

struct Capture {
    Plus4VideoDecoder *decoder;
    unsigned char pixels[384 * 288 * 3];
    unsigned char complete[384 * 288 * 3];
    unsigned frames;
};

static void line(void *opaque, int number, const Plus4VideoLineData *data)
{
    Capture *c = static_cast<Capture *>(opaque);
    if (number >= 0 && number < 576) {
        Plus4VideoDecoder_DecodeLine(c->decoder,
            c->pixels + (number / 2) * 384 * 3, 384, 1, data);
    }
}

static void frame(void *opaque)
{
    Capture *c = static_cast<Capture *>(opaque);
    std::memcpy(c->complete, c->pixels, sizeof(c->pixels));
    c->frames++;
}

static void check(Plus4VM *vm, Plus4Emu_Error result)
{
    if (result != PLUS4EMU_SUCCESS) {
        std::fprintf(stderr, "%s\n", Plus4VM_GetLastErrorMessage(vm));
        std::exit(1);
    }
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        std::fprintf(stderr, "Usage: %s ROMDIR PRG|- DISK|- SECONDS OUTPUT.rgb\n", argv[0]);
        return 2;
    }
    int seconds = std::atoi(argv[4]);
    if (seconds < 1 || seconds > 600) return 2;
    Capture c = {};
    Plus4VM *vm = Plus4VM_Create();
    if (!vm) return 1;
    c.decoder = Plus4VideoDecoder_Create(line, frame, &c);
    if (!c.decoder) return 1;
    Plus4VM_SetVideoOutputCallback(vm, Plus4VideoDecoder_VideoCallback, c.decoder);
    Plus4VM_SetEnableAudioOutput(vm, 0);
    Plus4VM_SetVideoClockFrequency(vm, 17734475);
    Plus4VM_SetCPUFrequency(vm, 1);
    check(vm, Plus4VM_SetRAMConfiguration(vm, 64, 0x99999999ULL));
    const char *roms[] = {"p4_basic.rom", "p4kernal.rom", "dos1541.rom"};
    const int segments[] = {0, 1, 0x10};
    for (int i = 0; i < 3; i++) {
        std::string path = std::string(argv[1]) + "/" + roms[i];
        check(vm, Plus4VM_LoadROM(vm, segments[i], path.c_str(), 0));
    }
    for (int i = 0; i < 4; i++)
        check(vm, Plus4VM_SetDiskImageFile(vm, i, "", 0));
    if (std::strcmp(argv[3], "-"))
        check(vm, Plus4VM_SetDiskImageFile(vm, 0, argv[3], 0));
    Plus4VM_Reset(vm, 1);
    check(vm, Plus4VM_Run(vm, 2000000));
    if (std::strcmp(argv[2], "-")) {
        check(vm, Plus4VM_LoadProgram(vm, argv[2]));
        const char *run = "RUN\r";
        for (unsigned i = 0; i < 4; i++)
            Plus4VM_WriteMemory(vm, 0x0527 + i, run[i], 1);
        Plus4VM_WriteMemory(vm, 0x00ef, 4, 1);
    }
    FILE *output = std::fopen(argv[5], "wb");
    if (!output) return 1;
    // Four completed frames per emulated second. Never capture half a frame.
    for (int i = 0; i < seconds * 4; i++) {
        check(vm, Plus4VM_Run(vm, 250000));
        if (std::fwrite(c.complete, 1, sizeof(c.complete), output) != sizeof(c.complete))
            return 1;
    }
    std::fclose(output);
    std::fprintf(stderr, "%u completed video frames; PC=%04x\n",
                 c.frames, Plus4VM_GetProgramCounter(vm));
    Plus4VM_Destroy(vm);
    Plus4VideoDecoder_Destroy(c.decoder);
    return 0;
}
