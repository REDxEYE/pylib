use std::io::{self, BufRead, Error, ErrorKind, Read, Seek, SeekFrom};

use byteorder::{LE, ReadBytesExt};
use half::f16;

pub trait FromReader<R>: Sized
    where R: Read + Seek {
    fn from_reader(reader: &mut R) -> io::Result<Self>;
}

pub trait BufReadExt: BufRead {
    fn read_ztstring_buf(&mut self) -> io::Result<String> {
        let mut buffer = Vec::new();
        self.read_until(0, &mut buffer)?;
        buffer.truncate(buffer.len() - 1);
        String::from_utf8(buffer).map_err(|e| Error::new(ErrorKind::InvalidData, e.to_string()))
    }
}

pub trait ReadSeekExt: Read + Seek {
    fn read_ztstring(&mut self) -> io::Result<String> {
        let mut buffer = Vec::with_capacity(16);
        let mut chunk = vec![0; 32]; // Buffer for the chunk

        loop {
            let size = self.read(&mut chunk)?;
            if size == 0 {
                break; // End of file or stream reached without finding zero
            }
            if let Some(pos) = chunk.iter().position(|&x| x == 0) {
                buffer.extend_from_slice(&chunk[..pos]); // Add everything before the zero
                self.seek(SeekFrom::Current(-((size as i64) - (pos as i64) - 1)))?; // Seek back to position right after zero
                break;
            } else {
                buffer.extend_from_slice(&chunk[..size]); // Add all read bytes if no zero found
            }
        }
        String::from_utf8(buffer).map_err(|e| Error::new(ErrorKind::InvalidData, e.to_string()))
    }
    #[inline]
    fn read_pztstring(&mut self, base_offset: u64) -> io::Result<String> {
        let ptr = self.read_i32le()? as i64;
        let cur_offset = self.stream_position()?;
        self.seek(SeekFrom::Start((base_offset as i64 + ptr) as u64))?;
        let res = self.read_ztstring()?;
        self.seek(SeekFrom::Start(cur_offset))?;
        Ok(res)
    }
}

pub trait ReadExt: Read {
    #[inline]
    fn read_fixed_string(&mut self, len: usize) -> io::Result<String> {
        let mut buf = vec![0u8; len + 1];
        self.read_exact(&mut buf[..len])?;
        let nul_index = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
        let valid_buf = &buf[..nul_index];
        String::from_utf8(valid_buf.to_vec()).map_err(|e| Error::new(ErrorKind::InvalidData, e.to_string()))
    }


    #[inline]
    fn read_relptr(&mut self, base: u64) -> io::Result<u64> {
        let i = self.read_i32le()?;
        Ok(base.wrapping_add_signed(i as i64))
    }


    #[inline]
    fn read_u16le(&mut self) -> io::Result<u16> {
        self.read_u16::<LE>()
    }

    #[inline]
    fn read_u32le(&mut self) -> io::Result<u32> {
        self.read_u32::<LE>()
    }

    #[inline]
    fn read_i16le(&mut self) -> io::Result<i16> {
        self.read_i16::<LE>()
    }

    #[inline]
    fn read_i32le(&mut self) -> io::Result<i32> {
        self.read_i32::<LE>()
    }

    #[inline]
    fn read_f32le(&mut self) -> io::Result<f32> {
        self.read_f32::<LE>()
    }

    #[inline]
    fn read_f16le(&mut self) -> io::Result<f32> {
        Ok(f16::from_bits(self.read_u16::<LE>()?).into())
    }
}

impl<R: Read + Seek + ?Sized> ReadSeekExt for R {}

impl<R: Read + ?Sized> ReadExt for R {}

impl<R: BufRead + ?Sized> BufReadExt for R {}