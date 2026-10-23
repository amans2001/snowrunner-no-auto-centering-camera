// Byte-pattern search used to find the camera instruction in SnowRunner.exe.
// Plain C with no Windows dependencies, so tests/sigscan_test.c can run it on Linux.
#ifndef SIGSCAN_H
#define SIGSCAN_H
#include <stddef.h>

// The per-frame camera-offset update inside SnowRunner's third-person camera code:
//   F3 0F 58 83 E8 01 00 00   addss  xmm0, [rbx+1E8h]
//   41 0F 2F ??               comiss xmm0, xmmN        (register may change between builds)
//   F3 0F 11 83 E8 01 00 00   movss  [rbx+1E8h], xmm0  <- hooked instruction
// SIG_HOOK_AT is where the hooked instruction starts inside the pattern.
#define SIG_LEN      20
#define SIG_HOOK_AT  12
static const short SIG[SIG_LEN] = {
    0xF3,0x0F,0x58,0x83,0xE8,0x01,0x00,0x00,
    0x41,0x0F,0x2F,  -1,
    0xF3,0x0F,0x11,0x83,0xE8,0x01,0x00,0x00
};

// Same spot after this tool has already hooked it (jmp rel32 + 3 nops).
static const short SIG_HOOKED[SIG_LEN] = {
    0xF3,0x0F,0x58,0x83,0xE8,0x01,0x00,0x00,
    0x41,0x0F,0x2F,  -1,
    0xE9,  -1,  -1,  -1,  -1,0x90,0x90,0x90
};

static int sig_match(const unsigned char *p, const short *sig) {
    for (int i = 0; i < SIG_LEN; i++)
        if (sig[i] >= 0 && p[i] != (unsigned char)sig[i]) return 0;
    return 1;
}

// Searches buf for either pattern. Returns the offset of the first match (start of
// the pattern) or -1. *count gets the total number of matches, so callers can refuse
// to patch when the pattern is not unique.
static long sig_find(const unsigned char *buf, size_t len, int *count) {
    long first = -1; int n = 0;
    if (len < SIG_LEN) { *count = 0; return -1; }
    for (size_t i = 0; i + SIG_LEN <= len; i++) {
        if (buf[i] != 0xF3) continue;
        if (sig_match(buf + i, SIG) || sig_match(buf + i, SIG_HOOKED)) {
            if (first < 0) first = (long)i;
            n++;
        }
    }
    *count = n;
    return first;
}

#endif
