# Survey helpers (not built by CMake)

Small one-off programs used to work out the file formats from the disc data: each reads an
install read-only and prints counts or dumps (e.g. `junc.cpp` junction records, `sig.cpp` signal
attachments, `jtrain.cpp` junction settings against session trains, `cmds.cpp` driver commands,
`press.cpp` brake pressures). They were kept in `/var/tmp/oldtr` on the development PC and are
copied here so they are not lost. Build one by hand against the library objects, e.g.

    g++ -std=c++20 -O2 -Isrc tools/dev/surveys/junc.cpp <the trainz_content objects> -pthread -o junc
    ./junc /path/to/install

`../shrink.py` compresses screenshots for `docs/screenshots/`; `../soundbars.py` draws a mono
16-bit WAV's loudness as a PNG.
