#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define BCDEC_IMPLEMENTATION
#define BCDEC_BC4BC5_PRECISE
#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_image_resize2.h"
#include "bcdec.h"

#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 1
#include "tinyexr.h"