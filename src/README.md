# src/ — NESticle sources, patched for WebAssembly

These are the files from the leaked 1997 NESticle source tree (Bloodlust Software) that the build
actually uses (55 files), copied with lower-case names and LF line endings, **with the port's fixes
already applied**. There is no patch layer; edit these files directly.

Left out on purpose: the DOS/Win95 platform glue and x86 assembly (replaced by `../port/`), the
unrelated NetBIOS-kit files that sit in the leaked tree (`README`, `INSTALL`, `EXAM`, `VULNS.H`, …),
empty headers, backups, zips and IDE files. `FILE.CPP` is replaced by `../port/file.cpp`.

Changes made to the original files:

| file | change |
|------|--------|
| `command.cpp`, `inputw.cpp`, `vol.cpp`, `nesvideo.cpp`, `slist.cpp`, `rom.cpp`, … | loop variables declared in `for (int i…)` but used after the loop (pre-standard scoping) are declared before it |
| `nesvideo.h`, `vol.h` | removed redundant `Class::` qualification on member declarations; `vol.h`: `#pragma pack(1)` on the 18-byte on-disk header |
| `command.cpp`, `mmc.cpp` | string literal → `char *` casts |
| `stddlg.cpp` | Watcom-only `_heapset` heap check replaced by a message |
| `dd.h` | `surface` holds a plain byte buffer instead of a DirectDraw surface |
| `message.cpp` | messages are also written to the console; `vsprintf` → `vsnprintf` (200-byte buffer) |
| `nesdlg.cpp` | File → Load ROM first asks the web page to open the browser's file picker |

Everything is compiled with `-funsigned-char` (the original compiler's default); see `../README.md`.
