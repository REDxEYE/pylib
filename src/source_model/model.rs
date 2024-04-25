use std::collections::HashMap;
use std::fmt::{Debug, Formatter};
use std::ops::Div;

use nalgebra::Matrix4;

#[derive(Debug)]
pub struct Bone {
    pub name: String,
    pub parent: i32,
    pub matrix: Matrix4<f32>,

}

#[derive(Debug)]
pub struct SubMesh {
    pub name: String,
    pub indices_offset: u32,
    pub indices_count: u32,
    pub vertex_offset: u32,
    pub vertex_count: u32,
    pub sparse_material_id_list: Vec<(u32, u32)>,
}

#[derive(Debug)]
pub struct Model {
    pub name: String,
    pub bones: Vec<Bone>,
    pub materials: Vec<String>,
    pub sub_meshes: Vec<SubMesh>,
    pub vertex_buffer: VertexBuffer,
    pub index_buffer: IndexBuffer,
}

pub struct VertexBuffer {
    pub vertex_count: u32,
    pub vertex_data: Vec<u8>,
    pub extra_slots: HashMap<u32, Vec<u8>>,
}

impl Debug for VertexBuffer {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "VertexBuffer(vertex count={}, vertex size={}, extra attributes={})", self.vertex_count, self.vertex_data.len().div(self.vertex_count as usize), self.extra_slots.len())
    }
}

pub struct IndexBuffer {
    pub index_count: u32,
    pub index_data: Vec<u8>,
}

impl Debug for IndexBuffer {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "IndexBuffer(index count={}, index size={})", self.index_count, self.index_data.len().div(self.index_count as usize))
    }
}