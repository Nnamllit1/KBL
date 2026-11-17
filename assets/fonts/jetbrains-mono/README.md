# JetBrains Mono

The unmodified Regular TTF is from JetBrains Mono 2.304 and is licensed under
the included [SIL Open Font License](OFL.txt). The generated ASCII coverage data
retains that license. KBL's code remains under its own license.

`tools/bake_font.py` uses Pillow to generate 4-bit antialiased glyph coverage.
The checked-in C++ data is enough for normal builds; the running boot stage
requires no font parser or font library. Source and atlas hashes are recorded
in `baked.json`. The full font notice is also retained in the payload and can
be printed by the host tool with `kbl licenses`.
