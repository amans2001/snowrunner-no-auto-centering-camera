# No Auto-Centering Camera (SnowRunner)

A small standalone Windows tool that stops SnowRunner's third-person camera from
auto-centering behind the truck when it turns. The camera holds the direction you
set; the right stick / mouse works as normal. **F8** toggles it on and off.

Nexus Mods page: _(link)_

## What it does (and doesn't do)

- Finds the running `SnowRunner.exe` and checks the 8 original bytes of one
  instruction (`movss [rbx+1E8h], xmm0` at `SnowRunner.exe+0x85CAE5`, the
  per-frame camera-offset write). If they don't match, it stops and changes nothing.
- Allocates one small block of memory inside the game, copies the hook code
  (`src/hook.asm`) into it, and redirects that one instruction to it.
- The hook measures how far the truck turned since the last frame and cancels it
  out of the camera offset. It pauses while the right stick is in use.
- Closing the program restores the original bytes.
- No files are written or modified. No network access. No other processes touched.

## Files

| Path | Purpose |
|---|---|
| `src/nofollow.c` | The Windows program (finds the game, installs/removes the hook, F8 hotkey) |
| `src/hook.asm` | The hook code that runs inside the game (NASM, position-independent) |
| `tools/blob2h.py` | Converts the assembled hook into a C header |
| `tests/sim.c` | Runs the real hook code against a simulated camera (Linux x86-64) |
| `build.sh` | One-command build |

## Build

On Linux (or WSL) with `nasm`, `python3` and the MinGW-w64 cross compiler:

```sh
sudo apt install nasm gcc-mingw-w64-x86-64 python3
./build.sh
```

Output: `build/SnowRunner_NoFollowCamera.exe`.

Optional test of the hook logic (Linux x86-64, after building):

```sh
gcc -O1 -o build/sim tests/sim.c -lm && ./build/sim
```

## Use

1. Start SnowRunner and load a save.
2. Run `SnowRunner_NoFollowCamera.exe`. Keep its window open.
3. F8 = on/off. Close the window to restore the game to normal.

Single-player only. Compatible with "SnowRunner time control" by EquDevel.

## License

MIT
