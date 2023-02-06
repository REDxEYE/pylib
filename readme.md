# Library helper for SourceIO blender plugin.

## Contains:
* lzham, lz4, zstd compressors/decompressors
* draco compressed mesh data decompression routines
* tiny vtflib implementation
* BCn decompressor

## How to build:

### Linux / Windows(mingw64) (steps by [@Architector4](https://www.github.com/Architector4)) 
1. git clone --recurse-submodules -j8 'https://github.com/REDxEYE/pylib.git'
2. cd pylib
3. cmake .
4. make
5. cp pylib.so ~/.config/blender/3.4/scripts/addons/SourceIO/library/utils/pylib_loader/unix/pylib.so (adjust path to fit your blender version)

### Windows (MSVC)
You can follow guide from Microsoft https://learn.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio?view=msvc-170
