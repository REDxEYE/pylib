pub mod compressed_index_buffer {
    const INDEX_HEADER: u8 = 0xe0;


    /// Writes a `u16` value into a mutable `u8` slice.
    /// Assumes little-endian byte order.
    fn write_u16(slice: &mut [u8], value: u16) -> Result<(), &'static str> {
        if slice.len() < 2 {
            return Err("Slice too small to hold a u16 value");
        }

        slice[0..2].copy_from_slice(&value.to_le_bytes());
        Ok(())
    }

    /// Writes a `u32` value into a mutable `u8` slice.
    /// Assumes little-endian byte order.
    fn write_u32(slice: &mut [u8], value: u32) -> Result<(), &'static str> {
        if slice.len() < 4 {
            return Err("Slice too small to hold a u32 value");
        }

        slice[0..4].copy_from_slice(&value.to_le_bytes());
        Ok(())
    }
    
    fn push_edge_fifo(fifo: &mut [(u32, u32); 16], offset: &mut usize, a: u32, b: u32) {
        fifo[*offset] = (a, b);
        *offset = (*offset + 1) & 15;
    }

    fn get_edge_fifo(fifo: &[(u32, u32); 16], offset: usize) -> (u32, u32) {
        fifo[offset & 15]
    }

    fn push_vertex_fifo(fifo: &mut [u32; 16], offset: &mut usize, v: u32, cond: bool) {
        fifo[*offset] = v;
        *offset = (*offset + if cond { 1 } else { 0 }) & 15;
    }

    fn get_vertex_fifo(fifo: &[u32; 16], offset: usize) -> u32 {
        fifo[offset & 15]
    }

    fn write_triangle(destination: &mut [u8], mut dest_offset: usize, index_size: usize, a: u32, b: u32, c: u32) {
        if index_size == 2 {
            write_u16(&mut destination[dest_offset * index_size..], a as u16).expect("Failed to write Triangle index A");
            dest_offset += 1;
            write_u16(&mut destination[dest_offset * index_size..], b as u16).expect("Failed to write Triangle index B");
            dest_offset += 1;
            write_u16(&mut destination[dest_offset * index_size..], c as u16).expect("Failed to write Triangle index C");
        } else {
            write_u32(&mut destination[dest_offset * index_size..], a).expect("Failed to write Triangle index A");
            dest_offset += 1;
            write_u32(&mut destination[dest_offset * index_size..], b).expect("Failed to write Triangle index B");
            dest_offset += 1;
            write_u32(&mut destination[dest_offset * index_size..], c).expect("Failed to write Triangle index C");
        }
    }

    pub fn decode_index_buffer(destination: &mut [u8], index_count: usize, index_size: usize, buffer: &[u8]) -> Result<(), &'static str> {
        if index_count % 3 != 0 {
            return Err("Expected indexCount to be a multiple of 3.");
        }

        if index_size != 2 && index_size != 4 {
            return Err("Expected indexSize to be either 2 or 4");
        }

        let data_offset = 1 + (index_count / 3);

        if buffer.len() < data_offset + 16 {
            return Err("Index buffer is too short.");
        }

        if buffer[0]&0xF0 != INDEX_HEADER {
            return Err("Incorrect index buffer header.");
        }
        if buffer[0] &0x0F > 1{
            return Err("Unsupported version");
        }

        let mut vertex_fifo: [u32; 16] = [0; 16];
        let mut edge_fifo: [(u32, u32); 16] = [(0, 0); 16];
        let mut edge_fifo_offset = 0usize;
        let mut vertex_fifo_offset = 0usize;

        let mut next = 0u32;
        let mut last = 0u32;

        let mut buffer_index = 1;
        let data = &buffer[data_offset..buffer.len() - 16];
        let mut data_offset = 0usize;
        let code_aux_table = &buffer[buffer.len() - 16..];

        for i in (0..index_count).step_by(3) {
            let code_tri = buffer[buffer_index];
            buffer_index += 1;

            if code_tri < 0xF0 {
                let fe = (code_tri >> 4) as usize;
                let ab = get_edge_fifo(&edge_fifo, edge_fifo_offset.wrapping_sub(1 + fe));
                let fec = (code_tri & 15) as usize;

                if fec != 15 {
                    let c: u32 = if fec == 0 { next } else { get_vertex_fifo(&vertex_fifo, vertex_fifo_offset.wrapping_sub(1 + fec)) };
                    let fec0 = fec == 0;
                    next += if fec0 { 1 } else { 0 };

                    write_triangle(destination, i, index_size, ab.0, ab.1, c);

                    push_vertex_fifo(&mut vertex_fifo, &mut vertex_fifo_offset, c, fec0);

                    push_edge_fifo(&mut edge_fifo, &mut edge_fifo_offset, c, ab.1);
                    push_edge_fifo(&mut edge_fifo, &mut edge_fifo_offset, ab.0, c);
                } else {
                    let c = decode_index(data, &mut data_offset, last);
                    last = c;

                    write_triangle(destination, i, index_size, ab.0, ab.1, c);

                    push_vertex_fifo(&mut vertex_fifo, &mut vertex_fifo_offset, c, true);

                    push_edge_fifo(&mut edge_fifo, &mut edge_fifo_offset, c, ab.1);
                    push_edge_fifo(&mut edge_fifo, &mut edge_fifo_offset, ab.0, c);
                }
            } else if code_tri < 0xFE {
                let code_aux = code_aux_table[(code_tri & 15) as usize];
                let feb: u8 = code_aux >> 4;
                let fec: u8 = code_aux % 16;
                let a = next;
                next += 1;
                let b = if feb == 0 { next } else { get_vertex_fifo(&vertex_fifo, vertex_fifo_offset.wrapping_sub(feb as usize)) };

                let feb0 = if feb == 0 { 1 } else { 0 };
                next += feb0;

                let c = if fec == 0 { next } else { get_vertex_fifo(&vertex_fifo, vertex_fifo_offset.wrapping_sub(fec as usize)) };

                let fec0 = if fec == 0 { 1 } else { 0 };
                next += fec0;

                write_triangle(destination, i, index_size, a, b, c);

                push_vertex_fifo(&mut vertex_fifo, &mut vertex_fifo_offset, a, true);
                push_vertex_fifo(&mut vertex_fifo, &mut vertex_fifo_offset, b, feb0 == 1);
                push_vertex_fifo(&mut vertex_fifo, &mut vertex_fifo_offset, c, fec0 == 1);

                push_edge_fifo(&mut edge_fifo, &mut edge_fifo_offset, b, a);
                push_edge_fifo(&mut edge_fifo, &mut edge_fifo_offset, c, b);
                push_edge_fifo(&mut edge_fifo, &mut edge_fifo_offset, a, c);
            } else {
                let code_aux = data[data_offset];
                data_offset += 1;

                let fea: u8 = if code_tri == 0xfe { 0u8 } else { 15u8 };
                let feb: u8 = code_aux >> 4;
                let fec: u8 = code_aux & 15;

                let mut a = if fea == 0 {
                    let tmp = next;
                    next += 1;
                    tmp
                } else { 0 };

                let mut b = if feb == 0 {
                    let tmp = next;
                    next += 1;
                    tmp
                } else { get_vertex_fifo(&vertex_fifo, vertex_fifo_offset.wrapping_sub(feb as usize)) };

                let mut c = if fec == 0 {
                    let tmp = next;
                    next += 1;
                    tmp
                } else { get_vertex_fifo(&vertex_fifo, vertex_fifo_offset.wrapping_sub(fec as usize)) };

                if fea == 15 {
                    a = decode_index(&data, &mut data_offset, last);
                    last = a;
                }
                if feb == 15 {
                    b = decode_index(&data, &mut data_offset, last);
                    last = b;
                }
                if fec == 15 {
                    c = decode_index(&data, &mut data_offset, last);
                    last = c;
                }

                write_triangle(destination, i, index_size, a, b, c);

                push_vertex_fifo(&mut vertex_fifo, &mut vertex_fifo_offset, a, true);
                push_vertex_fifo(&mut vertex_fifo, &mut vertex_fifo_offset, b, (feb == 0) || feb == 15);
                push_vertex_fifo(&mut vertex_fifo, &mut vertex_fifo_offset, c, (fec == 0) || fec == 15);

                push_edge_fifo(&mut edge_fifo, &mut edge_fifo_offset, b, a);
                push_edge_fifo(&mut edge_fifo, &mut edge_fifo_offset, c, b);
                push_edge_fifo(&mut edge_fifo, &mut edge_fifo_offset, a, c);
            }
        }

        if data.len() != data_offset {
            return Err("we didn't read all data bytes and stopped before the boundary between data and codeaux table");
        }

        Ok(())
    }

    fn unzigzag32(v: u32) -> u32 {
        let negated_lsb = if v & 1 == 1 { 0xFFFFFFFF } else { 0 }; // Emulating -(v & 1) with conditional
        return (v >> 1) ^ negated_lsb;
    }

    fn decode_index(data: &[u8], data_offset: &mut usize, last: u32) -> u32 {
        let v = decode_varint(&data, data_offset).expect("Failed to read varint encoded index");
        let d = unzigzag32(v);
        last.wrapping_add(d)
    }

    fn decode_varint(data: &[u8], data_offset: &mut usize) -> Option<u32> {
        if *data_offset >= data.len() {
            return None; // Ensure there's at least one byte to read
        }

        let mut result = 0u32;
        let mut shift = 0;
        let mut read_more = true;

        while read_more && *data_offset < data.len() && shift <= 28 {
            let byte = data[*data_offset] as u32;
            *data_offset += 1; // Move to the next byte for the next iteration

            result |= (byte & 127) << shift; // Mask out the continuation bit and accumulate the value
            shift += 7; // Prepare for the next 7 bits

            // If the continuation bit is not set, stop reading more bytes
            read_more = byte >= 128;
        }

        if shift > 28 && read_more {
            // If we've exceeded the shift limit and the loop is trying to continue,
            // it indicates data is malformed or not as expected for a u32 varint
            None
        } else {
            Some(result)
        }
    }
}