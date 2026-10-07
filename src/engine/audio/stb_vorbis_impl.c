// stb_vorbis compiled as C : it does not build as C++ (its single-letter macros clash with system
// headers). miniaudio_impl.cpp only sees its declarations and enables .ogg decoding through them.
#include "miniaudio/extras/stb_vorbis.c"
