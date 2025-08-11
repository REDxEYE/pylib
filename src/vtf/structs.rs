use crate::utils::reader_utils::{FromReader, ReadExt};
use crate::vtf::VtfFlags;
use byteorder::ReadBytesExt;
use image::Rgb;
use num_traits::FromPrimitive;
use std::cmp::max;
use std::io::{Read, Seek, SeekFrom};

#[derive(Debug, FromPrimitive, PartialEq)]
#[repr(i32)]
#[derive(Clone)]
pub enum VtfImageFormat {
    NONE = -1,
    RGBA8888 = 0,
    ABGR8888,
    RGB888,
    BGR888,
    RGB565,
    I8,
    IA88,
    P8,
    A8,
    RGB888Bluescreen,
    BGR888Bluescreen,
    ARGB8888,
    BGRA8888,
    DXT1,
    DXT3,
    DXT5,
    BGRX8888,
    BGR565,
    BGRX5551,
    BGRA4444,
    DXT1Onebitalpha,
    BGRA5551,
    UV88,
    UVWQ8888,
    RGBA16161616F,
    RGBA16161616,
    UVLX8888,
}

impl VtfImageFormat {
    /// Returns the bytes required to store an image of given dimensions
    pub fn buffer_size(&self, dimensions: (u32, u32)) -> i64 {
        let (w, h) = (dimensions.0 as i64, dimensions.1 as i64);

        match self {
            VtfImageFormat::NONE => 0,
            VtfImageFormat::DXT1 | VtfImageFormat::DXT1Onebitalpha => w * h / 2,
            VtfImageFormat::DXT3 | VtfImageFormat::DXT5 => w * h,
            _ => w * h * self.bytes_per_pixel() as i64, // Use lookup for all other formats
        }
    }

    /// Returns the width and height of a mipmapped image at a given mip level
    pub fn mip_resolution(&self, dimensions: (u32, u32), mip: u32) -> (u32, u32) {
        let (w, h) = (dimensions.0 >> mip, dimensions.1 >> mip);

        if self.is_compressed() {
            (max(w, 4), max(h, 4)) // DXT formats require minimum 4x4 blocks
        } else {
            (w, h)
        }
    }

    /// Returns bytes per pixel for uncompressed formats
    fn bytes_per_pixel(&self) -> u8 {
        match self {
            VtfImageFormat::RGBA8888
            | VtfImageFormat::ABGR8888
            | VtfImageFormat::ARGB8888
            | VtfImageFormat::BGRA8888
            | VtfImageFormat::BGRX8888
            | VtfImageFormat::UVWQ8888
            | VtfImageFormat::UVLX8888 => 4,

            VtfImageFormat::RGB888
            | VtfImageFormat::BGR888
            | VtfImageFormat::RGB888Bluescreen
            | VtfImageFormat::BGR888Bluescreen => 3,

            VtfImageFormat::RGB565
            | VtfImageFormat::BGR565
            | VtfImageFormat::BGRX5551
            | VtfImageFormat::BGRA4444
            | VtfImageFormat::BGRA5551
            | VtfImageFormat::UV88
            | VtfImageFormat::IA88 => 2,

            VtfImageFormat::I8 | VtfImageFormat::P8 | VtfImageFormat::A8 => 1,

            VtfImageFormat::RGBA16161616F | VtfImageFormat::RGBA16161616 => 8,

            _ => 0, // Default case for NONE or unknown formats
        }
    }

    /// Checks if format is compressed (DXT1, DXT3, DXT5)
    fn is_compressed(&self) -> bool {
        matches!(
            self,
            VtfImageFormat::DXT1
                | VtfImageFormat::DXT1Onebitalpha
                | VtfImageFormat::DXT3
                | VtfImageFormat::DXT5
        )
    }
}

#[derive(Debug)]
pub struct Header {
    pub version: (u32, u32),
    pub header_size: u32,
    pub dimensions: (u32, u32),
    pub flags: VtfFlags,
    pub frames: u16,
    pub first_frame: u16,
    pub reflectivity: Rgb<f32>,
    pub bump_scale: f32,
    pub high_res_format: VtfImageFormat,
    pub mip_count: u8,
    pub low_res_format: VtfImageFormat,
    pub low_res_dimensions: (u32, u32),
    pub depth: u16,
    pub resource_count: u32,
}

impl<R: Read + Seek> FromReader<R> for Header {
    fn from_reader(reader: &mut R) -> std::io::Result<Self> {
        let version = (reader.read_u32le()?, reader.read_u32le()?);
        let header_size = reader.read_u32le()?;
        let dimensions = (reader.read_u16le()? as u32, reader.read_u16le()? as u32);
        let flags = VtfFlags::from_bits(reader.read_u32le()?).unwrap_or_else(VtfFlags::empty);
        let frames = reader.read_u16le()?;
        let first_frame = reader.read_u16le()?;

        reader.seek(SeekFrom::Current(4))?; // Skip padding

        let mut buf = [0.0; 3];
        reader.read_f32_into::<byteorder::LittleEndian>(&mut buf)?;

        let reflectivity = Rgb::from(buf);

        reader.seek(SeekFrom::Current(4))?; // Skip padding

        let bump_scale = reader.read_f32le()?;
        let high_res_format = VtfImageFormat::from_i32(reader.read_i32le()?).ok_or_else(|| {
            std::io::Error::new(std::io::ErrorKind::InvalidData, "Invalid high_res_format")
        })?;
        let mip_count = reader.read_u8()?;
        let low_res_format = VtfImageFormat::from_i32(reader.read_i32le()?).ok_or_else(|| {
            std::io::Error::new(std::io::ErrorKind::InvalidData, "Invalid low_res_format")
        })?;
        let low_res_dimensions = (reader.read_u8()? as u32, reader.read_u8()? as u32);

        // Read depth only if version >= 2, otherwise default to 0
        let depth = (version.1 >= 2)
            .then(|| reader.read_u16le())
            .transpose()?
            .unwrap_or(0);

        // Read resource count only if version >= 3
        let resource_count = (version.1 >= 3)
            .then(|| {
                reader.seek(SeekFrom::Current(3))?;
                let count = reader.read_u32le()?;
                reader.seek(SeekFrom::Current(8))?;
                Ok::<u32, std::io::Error>(count)
            })
            .transpose()?
            .unwrap_or(0);

        Ok(Header {
            version,
            header_size,
            dimensions,
            flags,
            frames,
            first_frame,
            reflectivity,
            bump_scale,
            high_res_format,
            mip_count,
            low_res_format,
            low_res_dimensions,
            depth,
            resource_count,
        })
    }
}

#[derive(Debug)]
pub struct ResourceEntry {
    pub tag: [u8; 3],
    pub flags: u8,
    pub offset: u32,
}

impl<R: Read + Seek> FromReader<R> for ResourceEntry {
    fn from_reader(reader: &mut R) -> std::io::Result<Self> {
        let mut tag = [0u8; 3];
        reader.read_exact(&mut tag)?;

        let flags = reader.read_u8()?;
        let offset = reader.read_u32le()?;

        Ok(ResourceEntry { tag, flags, offset })
    }
}
