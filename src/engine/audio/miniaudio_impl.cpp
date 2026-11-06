// stb_vorbis must be declared before miniaudio, which detects it through the header's include guard to enable .ogg
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
