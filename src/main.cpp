

#include <compressed_index_buffer.h>
#include <compressed_vertex_buffer.h>
#include <lz4.h>
#include <vector>

extern "C" {

PYLIB_DLL_EXPORT bool decode_vertex_buffer(char *src, size_t src_size, char *dst, size_t dst_size, int32_t vertex_size,
                                           int32_t vertex_count) {

    std::vector<uint8_t> buffer;
    buffer.assign(src, src + src_size);
    std::vector<uint8_t> decoded = MeshOptimizerVertexDecoder::DecodeVertexBuffer(vertex_count, vertex_size, buffer);
    size_t buffer_size = decoded.size() * sizeof(decoded.front());
    if (buffer_size > dst_size) {
        return false;
    }
    memcpy(dst, decoded.data(), buffer_size);
    return true;
}

PYLIB_DLL_EXPORT bool decode_index_buffer(char *src, size_t src_size, char *dst, size_t dst_size, int32_t index_size,
                                          int32_t index_count) {

    std::vector<uint8_t> buffer;
    buffer.assign(src, src + src_size);
    std::vector<uint8_t> decoded = MeshOptimizerIndexDecoder::DecodeIndexBuffer(index_count, index_size, buffer);
    size_t buffer_size = decoded.size() * sizeof(decoded.front());
    if (buffer_size > dst_size) {
        return false;
    }
    memcpy(dst, decoded.data(), buffer_size);
    return true;
}
}