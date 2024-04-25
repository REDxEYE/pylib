use std::io;
use std::io::{Read, Seek};

use byteorder::{LE, ReadBytesExt};

use crate::utils::reader_utils::FromReader;

pub trait FromSlice<InnerType: Sized, const SIZE: usize>: Sized {
    fn from_slice(data: &[InnerType; SIZE]) -> Self;
}

pub trait ArrayFromReader<InnerType, ResType, const SIZE: usize> where ResType: FromSlice<InnerType, SIZE> {
    fn array_from_reader(reader: &mut impl Read) -> io::Result<Vec<ResType>>;
}


#[derive(Debug, Clone, Default, PartialEq, Eq)]
#[repr(C)]
pub struct Color {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub a: u8,
}

#[derive(Debug, Clone, Default, PartialEq)]
#[repr(C)]
pub struct Vector2 {
    pub x: f32,
    pub y: f32,
}

#[derive(Debug, Clone, Default, PartialEq)]
#[repr(C)]
pub struct Vector3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl From<Vector3> for nalgebra::Vector3<f32> {
    fn from(value: Vector3) -> Self {
        nalgebra::Vector3::new(value.x, value.y, value.z)
    }
}


#[derive(Debug, Clone, Default, PartialEq)]
#[repr(C)]
pub struct Vector4 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub w: f32,
}

#[derive(Debug, Clone, Default, PartialEq)]
#[repr(C)]
pub struct Matrix {
    pub m: [f32; 16],
}

impl<R: Read + Seek> FromReader<R> for Color {
    #[inline(always)]
    fn from_reader(reader: &mut R) -> io::Result<Self> {
        let mut data = [0u8; 4];
        reader.read_exact(&mut data)?;
        Ok(Color { r: data[0], g: data[1], b: data[2], a: data[3] })
    }
}

impl FromSlice<u8, 4> for Color {
    #[inline(always)]
    fn from_slice(data: &[u8; 4]) -> Color {
        Color { r: data[0], g: data[1], b: data[2], a: data[3] }
    }
}

impl ArrayFromReader<u8, Color, 4> for Color {
    #[inline(always)]
    fn array_from_reader(reader: &mut impl Read) -> io::Result<Vec<Self>> {
        let count = reader.read_u32::<LE>()? as usize;
        let mut buf = vec![0u8; count * 4];
        reader.read_exact(&mut buf)?;
        let items = buf.chunks_exact(4)
            .map(|chunk| { Self::from_slice(chunk.try_into().unwrap()) })
            .collect();
        Ok(items)
    }
}

impl<R: Read + Seek> FromReader<R> for Vector2 {
    #[inline(always)]
    fn from_reader(reader: &mut R) -> io::Result<Self> {
        let mut data = [0f32; 2];
        reader.read_f32_into::<LE>(&mut data)?;
        Ok(Vector2 { x: data[0], y: data[1] })
    }
}

impl FromSlice<f32, 2> for Vector2 {
    #[inline(always)]
    fn from_slice(data: &[f32; 2]) -> Vector2 {
        Vector2 { x: data[0], y: data[1] }
    }
}

impl ArrayFromReader<f32, Vector2, 2> for Vector2 {
    #[inline(always)]
    fn array_from_reader(reader: &mut impl Read) -> io::Result<Vec<Self>> {
        let count = reader.read_u32::<LE>()? as usize;
        let mut buf = vec![0f32; count * 2];
        reader.read_f32_into::<LE>(&mut buf)?;
        let items = buf.chunks_exact(2)
            .map(|chunk| { Self::from_slice(chunk.try_into().unwrap()) })
            .collect();
        Ok(items)
    }
}

impl<R: Read + Seek> FromReader<R> for Vector3 {
    #[inline(always)]
    fn from_reader(reader: &mut R) -> io::Result<Self> {
        let mut data = [0f32; 3];
        reader.read_f32_into::<LE>(&mut data)?;
        Ok(Vector3 { x: data[0], y: data[1], z: data[2] })
    }
}

impl FromSlice<f32, 3> for Vector3 {
    #[inline(always)]
    fn from_slice(data: &[f32; 3]) -> Vector3 {
        Vector3 { x: data[0], y: data[1], z: data[2] }
    }
}

impl ArrayFromReader<f32, Vector3, 3> for Vector3 {
    #[inline(always)]
    fn array_from_reader(reader: &mut impl Read) -> io::Result<Vec<Self>> {
        let count = reader.read_u32::<LE>()? as usize;
        let mut buf = vec![0f32; count * 3];
        reader.read_f32_into::<LE>(&mut *buf)?;
        let items = buf.chunks_exact(3)
            .map(|chunk| { Self::from_slice(chunk.try_into().unwrap()) })
            .collect();
        Ok(items)
    }
}

impl<R: Read + Seek> FromReader<R> for Vector4 {
    #[inline(always)]
    fn from_reader(reader: &mut R) -> io::Result<Self> {
        let mut data = [0f32; 4];
        reader.read_f32_into::<LE>(&mut data)?;
        Ok(Vector4 { x: data[0], y: data[1], z: data[2], w: data[3] })
    }
}

impl FromSlice<f32, 4> for Vector4 {
    #[inline(always)]
    fn from_slice(data: &[f32; 4]) -> Vector4 {
        Vector4 { x: data[0], y: data[1], z: data[2], w: data[3] }
    }
}

impl ArrayFromReader<f32, Vector4, 4> for Vector4 {
    #[inline(always)]
    fn array_from_reader(reader: &mut impl Read) -> io::Result<Vec<Self>> {
        let count = reader.read_u32::<LE>()? as usize;
        let mut buf = vec![0f32; count * 4];
        reader.read_f32_into::<LE>(&mut buf)?;
        let items = buf.chunks_exact(4)
            .map(|chunk| { Self::from_slice(chunk.try_into().unwrap()) })
            .collect();
        Ok(items)
    }
}

impl<R: Read + Seek> FromReader<R> for Matrix {
    #[inline(always)]
    fn from_reader(reader: &mut R) -> io::Result<Self> {
        let mut data = [0f32; 16];
        reader.read_f32_into::<LE>(&mut data)?;
        Ok(Matrix { m: data })
    }
}

impl FromSlice<f32, 16> for Matrix {
    #[inline(always)]
    fn from_slice(data: &[f32; 16]) -> Matrix {
        Matrix { m: *data }
    }
}

impl ArrayFromReader<f32, Matrix, 16> for Matrix {
    #[inline(always)]
    fn array_from_reader(reader: &mut impl Read) -> io::Result<Vec<Matrix>> {
        let count = reader.read_u32::<LE>()? as usize;
        let mut buf = vec![0f32; count * 16];
        reader.read_f32_into::<LE>(&mut buf)?;
        let items = buf.chunks_exact(16)
            .map(|chunk| { Self::from_slice(chunk.try_into().unwrap()) })
            .collect();
        Ok(items)
    }
}



