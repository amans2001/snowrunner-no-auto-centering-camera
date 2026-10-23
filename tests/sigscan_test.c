// Runs the camera-code search against a SnowRunner.exe dumped with its Steam DRM
// layer removed (the shipped exe's code is encrypted until the game starts).
// Usage: ./sigscan_test SnowRunner_unpacked.exe
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/sigscan.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s SnowRunner_unpacked.exe\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(n);
    if (fread(buf, 1, n, f) != (size_t)n) { perror("read"); return 2; }
    fclose(f);

    int count; long at = sig_find(buf, n, &count);
    printf("matches: %d\n", count);
    if (count != 1) { printf("FAIL: expected exactly one match\n"); return 1; }
    long hook = at + SIG_HOOK_AT;
    printf("hook instruction at file offset 0x%lX: ", hook);
    for (int i = 0; i < 8; i++) printf("%02X ", buf[hook + i]);
    printf("\n");
    static const unsigned char ORIG[8] = {0xF3,0x0F,0x11,0x83,0xE8,0x01,0x00,0x00};
    if (memcmp(buf + hook, ORIG, 8)) { printf("FAIL: not the expected instruction\n"); return 1; }

    // Same search must still find the spot once it's hooked (jmp + 3 nops).
    unsigned char saved[8]; memcpy(saved, buf + hook, 8);
    static const unsigned char J[8] = {0xE9,0x11,0x22,0x33,0x44,0x90,0x90,0x90};
    memcpy(buf + hook, J, 8);
    long at2 = sig_find(buf, n, &count);
    if (count != 1 || at2 != at) { printf("FAIL: hooked spot not found\n"); return 1; }
    memcpy(buf + hook, saved, 8);
    printf("PASS\n");
    return 0;
}
