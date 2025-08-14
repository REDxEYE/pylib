#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
//#define STB_IMAGE_RESIZE_IMPLEMENTATION  // <-- Implementation comes from vtflib
#define BCDEC_IMPLEMENTATION
#define BCDEC_BC4BC5_PRECISE
#include "ext/stb_image.h"
#include "ext/stb_image_write.h"
#include "ext/stb_image_resize2.h"
#include "ext/bcdec.h"

#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 1
#include "tinyexr.h"