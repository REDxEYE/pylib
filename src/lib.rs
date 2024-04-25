#![allow(dead_code)]

use std::io::Cursor;
use std::path::Path;

use pyo3::prelude::*;
use pyo3::types::{PyBytes, PyString, PyType};
use zstd::stream::decode_all;
use zstd::zstd_safe::decompress as zstd_decompress;

pub use utils::decode_index_buffer;
pub use utils::decode_vertex_buffer;

use crate::vpk::Vpk as InnerVpk;

pub mod utils;
pub mod dmx;
pub mod shared;
pub mod vpk;
mod errors;

#[pyclass]
pub struct Vpk {
    pub vpk: InnerVpk,
}

#[pymethods]
impl Vpk {
    #[classmethod]
    fn from_path<'p>(_: &Bound<'p, PyType>, path: &Bound<'p, PyString>) -> PyResult<Self> {
        let string = path.to_string();
        return Ok(Vpk {
            vpk: InnerVpk::from_path(Path::new(&string)).expect("Failed to open VPK")
        });
    }

    fn find_file<'p>(&mut self, py: Python<'p>, path: &Bound<'p, PyString>) -> PyResult<Option<Bound<'p, PyBytes>>> {
        match self.vpk.find_file(path.to_string()) {
            None => { Ok(None) }
            Some(res) => { Ok(Some(PyBytes::new_bound(py,res.as_slice()))) }
        }
    }
}


#[pyfunction]
#[pyo3(signature = (vertex_count, vertex_size, input_data), name = "decode_vertex_buffer")]
pub fn py_decode_vertex_buffer<'p>(py: Python<'p>, vertex_count: u32, vertex_size: u32, input_data: &Bound<'p, PyBytes>) -> Bound<'p, PyBytes> {
    let mut dest = vec![0u8; (vertex_size * vertex_count) as usize];
    decode_vertex_buffer(dest.as_mut_slice(), vertex_count as usize, vertex_size as usize, input_data.as_bytes()).expect("Failed to decode vertex buffer");
    return PyBytes::new_bound(py, dest.as_slice());
}

#[pyfunction]
#[pyo3(signature = (index_count, index_size, input_data), name = "decode_index_buffer")]
pub fn py_decode_index_buffer<'p>(py: Python<'p>, index_count: u32, index_size: u32, input_data: &Bound<'p, PyBytes>) -> Bound<'p, PyBytes> {
    let mut dest = vec![0u8; (index_count * index_size) as usize];
    decode_index_buffer(dest.as_mut_slice(), index_count as usize, index_size as usize, input_data.as_bytes()).expect("Failed to decode index buffer");
    return PyBytes::new_bound(py, dest.as_slice());
}

pub fn py_zstd_decompress<'p>(py: Python<'p>, input_data: &Bound<'p, PyBytes>, decompressed_size: usize) -> Bound<'p, PyBytes> {
    let mut dest = vec![0u8; decompressed_size];
    zstd_decompress(&mut dest, input_data.as_bytes()).expect("Failed to decompress ZSTD data");
    return PyBytes::new_bound(py, dest.as_slice());
}

pub fn py_zstd_decompress_stream<'p>(py: Python<'p>, input_data: &Bound<'p, PyBytes>) -> Bound<'p, PyBytes> {
    let dest = decode_all(Cursor::new(input_data.as_bytes())).expect("Failed to decompress ZSTD data stream");
    return PyBytes::new_bound(py, dest.as_slice());
}

/// A Python module implemented in Rust.
#[pymodule]
fn rustlib(_py: Python, m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(py_decode_vertex_buffer, m)?)?;
    m.add_function(wrap_pyfunction!(py_decode_index_buffer, m)?)?;
    m.add_class::<Vpk>()?;
    Ok(())
}
