#ifndef LIBTODMVI_PRIVATE_H
#define LIBTODMVI_PRIVATE_H

#include "dmod.h"
#include "libtodmvi.h"

/* encode.c - pixels (0xAARRGGBB, `stride` apart) into the .dmvi file `output` */
int encode_file(const uint32_t* pixels, uint32_t width, uint32_t height, uint32_t stride, const char* output,
                const libtodmvi_options_t* options, libtodmvi_result_t* result);

#endif /* LIBTODMVI_PRIVATE_H */
