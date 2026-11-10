# Menu fonts

Glyph atlases made with `scripts/genfont.py` (see SETTINGS.md section 9.1).

| File | Source font | License |
|---|---|---|
| `En.dat` (built in) and `menu/Sans.dat` | Selawik Regular 1.01 (`selawk.ttf`), https://github.com/microsoft/Selawik | SIL OFL 1.1, `OFL-Selawik.txt` |
| `menu/SansBold.dat` | Selawik Semibold 1.01 (`selawksb.ttf`) | SIL OFL 1.1, `OFL-Selawik.txt` |
| `menu/Serif.dat` | Liberation Serif Regular 2.1.5, https://github.com/liberationfonts/liberation-fonts | SIL OFL 1.1, `OFL-LiberationSerif.txt` |
| `menu/Classic.dat` | The original WiiSXRX menu glyphs | from the upstream project |

The Reserved Font Names (Selawik, Liberation) are not used for the files:
a glyph atlas is a modified version of the font.

The `MenuFont` values `Segoe`, `Times` and `CalibriBold` from WiiStation 5.x
still work: they select `Sans`, `Serif` and `SansBold`.

`Chs`, `De`, `Es`, `It`, `Kr`, `Pt` (`*.zip`) are the language fonts of the
upstream project; they were not changed.
