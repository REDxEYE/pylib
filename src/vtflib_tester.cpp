#include <cstdio>
#include <cstdlib>
#include "vtflib/vtflib.h"
#include "vtflib_module.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main(int argc, char **argv) {
    FILE *file = fopen(
//            R"(C:\PYTHON_PROJECTS\SourceIOPlugin\test_data\materials\models\characters\kasumi\kas_face_base.vtf)",
//            R"(C:\Program Files (x86)\Steam\steamapps\common\Portal 2\portal2\materials\metal\underground_metal_corrugated001..vtf)",
//            R"(C:\Program Files (x86)\Steam\steamapps\common\Half-Life 2\hl2\materials\brick\brickfloor001a.vtf)",
            R"(C:\Program Files (x86)\Steam\steamapps\common\SourceFilmmaker\game\tf\materials\skybox\sky_badlands_01dn.vtf)",
            "rb");
    if (file == nullptr) {
        fprintf(stderr, "Failed to open file\n");
        return -1;
    }

    fseek(file, 0, SEEK_END);
    size_t fsize = ftell(file);
    fseek(file, 0, SEEK_SET);

    uint8_t *fdata = static_cast<uint8_t *>(malloc(fsize));
    fread(fdata, 1, fsize, file);

    VTFFile vfile(fdata, fsize);
    printf("%i.%i\n", vfile.version_major(), vfile.version_minor());
    printf("%ix%ix%i\n", vfile.width(), vfile.height(), vfile.depth());
    printf("%i\n", vfile.image_format());
    size_t buffer_size;

    char *image_data = static_cast<char *>(malloc(vfile.width() * vfile.height() * 3));

    vtf_get_as_rgba8888(&vfile, image_data, vfile.width() * vfile.height() * 3, false);

    if (image_data == nullptr) {
        fprintf(stderr, "No high-res image resource!");
        delete fdata;
        return -1;
    }
    stbi_write_png("test.png", vfile.width(), vfile.height(), 3, image_data, vfile.width() * 3);

    delete fdata;
}