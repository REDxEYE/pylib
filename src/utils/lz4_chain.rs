use std::cmp;
use std::ffi::c_int;

use lz4_sys::{
    LZ4StreamDecode, LZ4_createStreamDecode, LZ4_decompress_safe_continue, LZ4_freeStreamDecode,
    LZ4_setStreamDecode,
};

pub const SAFE_C_INT_MAX: u32 = c_int::MAX as u32;

pub struct LZ4ChainDecoder {
    decompressor: *mut LZ4StreamDecode,
    output_buffer: Vec<u8>,
    output_index: isize,
    frame_size: isize,
}

impl LZ4ChainDecoder {
    pub fn new(block_size: isize, extra_blocks: isize) -> Self {
        let block_size = cmp::max(block_size as usize, 1024).next_power_of_two();
        let output_length = (1024 * 64) + (1 + extra_blocks as usize) * block_size + 32;

        Self {
            decompressor: unsafe { LZ4_createStreamDecode() },
            output_buffer: vec![0; output_length],
            output_index: 0,
            frame_size: block_size as isize,
        }
    }

    pub fn free(&mut self) {
        unsafe {
            LZ4_freeStreamDecode(self.decompressor);
        }
    }

    pub fn prepare(&mut self, block_size: isize) -> Result<(), &'static str> {
        if self.output_index + block_size <= self.output_buffer.len() as isize {
            return Ok(());
        }
        let dict_start = cmp::max(self.output_index as isize - (1024 * 64), 0) as isize;
        let dict_size = self.output_index - dict_start;
        self.output_buffer
            .copy_within(dict_start as usize..self.output_index as usize, 0);
        if dict_size > SAFE_C_INT_MAX as isize {
            return Err("input_data or decompressed_size is too big");
        }
        unsafe {
            LZ4_setStreamDecode(
                self.decompressor,
                self.output_buffer.as_ptr().cast(),
                dict_size as c_int,
            )
        };
        self.output_index = dict_size;
        Ok(())
    }

    pub fn decode(&mut self, src: &[u8], block_size: isize) -> Result<isize, &'static str> {
        let block_size = if block_size > 0 {
            block_size
        } else {
            self.frame_size
        };
        self.prepare(block_size)?;
        let tmp = &mut self.output_buffer[self.output_index as usize..];
        if src.len() > SAFE_C_INT_MAX as usize || block_size > SAFE_C_INT_MAX as isize {
            return Err("input_data or decompressed_size is too big");
        }
        let decoded_size = unsafe {
            LZ4_decompress_safe_continue(
                self.decompressor,
                src.as_ptr(),
                tmp.as_mut_ptr(),
                src.len() as c_int,
                block_size as c_int,
            )
        };
        if decoded_size > 0 {
            self.output_index += decoded_size as isize;
        }
        Ok(decoded_size as isize)
    }

    pub fn drain(
        &mut self,
        dst: &mut [u8],
        offset: isize,
        size: isize,
    ) -> Result<(), &'static str> {
        let end_offset = self.output_index as isize + offset;
        if end_offset < 0
            || size > dst.len() as isize
            || end_offset + size > self.output_index as isize
        {
            return Err("Invalid offset or size");
        }
        dst[..size as usize].copy_from_slice(
            &self.output_buffer[(end_offset as usize)..(end_offset as isize + size) as usize],
        );
        Ok(())
    }

    pub fn decode_and_drain(&mut self, src: &[u8], dst: &mut [u8]) -> Result<isize, &'static str> {
        let decoded = self.decode(src, 0)?;
        let dst_len = dst.len() as isize;
        if decoded > dst_len {
            println!("Decode buffer overflow: {decoded}>{dst_len}");
            return Err("Decode buffer overflow");
        }
        self.drain(dst, -(decoded), decoded)?;
        Ok(decoded)
    }
}
