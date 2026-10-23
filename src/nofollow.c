// SnowRunner No-Follow Camera - standalone trainer
// Keeps the third-person camera pointing the same way when the truck turns.
// F8 = on/off, close window = restore game code.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "hook_blob.h"
#include "sigscan.h"

#define VERSION      "v2.3"
#define OFF_RETADDR  0
#define OFF_MAGIC    8
#define OFF_ENABLED  16
#define OFF_ENTRY    96

static const unsigned char ORIG[8] = {0xF3,0x0F,0x11,0x83,0xE8,0x01,0x00,0x00};

static HANDLE g_proc = NULL;
static ULONG_PTR g_target = 0;
static ULONG_PTR g_blob = 0;
static int g_patched = 0;

static void say(const char *msg) { printf("%s\n", msg); fflush(stdout); }

static DWORD find_pid(void) {
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do { if (_stricmp(pe.szExeFile, "SnowRunner.exe") == 0) { pid = pe.th32ProcessID; break; } }
        while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static ULONG_PTR module_base(DWORD pid, DWORD *size) {
    ULONG_PTR base = 0;
    for (int tries = 0; tries < 10 && !base; tries++) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap == INVALID_HANDLE_VALUE) { Sleep(500); continue; }
        MODULEENTRY32 me; me.dwSize = sizeof(me);
        if (Module32First(snap, &me)) {
            do { if (_stricmp(me.szModule, "SnowRunner.exe") == 0) { base = (ULONG_PTR)me.modBaseAddr; *size = me.modBaseSize; break; } }
            while (Module32Next(snap, &me));
        }
        CloseHandle(snap);
    }
    return base;
}

