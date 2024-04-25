pub mod compressed_vertex_buffer {
    const VERTEX_HEADER: u8 = 0xa0;
    const VERTEX_BLOCK_SIZE_BYTES: usize = 8192;
    const VERTEX_BLOCK_MAX_SIZE: usize = 256;
    const BYTE_GROUP_SIZE: usize = 16;
    const BYTE_GROUP_DECODE_LIMIT: usize = 24;
    const TAIL_MAX_SIZE: usize = 32;

    fn get_vertex_block_size(vertex_size: usize) -> usize {
        let mut result = VERTEX_BLOCK_SIZE_BYTES / vertex_size;
        result &= !(BYTE_GROUP_SIZE - 1);
        result.min(VERTEX_BLOCK_MAX_SIZE)
    }

    fn unzigzag8(v: u8) -> u8 {
        let negated_lsb = if v & 1 == 1 { 0xFF } else { 0 }; // Emulating -(v & 1) with conditional
        return (v >> 1) ^ negated_lsb;
    }

    fn decode_bytes_group(data: &[u8], bitslog2: usize, buffer: &mut [u8]) -> usize {
        let mut byte;
        let mut bytes_used;
        let mut buffer_offset = 0; // Initialize buffer offset
        let mut data_offset = 0; // Initialize data offset to track current position in data

        let mut next = |bits: u8, encv: u8, mut b: u8, mut dv: usize| {
            let enc = b >> (8 - bits);
            b <<= bits;
            let is_same = enc == (1 << bits) - 1;

            if is_same {
                dv += 1;
                buffer[buffer_offset] = encv;
            } else {
                buffer[buffer_offset] = enc;
            }
            buffer_offset += 1;
            return (b, dv);
        };

        // Adjust data_offset based on bitslog2 before entering the loop
        match bitslog2 {
            0 => {
                for b in buffer.iter_mut() {
                    *b = 0;
                }
                return 0;
            }
            1 => {
                bytes_used = 4; // Skip header bytes
                for _ in 0..4 {
                    byte = data[data_offset];
                    (byte, bytes_used) = next(2, data[bytes_used], byte, bytes_used);
                    (byte, bytes_used) = next(2, data[bytes_used], byte, bytes_used);
                    (byte, bytes_used) = next(2, data[bytes_used], byte, bytes_used);
                    (_, bytes_used) = next(2, data[bytes_used], byte, bytes_used);
                    data_offset += 1;
                }
                return bytes_used;
            }
            2 => {
                bytes_used = 8; // Skip header bytes
                for _ in 0..8 {
                    byte = data[data_offset];
                    (byte, bytes_used) = next(4, data[bytes_used], byte, bytes_used);
                    (_, bytes_used) = next(4, data[bytes_used], byte, bytes_used);
                    data_offset += 1;
                }
                return bytes_used;
            }
            3 => {
                // Direct copy if no encoding applied, and skip these bytes
                buffer.copy_from_slice(&data[data_offset..data_offset + buffer.len()]);
                return buffer.len();
            }
            _ => unreachable!(),
        }
    }



    fn decode_bytes(data: &[u8], expected_buffer_size: usize, result_buffer: &mut [u8]) -> Option<usize> {
        if expected_buffer_size % BYTE_GROUP_SIZE != 0 {
            return None;
        }

        let header_size = (expected_buffer_size / BYTE_GROUP_SIZE + 3) / 4;
        if data.len() < header_size {
            return None;
        }

        let (header, block_data) = data.split_at(header_size);

        let mut data_offset = 0; // Start processing data after the header
        for (i, chunk) in result_buffer.chunks_mut(BYTE_GROUP_SIZE).enumerate() {
            if block_data.len() - data_offset < BYTE_GROUP_DECODE_LIMIT {
                return None;
            }

            let bitslog2 = (header[i / 4] >> ((i % 4) * 2)) & 3;
            // Since we're now passing a mutable slice directly to `decode_bytes_group`,
            // it will fill the chunk directly, and we only need to track the bytes used.
            let bytes_used = decode_bytes_group(&block_data[data_offset..], bitslog2 as usize, chunk);
            data_offset += bytes_used; // Update data_offset based on the bytes read
        }

        Some(data_offset + header_size) // Return total bytes read including the header
    }


    fn decode_vertex_block(data: &[u8], vertex_count: usize, vertex_size: usize, last_vertex: &mut [u8; 256], result: &mut[u8]) -> Option<usize> {
        if vertex_count == 0 && vertex_count > VERTEX_BLOCK_MAX_SIZE {
            return None;
        }
        let vertex_count_aligned = (vertex_count + BYTE_GROUP_SIZE - 1) & !(BYTE_GROUP_SIZE - 1);
        let mut data_offset = 0;
        let mut result_buffer = vec![0u8; vertex_count_aligned];
        for k in 0..vertex_size {
            let bytes_read = decode_bytes(&data[data_offset..], vertex_count_aligned, result_buffer.as_mut_slice())?;
            data_offset += bytes_read;
            let mut vertex_offset = k;
            let mut p = last_vertex[k];

            for &v in &result_buffer[..vertex_count] {
                let decoded = unzigzag8(v).wrapping_add(p);
                result[vertex_offset] = decoded;
                p = decoded;
                vertex_offset += vertex_size;
            }
        }
        last_vertex[..vertex_size].copy_from_slice(&result[vertex_size * (vertex_count - 1)..vertex_size * vertex_count]);

        Some(data_offset)
    }

    pub fn decode_vertex_buffer(destination: &mut [u8], vertex_count: usize, vertex_size: usize, buffer: &[u8]) -> Result<(), &'static str> {
        if vertex_size == 0 && vertex_size > 256 && vertex_size % 4 != 0 {
            return Err("Invalid vertex size");
        }

        if buffer.len() < 1 + vertex_size {
            return Err("Buffer too small");
        }

        let (data_header, data) = buffer.split_first().unwrap();
        if (*data_header & 0xf0) != VERTEX_HEADER {
            return Err("Invalid header");
        }

        let version = data_header & 0x0f;
        if version > 0 {
            return Err("Unsupported version");
        }

        let mut last_vertex = [0u8; 256];
        last_vertex[0..vertex_size].copy_from_slice(&data[data.len() - vertex_size..]);
        
        let vertex_block_size = get_vertex_block_size(vertex_size);
        let mut vertex_offset = 0;
        let mut data_offset = 0;
        while vertex_offset < vertex_count {
            let block_size = if vertex_offset + vertex_block_size < vertex_count {
                vertex_block_size
            } else {
                vertex_count - vertex_offset
            };
            let bytes_read = decode_vertex_block(&data[data_offset..], block_size, vertex_size, &mut last_vertex, &mut destination[vertex_offset * vertex_size..]).ok_or("Failed to decode")?;
            data_offset += bytes_read;
            vertex_offset += block_size;
        }

        let tail_size = if vertex_size < TAIL_MAX_SIZE { TAIL_MAX_SIZE } else { vertex_size };
        if data.len()-data_offset != tail_size {
            return Err("Incorrect tail size");
        }

        Ok(())
    }
}