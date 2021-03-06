//
// Created by MED45 on 09.04.2020.
//

#ifndef COMPRESSED_VERTEX_BUFFER_H
#define COMPRESSED_VERTEX_BUFFER_H

#include <cstdint>
#include <vector>
#include "utils.hpp"
/*
MIT License
Copyright (c) 2020 Florian Weischer
Copyright (c) 2015 Steam Database
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
class MeshOptimizerVertexDecoder
{
public:
    static constexpr uint8_t VertexHeader = 0xa0;

    static constexpr int32_t VertexBlockSizeBytes = 8192;
    static constexpr int32_t VertexBlockMaxSize = 256;
    static constexpr int32_t ByteGroupSize = 16;
    static constexpr int32_t TailMaxSize = 32;

    static uint32_t GetVertexBlockSize(uint32_t vertexSize);
    static uint8_t Unzigzag8(uint8_t v);
    // Note: These have been moved to the cpp because the span class being used messes with the debugger.
    // Once C++-20 is available, these should be moved back to the header and tcb::span should be replaced by std::span!
    //static tcb::span<uint8_t> DecodeBytesGroup(const tcb::span<uint8_t> &data, tcb::span<uint8_t> destination, int bitslog2);
    //static tcb::span<uint8_t> DecodeBytes(tcb::span<uint8_t> data, tcb::span<uint8_t> destination);
    //static tcb::span<uint8_t> DecodeVertexBlock(tcb::span<uint8_t> data, tcb::span<uint8_t> vertexData, int vertexCount, int vertexSize, tcb::span<uint8_t> lastVertex);
    static std::vector<uint8_t> DecodeVertexBuffer(uint32_t vertexCount, uint32_t vertexSize, const std::vector<uint8_t> &vertexBuffer);
};

#endif //COMPRESSED_VERTEX_BUFFER_H
