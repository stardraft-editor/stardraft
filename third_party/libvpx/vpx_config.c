#include "vpx/vpx_codec.h"
static const char *const cfg = "--target=generic-gnu (StarDraft: C only, decoders)";
const char *vpx_codec_build_config(void) { return cfg; }
