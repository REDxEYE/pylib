use std::io::{Read, Seek};

use bitflags::bitflags;
use byteorder::ReadBytesExt;

use rustlib::utils::reader_utils::ReadExt;

use crate::utils::reader_utils::{FromReader, ReadSeekExt};

pub struct IndexHeader {
    version: u32,
    vertex_cache_size: u32,
    max_bones_per_strip: u16,
    max_bones_per_tri: u16,
    max_bones_per_vertex: u32,
    checksum: u32,
    lod_count: u32,
    material_replacement_list_offset: u32,
    bodypart_count: u32,
    bodypart_offset: u32,
}

impl<R: Read + Seek> FromReader<R> for IndexHeader {
    fn from_reader(reader: &mut R) -> std::io::Result<Self> {
        Ok(IndexHeader {
            version: reader.read_u32le()?,
            vertex_cache_size: reader.read_u32le()?,
            max_bones_per_strip: reader.read_u16le()?,
            max_bones_per_tri: reader.read_u16le()?,
            max_bones_per_vertex: reader.read_u32le()?,
            checksum: reader.read_u32le()?,
            lod_count: reader.read_u32le()?,
            material_replacement_list_offset: reader.read_u32le()?,
            bodypart_count: reader.read_u32le()?,
            bodypart_offset: reader.read_u32le()?,
        })
    }
}

pub struct IndexMaterialReplacementList {
    pub replacements_count: u32,
    pub replacements_offset: u64,
}

impl<R: Read + Seek> FromReader<R> for IndexMaterialReplacementList {
    fn from_reader(reader: &mut R) -> std::io::Result<Self> {
        let base = reader.stream_position()?;
        Ok(IndexMaterialReplacementList {
            replacements_count: reader.read_u32le()?,
            replacements_offset: reader.read_relptr(base)?,
        })
    }
}

pub struct IndexMaterialReplacement {
    pub material_id: u32,
    pub name: String,
}

impl<R: Read + Seek> FromReader<R> for IndexMaterialReplacement {
    fn from_reader(reader: &mut R) -> std::io::Result<Self> {
        let base = reader.stream_position()?;
        Ok(IndexMaterialReplacement {
            material_id: reader.read_u32le()?,
            name: reader.read_pztstring(base)?,
        })
    }
}

pub struct IndexBodypart {
    pub model_count: u32,
    pub model_offset: u64,
}

impl<R: Read + Seek> FromReader<R> for IndexBodypart {
    fn from_reader(reader: &mut R) -> std::io::Result<Self> {
        let base = reader.stream_position()?;
        Ok(IndexBodypart {
            model_count: reader.read_u32le()?,
            model_offset: reader.read_relptr(base)?,
        })
    }
}

pub struct IndexModel {
    pub lod_count: u32,
    pub lod_offset: u64,
}

impl<R: Read + Seek> FromReader<R> for IndexModel {
    fn from_reader(reader: &mut R) -> std::io::Result<Self> {
        let base = reader.stream_position()?;
        Ok(IndexModel {
            lod_count: reader.read_u32le()?,
            lod_offset: reader.read_relptr(base)?,
        })
    }
}

pub struct IndexModelLod {
    pub mesh_count: u32,
    pub mesh_offset: u64,
    pub switch_point: f32,
}

impl<R: Read + Seek> FromReader<R> for IndexModelLod {
    fn from_reader(reader: &mut R) -> std::io::Result<Self> {
        let base = reader.stream_position()?;
        Ok(IndexModelLod {
            mesh_count: reader.read_u32le()?,
            mesh_offset: reader.read_relptr(base)?,
            switch_point: reader.read_f32le()?,
        })
    }
}

pub struct IndexMesh {
    pub strip_group_count: u32,
    pub strip_group_offset: u64,
    pub flags: u8,
}

impl<R: Read + Seek> FromReader<R> for IndexMesh {
    fn from_reader(reader: &mut R) -> std::io::Result<Self> {
        let base = reader.stream_position()?;
        Ok(IndexMesh {
            strip_group_count: reader.read_u32le()?,
            strip_group_offset: reader.read_relptr(base)?,
            flags: reader.read_u8()?,
        })
    }
}

bitflags! {
    #[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
    pub struct IndexStripGroupFlags: u8 {
        const IS_FLEXED = 0x01;
        const IS_HWSKINNED = 0x02;
        const IS_DELTA_FLEXED = 0x04;
        const SUPPRESS_HW_MORPH = 0x08;
    }
}

pub struct IndexStripGroup {
    pub vertex_count: u32,
    pub vertex_offset: u64,
    pub index_count: u32,
    pub index_offset: u64,
    pub strip_count: u32,
    pub strip_offset: u64,
    pub flags: IndexStripGroupFlags,
}

impl<R: Read + Seek> FromReader<R> for IndexStripGroup {
    fn from_reader(reader: &mut R) -> std::io::Result<Self> {
        let base = reader.stream_position()?;
        Ok(IndexStripGroup {
            vertex_count: reader.read_u32le()?,
            vertex_offset: reader.read_relptr(base)?,
            index_count: reader.read_u32le()?,
            index_offset: reader.read_relptr(base)?,
            strip_count: reader.read_u32le()?,
            strip_offset: reader.read_relptr(base)?,
            flags: IndexStripGroupFlags::from_bits_retain(reader.read_u8()?),
        })
    }
}

bitflags! {
    #[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
    pub struct IndexStripHeaderFlags: u8 {
        const IS_TRILIST = 0x01;
        const IS_QUADLIST_REG = 0x02;
        const IS_QUADLIST_EXTRA = 0x04;
    }
}

pub struct IndexStrip {
    pub index_count: u32,
    pub index_mesh_offset: u64,
    pub vertex_count: u32,
    pub vertex_mesh_offset: u64,
    pub bone_count: u16,
    pub flags: IndexStripHeaderFlags,
    pub bone_state_change_count: u32,
    pub bone_state_change_offset: u32,

}

impl<R: Read + Seek> FromReader<R> for IndexStrip {
    fn from_reader(reader: &mut R) -> std::io::Result<Self> {
        let base = reader.stream_position()?;
        Ok(IndexStrip {
            index_count: reader.read_u32le()?,
            index_mesh_offset: reader.read_relptr(base)?,
            vertex_count: reader.read_u32le()?,
            vertex_mesh_offset: reader.read_relptr(base)?,
            bone_count: reader.read_u16le()?,
            flags: IndexStripHeaderFlags::from_bits_retain(reader.read_u8()?),
            bone_state_change_count: reader.read_u32le()?,
            bone_state_change_offset: reader.read_u32le()?,
        })
    }
}