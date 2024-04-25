use std::collections::HashMap;
use std::io::{Read, Seek, SeekFrom};

use rustlib::utils::reader_utils::ReadExt;

use crate::errors::SourceError;
use crate::source_model::model::VertexBuffer;
use crate::utils::reader_utils::FromReader;

#[derive(Debug)]
#[repr(C)]
pub struct VertexFileHeader {
    ident: u32,
    version: u32,
    checksum: u32,
    lod_count: u32,
    lod_vertex_counts: [u32; 8],
    fixup_count: u32,
    fixup_table_offset: u32,
    vertex_data_offset: u32,
    tangent_data_offset: u32,
}

impl<R: Read + Seek> FromReader<R> for VertexFileHeader {
    fn from_reader(reader: &mut R) -> std::io::Result<Self> {
        Ok(VertexFileHeader {
            ident: reader.read_u32le()?,
            version: reader.read_u32le()?,
            checksum: reader.read_u32le()?,
            lod_count: reader.read_u32le()?,
            lod_vertex_counts: [reader.read_u32le()?, reader.read_u32le()?, reader.read_u32le()?, reader.read_u32le()?, reader.read_u32le()?, reader.read_u32le()?, reader.read_u32le()?, reader.read_u32le()?],
            fixup_count: reader.read_u32le()?,
            fixup_table_offset: reader.read_u32le()?,
            vertex_data_offset: reader.read_u32le()?,
            tangent_data_offset: reader.read_u32le()?,
        })
    }
}

pub fn read_vvd_v4<R: Read + Seek>(reader: &mut R) -> Result<VertexBuffer, SourceError> {
    let header = VertexFileHeader::from_reader(reader)?;

    if header.ident != u32::from_le_bytes(*b"IDSV") {
        return Err(SourceError::InvalidHeader("vvd".into(), header.ident.to_le_bytes(), *b"IDSV"));
    }
    if header.version != 4 {
        return Err(SourceError::UnsupportedVersionExact("vvd".into(), header.version, 4));
    }

    let vertex_count = header.lod_vertex_counts[0];

    reader.seek(SeekFrom::Start(header.vertex_data_offset as u64))?;
    let mut vertex_data = vec![0u8; vertex_count as usize * 48];
    reader.read_exact(&mut vertex_data)?;

    if header.tangent_data_offset > 0 {
        reader.seek(SeekFrom::Start((header.tangent_data_offset + 16 * vertex_count) as u64))?;
    }

    let old_pos = reader.stream_position()?;
    reader.seek(SeekFrom::End(0))?;
    let stream_size = reader.stream_position()?;
    reader.seek(SeekFrom::Start(old_pos))?;
    let remaining = stream_size - old_pos;
    let mut extra_slots = HashMap::new();
    if remaining >= 8 {
        let extra_data_base = reader.stream_position()?;
        let extra_item_count = reader.read_u32le()?;
        let extra_data_total_size = reader.read_u32le()?;

        if remaining - 8 >= extra_data_total_size as u64 {
            extra_slots.reserve(extra_item_count as usize);
            for _ in 0..extra_item_count {
                let slot = reader.read_u32le()?;
                let data_offset = reader.read_u32le()? as u64;
                let item_size = reader.read_u32le()? as usize;
                reader.seek(SeekFrom::Start(extra_data_base + data_offset))?;
                let mut extra_data = vec![0u8; item_size * vertex_count as usize];
                reader.read_exact(&mut extra_data)?;
                extra_slots.insert(slot,extra_data);
            }
        }
    }

    Ok(VertexBuffer {
        vertex_count,
        vertex_data,
        extra_slots,
    })
}