// verblib (the Freeverb of miniaudio's reverb node example) compiled as C : audio_manager.cpp only sees its declarations and
// wraps it in its own node (ReverbBusNode), which adds the pre-delay and the filters the reverb zones ask for.
#define VERBLIB_IMPLEMENTATION
#include "miniaudio/extras/nodes/ma_reverb_node/verblib.h"