// Finds the camera instruction by searching the game's code sections for the byte
// pattern in sigscan.h, so the tool keeps working when a game update moves the code.
// Returns its address, 0 if not found, or 1 if the pattern was found more than once.
static ULONG_PTR find_target(ULONG_PTR base, DWORD imageSize) {
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    if (!ReadProcessMemory(g_proc, (LPCVOID)base, &dos, sizeof(dos), NULL) || dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;
    if (!ReadProcessMemory(g_proc, (LPCVOID)(base + dos.e_lfanew), &nt, sizeof(nt), NULL) || nt.Signature != IMAGE_NT_SIGNATURE) return 0;
    int nsec = nt.FileHeader.NumberOfSections;
    ULONG_PTR secAddr = base + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
    IMAGE_SECTION_HEADER *secs = malloc(sizeof(IMAGE_SECTION_HEADER) * nsec);
    if (!secs || !ReadProcessMemory(g_proc, (LPCVOID)secAddr, secs, sizeof(IMAGE_SECTION_HEADER) * nsec, NULL)) { free(secs); return 0; }

    ULONG_PTR found = 0; int total = 0;
    for (int s = 0; s < nsec; s++) {
        if (!(secs[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        DWORD rva = secs[s].VirtualAddress, len = secs[s].Misc.VirtualSize;
        if (!len || rva + len > imageSize) continue;
        unsigned char *buf = malloc(len);
        if (!buf) continue;
        const DWORD CHUNK = 0x100000;                     // read in 1 MB pieces; zero any unreadable piece
        for (DWORD o = 0; o < len; o += CHUNK) {
            DWORD n = (len - o < CHUNK) ? len - o : CHUNK;
            if (!ReadProcessMemory(g_proc, (LPCVOID)(base + rva + o), buf + o, n, NULL)) memset(buf + o, 0, n);
        }
        int count = 0;
        long at = sig_find(buf, len, &count);
        if (count && !found) found = base + rva + at + SIG_HOOK_AT;
        total += count;
        free(buf);
    }
    free(secs);
    if (total > 1) return 1;
    return found;
}

static BOOL write_code(ULONG_PTR addr, const void *data, SIZE_T len) {
    DWORD old;
    if (!VirtualProtectEx(g_proc, (LPVOID)addr, len, PAGE_EXECUTE_READWRITE, &old)) return FALSE;
    BOOL ok = WriteProcessMemory(g_proc, (LPVOID)addr, data, len, NULL);
    VirtualProtectEx(g_proc, (LPVOID)addr, len, old, &old);
    FlushInstructionCache(g_proc, (LPCVOID)addr, len);
    return ok;
}

// Other SnowRunner mods (e.g. "SnowRunner time control") put their code at a fixed
// spot just below the game's base address without checking it's free. Never use
// that area, so both mods can run together in any order.
#define KEEP_FREE_BELOW_BASE 0x40000

static ULONG_PTR alloc_near(ULONG_PTR target, ULONG_PTR base) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    ULONG_PTR gran = si.dwAllocationGranularity;
    ULONG_PTR start = target & ~(gran - 1);
    for (ULONG_PTR d = gran; d < 0x7FF00000; d += gran) {
        ULONG_PTR cands[2] = { start - d, start + d };
        for (int i = 0; i < 2; i++) {
            if (cands[i] + gran > base - KEEP_FREE_BELOW_BASE && cands[i] < base) continue;
            LPVOID p = VirtualAllocEx(g_proc, (LPVOID)cands[i], 0x1000,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (p) {
                LONGLONG diff = (LONGLONG)((ULONG_PTR)p + OFF_ENTRY) - (LONGLONG)(target + 5);
                if (diff > -0x7FFF0000LL && diff < 0x7FFF0000LL) return (ULONG_PTR)p;
                VirtualFreeEx(g_proc, p, 0, MEM_RELEASE);
            }
        }
    }
    return 0;
}

static void set_byte(int off, unsigned char v) {
    WriteProcessMemory(g_proc, (LPVOID)(g_blob + off), &v, 1, NULL);
}

// Returns 1 = ready, 0 = fatal (message shown), -1 = retry later, -2 = camera code not found (yet)
static int install(DWORD pid) {
    g_proc = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
                         PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!g_proc) {
        say("Could not open SnowRunner. Try right-click -> Run as administrator.");
        return 0;
    }
    DWORD imageSize = 0;
    ULONG_PTR base = module_base(pid, &imageSize);
    if (!base) { CloseHandle(g_proc); g_proc = NULL; return -1; }
    g_target = find_target(base, imageSize);
    if (g_target == 1) {
        say("Found more than one possible camera spot in this SnowRunner version.");
        say("Nothing was changed. The fix needs updating for the new version.");
        return 0;
    }
    if (!g_target) { CloseHandle(g_proc); g_proc = NULL; return -2; }

    unsigned char cur[8];
    if (!ReadProcessMemory(g_proc, (LPCVOID)g_target, cur, 8, NULL)) {
        CloseHandle(g_proc); g_proc = NULL; return -1;
    }

    if (memcmp(cur, ORIG, 8) == 0) {
        g_blob = alloc_near(g_target, base);
        if (!g_blob) { say("Could not reserve memory in the game. Try restarting the game."); return 0; }
        unsigned char blob[sizeof(HOOK_BLOB)];
        memcpy(blob, HOOK_BLOB, sizeof(blob));
        ULONG_PTR ret = g_target + 8;
        memcpy(blob + OFF_RETADDR, &ret, 8);
        if (!WriteProcessMemory(g_proc, (LPVOID)g_blob, blob, sizeof(blob), NULL)) {
            say("Could not write to the game's memory. Try Run as administrator."); return 0;
        }
        FlushInstructionCache(g_proc, (LPCVOID)g_blob, sizeof(blob));
        unsigned char patch[8] = {0xE9,0,0,0,0,0x90,0x90,0x90};
        LONG rel = (LONG)((LONGLONG)(g_blob + OFF_ENTRY) - (LONGLONG)(g_target + 5));
        memcpy(patch + 1, &rel, 4);
        if (!write_code(g_target, patch, 8)) {
            say("Could not patch the game. Try Run as administrator."); return 0;
        }
        g_patched = 1;
        return 1;
    }

    if (cur[0] == 0xE9) {
        LONG rel; memcpy(&rel, cur + 1, 4);
        ULONG_PTR entry = g_target + 5 + rel;
        char m[8] = {0};
        if (ReadProcessMemory(g_proc, (LPCVOID)(entry - OFF_ENTRY + OFF_MAGIC), m, 8, NULL) &&
            memcmp(m, "SRNFCAM2", 8) == 0) {
            g_blob = entry - OFF_ENTRY;          // our hook from an earlier run: reuse it
            g_patched = 1;
            return 1;
        }
        say("The camera code is already modified by something else (probably Cheat Engine).");
        say("Please restart SnowRunner, then start this program again.");
        return 0;
    }

    say("This SnowRunner version doesn't match (the game was probably updated).");
    say("Nothing was changed. The fix needs updating for the new version.");
    return 0;
}

static void uninstall(void) {
    if (g_proc && g_patched) {
        write_code(g_target, ORIG, 8);   // hook memory is left in place (harmless) so nothing can crash
        g_patched = 0;
    }
}

static BOOL WINAPI on_close(DWORD type) {
    (void)type;
    uninstall();
    return FALSE;
}

int main(void) {
    SetConsoleTitleA("SnowRunner No-Follow Camera " VERSION);
    SetConsoleCtrlHandler(on_close, TRUE);
    say("========================================");
    say(" SnowRunner No-Follow Camera " VERSION);
    say("========================================");
    say(" F8  = camera fix ON / OFF");
    say(" Close this window to switch everything back to normal.");
    say(" Single-player only.");
    say("");

    if (!RegisterHotKey(NULL, 1, MOD_NOREPEAT, VK_F8))
        say("Note: F8 is in use by another program, so the hotkey may not work.");

    int enabled = 1, waitingShown = 0, notFoundTries = 0;
    for (;;) {
        DWORD pid = find_pid();
        if (!pid) {
            if (!waitingShown) { say("Waiting for SnowRunner to start..."); waitingShown = 1; }
            MSG msg; while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {}
            Sleep(2000);
            continue;
        }
        if (waitingShown) Sleep(3000);       // game just launched: let it finish starting
        int r = install(pid);
        if (r == -2 && ++notFoundTries >= 10) {            // ~20 s: the game is fully started by now
            say("This SnowRunner version doesn't match (the game was probably updated).");
            say("Nothing was changed. The fix needs updating for the new version.");
            r = 0;
        }
        if (r == 0) { say(""); say("Press Enter to close."); getchar(); return 1; }
        if (r < 0) { Sleep(2000); continue; }
        waitingShown = 0; notFoundTries = 0;

        set_byte(OFF_ENABLED, (unsigned char)enabled);
        say(enabled ? ">> Camera fix is ON  (F8 to turn off)" : ">> Camera fix is OFF (F8 to turn on)");

        for (;;) {
            DWORD w = MsgWaitForMultipleObjects(1, &g_proc, FALSE, INFINITE, QS_ALLINPUT);
            if (w == WAIT_OBJECT_0) break;                 // game closed
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message != WM_HOTKEY) continue;
                if (msg.wParam == 1) {
                    enabled = !enabled;
                    set_byte(OFF_ENABLED, (unsigned char)enabled);
                    say(enabled ? ">> Camera fix is ON" : ">> Camera fix is OFF");
                    Beep(enabled ? 880 : 440, 120);
                }
            }
        }
        say("SnowRunner closed.");
        CloseHandle(g_proc); g_proc = NULL; g_patched = 0; g_blob = 0;
    }
}
