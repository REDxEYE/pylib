use std::io;
use std::io::ErrorKind;

pub struct BitReader<'a> {
    array: &'a[u8],
    pos: u32,
}

impl<'a>  BitReader<'a> {
    pub fn from_slice(slice: &'a [u8]) -> Self {
        BitReader { array: slice, pos: 0 }
    }

    pub fn get(&mut self, count: u32) -> io::Result<u16> {
        let read = self.peek(count, self.pos);
        self.pos += count;
        read
    }
    pub fn peek(&mut self, count: u32, offset: u32) -> io::Result<u16> {
        if count > 16 {
            return Err(io::Error::new(ErrorKind::InvalidInput, "Cannot read more than 16 bit at once"));
        }
        if offset + count > self.len() as u32 {
            return Err(io::Error::new(ErrorKind::InvalidInput, format!("Not enough bits to read {} at offset {}", count, offset)));
        }

        let idx = (offset / 8) as usize;
        let bit = offset % 8;

        let chunk = if bit + count <= 8 {
            self.array[idx] as u16
        } else {
            self.array[idx] as u16 | (self.array[idx + 1] as u16) << 8
        };

        Ok((chunk >> bit) & ((1 << count) - 1))
    }

    pub fn len(&self) -> usize {
        self.array.len() * 8
    }
}