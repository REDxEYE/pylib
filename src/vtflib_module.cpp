#include "vtflib_module.h"
#include <cstring>

#include "detex.h"


void *vtf_load_vtf(char *src, size_t src_size) {
    VTFFile *vfile = new VTFFile((uint8_t *) src, src_size);
    return (void *) vfile;
}

uint32_t vtf_width(VTFFile *vfile) {
    return vfile->width();
}

uint32_t vtf_height(VTFFile *vfile) {
    return vfile->height();
}

VTFImageFormat vtf_image_format(VTFFile *vfile) {
    return vfile->image_format();
}

bool vtf_get_as_rgba8888(VTFFile *vfile, char *dst, size_t dst_size, bool flip) {
    size_t src_size;
    uint32_t width = vfile->width();
    uint32_t height = vfile->height();
    uint8_t *raw_pixel_data = vfile->get_highres_image(src_size);
    switch (vfile->image_format()) {
        case VTFImageFormat::DXT1:
            image_decode_bcn((char *) raw_pixel_data, src_size, dst, dst_size, (int32_t) width, (int32_t) height,
                             BCnMode::BC1, flip);
            break;
        case VTFImageFormat::DXT1_ONEBITALPHA:
            image_decode_bcn((char *) raw_pixel_data, src_size, dst, dst_size, (int32_t) width, (int32_t) height,
                             BCnMode::BC1A, flip);
            break;
        case VTFImageFormat::DXT3:
            image_decode_bcn((char *) raw_pixel_data, src_size, dst, dst_size, (int32_t) width, (int32_t) height,
                             BCnMode::BC2, flip);
            break;
        case VTFImageFormat::DXT5:
            image_decode_bcn((char *) raw_pixel_data, src_size, dst, dst_size, (int32_t) width, (int32_t) height,
                             BCnMode::BC3, flip);
            break;
        case VTFImageFormat::ATI1N:
            image_decode_bcn((char *) raw_pixel_data, src_size, dst, dst_size, (int32_t) width, (int32_t) height,
                             BCnMode::BC4, flip);
            break;
        case VTFImageFormat::ATI2N:
            image_decode_bcn((char *) raw_pixel_data, src_size, dst, dst_size, (int32_t) width, (int32_t) height,
                             BCnMode::BC5, flip);
            break;
        case VTFImageFormat::BGR565:
        case VTFImageFormat::BGRX5551:
        case VTFImageFormat::BGRA4444:
        case VTFImageFormat::BGRA5551:
        case VTFImageFormat::RGBA8888:
        case VTFImageFormat::ABGR8888:
        case VTFImageFormat::RGB888:
        case VTFImageFormat::BGR888:
        case VTFImageFormat::RGB565:
        case VTFImageFormat::I8:
        case VTFImageFormat::IA88:
        case VTFImageFormat::P8:
        case VTFImageFormat::A8:
        case VTFImageFormat::RGB888_BLUESCREEN:
        case VTFImageFormat::BGR888_BLUESCREEN:
        case VTFImageFormat::ARGB8888:
        case VTFImageFormat::BGRA8888:
        case VTFImageFormat::BGRX8888:
        case VTFImageFormat::UV88:
        case VTFImageFormat::UVWQ8888:
        case VTFImageFormat::RGBA16161616F:
        case VTFImageFormat::RGBA16161616:
        case VTFImageFormat::UVLX8888:
        case VTFImageFormat::R32F:
        case VTFImageFormat::RGB323232F:
        case VTFImageFormat::RGBA32323232F:
        case VTFImageFormat::NV_DST16:
        case VTFImageFormat::NV_DST24:
        case VTFImageFormat::NV_INTZ:
        case VTFImageFormat::NV_RAWZ:
        case VTFImageFormat::ATI_DST16:
        case VTFImageFormat::ATI_DST24:
        case VTFImageFormat::NV_NULL:
        case VTFImageFormat::COUNT:
        case VTFImageFormat::NONE:
            memcpy((char *) dst, (char *) raw_pixel_data, dst_size);
            break;
    }
    return true;

}

void vtf_destroy(VTFFile *vfile) {
    delete vfile;
}
void vtf_free(char* buff) {
    free(buff);
}
