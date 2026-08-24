// NanoSVG implementation. Normally compiled into libslic3r_gui; bambu-cli
// doesn't link the GUI lib, but libslic3r references some NanoSVG symbols
// at link time. Defining NANOSVG_IMPLEMENTATION here exposes the impl.

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg/nanosvg.h>
#include <nanosvg/nanosvgrast.h>
