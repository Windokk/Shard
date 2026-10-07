// Single translation unit holding the miniaudio implementation, kept apart from audio_manager.cpp so
// its (huge) header only gets compiled once.

// stb_vorbis (implemented in stb_vorbis_impl.c) must be declared before miniaudio : miniaudio detects it
// through the header's include guard and enables .ogg decoding.
#include <stdio.h>
#define STB_VORBIS_HEADER_ONLY
extern "C" {
#include "miniaudio/extras/stb_vorbis.c"
}
#undef STB_VORBIS_HEADER_ONLY

#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"
