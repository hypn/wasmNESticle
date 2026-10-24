# vendor/ — third-party pieces NOT in the leaked NESticle source

The leaked NESticle tree contains only the *headers* of its GUI toolkit (GUI.LIB, "a32") and not
`gui.vol` (fonts, cursor, palette, icons). Both were recovered by
[MacDue/NesticlePortSDL2](https://github.com/MacDue/NesticlePortSDL2) (manual decompilation of the
leaked object files). They are copied here unchanged apart from the port fixes noted below, so the
port can show the real toolkit.

| file | origin |
|------|--------|
| `gui.cpp`, `guirect.cpp`, `guimenu.cpp`, `guiedit.cpp` | `NesticlePortSDL2/src/` |
| `../assets/gui.vol` | `NesticlePortSDL2/gui.vol` (4 control icons are 16-byte placeholders there) |
| `../assets/NESticle.pal` | `NesticlePortSDL2/anes.pal`: the NES colour table (8-bit RGB triples), the file the original loaded as `NESticle.pal` |

That repo carries no license file; check with its author before redistributing. Everything else in
`wasm/` is written for this port or derived from the leaked NESticle sources (see `src/README.md`).
