extern crate num;
pub mod errors;
mod structs;
mod decoders;

use crate::utils::reader_utils::{FromReader, ReadExt};
use crate::vtf::errors::VTFError;
use image::{ImageBuffer, Rgba};
use rayon::prelude::*;
use std::fs::File;
use std::io::{BufRead, BufReader, Seek, SeekFrom};
use std::path::Path;
use structs::{Header, ResourceEntry, VtfImageFormat};

type F32Image = ImageBuffer<Rgba<f32>, Vec<f32>>;

bitflags::bitflags! {
    #[derive(Debug)]
    pub struct  VtfFlags:u32 {
        // Flags from the *.txt config file
        const POINTSAMPLE = 0x00000001;
        const TRILINEAR = 0x00000002;
        const CLAMPS = 0x00000004;
        const CLAMPT = 0x00000008;
        const ANISOTROPIC = 0x00000010;
        const HINT_DXT5 = 0x00000020;
        const SRGB = 0x00000040;
        const NORMAL = 0x00000080;
        const NOMIP = 0x00000100;
        const NOLOD = 0x00000200;
        const ALL_MIPS = 0x00000400;
        const PROCEDURAL = 0x00000800;
        const ONEBITALPHA = 0x00001000;
        const EIGHTBITALPHA = 0x00002000;
        const ENVMAP = 0x00004000;
        const RENDERTARGET = 0x00008000;
        const DEPTHRENDERTARGET = 0x00010000;
        const NODEBUGOVERRIDE = 0x00020000;
        const SINGLECOPY = 0x00040000;
        const PRE_SRGB = 0x00080000;
        const NODEPTHBUFFER = 0x00800000;
        const CLAMPU = 0x02000000;
        const VERTEXTEXTURE = 0x04000000;
        const SSBUMP = 0x08000000;
        const BORDER = 0x20000000;
    }
}

pub fn load_vtf<P: AsRef<Path>>(filepath: P) -> Result<F32Image, VTFError> {
    let file = File::open(filepath).map_err(|_| VTFError::FailedToReadTexture)?; // More specific error handling
    let mut reader = BufReader::new(file);
    read_vtf(&mut reader)
}
pub fn read_vtf<R: BufRead + Seek>(reader: &mut R) -> Result<F32Image, VTFError> {
    // Validate VTF Header
    if reader.read_u32le()? != 0x00465456 {
        return Err(VTFError::InvalidHeader);
    }

    let header = Header::from_reader(reader)?;

    // Read resource entries if available
    let resources: Vec<ResourceEntry> = if header.version.1 >= 3 {
        let mut res_entries = Vec::with_capacity(header.resource_count as usize);
        for _ in 0..header.resource_count {
            res_entries.push(ResourceEntry::from_reader(reader)?);
        }
        res_entries
    } else {
        Vec::new()
    };

    // Find the high-resolution texture resource
    if let Some(resource) = resources.iter().find(|entry| entry.tag == [0x30, 0, 0]) {
        reader.seek(SeekFrom::Start(resource.offset as u64))?;
    } else if header.low_res_format == VtfImageFormat::DXT1 {
        
        reader.seek(SeekFrom::Current(header.low_res_format.buffer_size(header.low_res_dimensions)))?;
    } else {
        return Err(VTFError::InvalidResourceEntry);
    }

    // Skip mip levels (except the highest resolution)
    for mip in (1..header.mip_count as u32).rev() {
        let mip_size = header.high_res_format.buffer_size(
            header
                .high_res_format
                .mip_resolution(header.dimensions, mip),
        );
        reader
            .seek(SeekFrom::Current(mip_size))
            .map_err(|_| VTFError::MipLevelError(mip))?;
    }

    // Read high-resolution texture
    let mip_size = header.high_res_format.buffer_size(header.dimensions) as usize;
    let mut mip_data = vec![0u8; mip_size];

    reader
        .read_exact(&mut mip_data)
        .map_err(|_| VTFError::MissingHighResTexture)?;

    // Decode the texture
    let decoded = decoders::decode_texture(header.high_res_format.clone(), &mip_data, header.dimensions)
        .ok_or(VTFError::UnsupportedFormat(header.high_res_format))?;

    // Convert decoded data into an image buffer
    ImageBuffer::from_raw(
        header.dimensions.0,
        header.dimensions.1,
        decoded,
    )
    .ok_or(VTFError::FailedToReadTexture)
}
