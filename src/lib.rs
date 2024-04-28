#![allow(dead_code)]

use std::io::Cursor;
use std::path::PathBuf;

use pyo3::exceptions::{PyBufferError, PyException};
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
    fn from_path<'p>(_: &Bound<'p, PyType>, path: &Bound<'p, PyAny>) -> PyResult<Self> {
        Ok(Vpk {
            vpk: InnerVpk::from_path(&path.extract::<PathBuf>()?)?
        })
    }

    fn find_file<'p>(&mut self, py: Python<'p>, path: &Bound<'p, PyAny>) -> PyResult<Option<Bound<'p, PyBytes>>> {
        match self.vpk.find_file(&path.to_string()) {
            None => { Ok(None) }
            Some(data) => { Ok(Some(PyBytes::new_bound(py, data.as_slice()))) }
        }
    }

    fn glob<'p>(&mut self, py: Python<'p>, pattern: &Bound<'p, PyString>) -> PyResult<Vec<(Bound<'p, PyString>, Bound<'p, PyBytes>)>> {
        let res = self.vpk.filter(pattern.to_string().as_str());
        Ok(res.iter().map(|(key, data)| {
            (PyString::new_bound(py, key.as_str()), PyBytes::new_bound(py, data.as_slice()))
        }).collect())
    }

    fn __contains__(&mut self, path: &Bound<PyAny>) -> PyResult<bool> {
        Ok(self.vpk.contains(&path.to_string()))
    }
}


#[pyfunction]
#[pyo3(signature = (vertex_count, vertex_size, input_data), name = "decode_vertex_buffer")]
pub fn py_decode_vertex_buffer(py: Python, vertex_count: u32, vertex_size: u32, input_data: Vec<u8>) -> PyResult<Bound<PyBytes>> {
    let mut dest = vec![0u8; (vertex_size * vertex_count) as usize];
    decode_vertex_buffer(dest.as_mut_slice(), vertex_count as usize, vertex_size as usize, input_data.as_slice()).map_err(|e| { PyException::new_err(e.to_string()) })?;
    Ok(PyBytes::new_bound(py, dest.as_slice()))
}

#[pyfunction]
#[pyo3(signature = (index_count, index_size, input_data), name = "decode_index_buffer")]
pub fn py_decode_index_buffer(py: Python, index_count: u32, index_size: u32, input_data: Vec<u8>) -> PyResult<Bound<PyBytes>> {
    let mut dest = vec![0u8; (index_count * index_size) as usize];
    decode_index_buffer(dest.as_mut_slice(), index_count as usize, index_size as usize, input_data.as_slice()).map_err(|e| { PyException::new_err(e.to_string()) })?;
    Ok(PyBytes::new_bound(py, dest.as_slice()))
}

pub fn py_zstd_decompress(py: Python, input_data: Vec<u8>, decompressed_size: usize) -> PyResult<Bound<PyBytes>> {
    let mut dest = vec![0u8; decompressed_size];
    zstd_decompress(&mut dest, input_data.as_slice()).map_err(|e| { PyBufferError::new_err(e.to_string()) })?;
    Ok(PyBytes::new_bound(py, dest.as_slice()))
}

pub fn py_zstd_decompress_stream(py: Python, input_data: Vec<u8>) -> PyResult<Bound<PyBytes>> {
    let dest = decode_all(Cursor::new(input_data.as_slice())).map_err(|e| { PyBufferError::new_err(e.to_string()) })?;
    Ok(PyBytes::new_bound(py, dest.as_slice()))
}

/// A Python module implemented in Rust.
#[pymodule]
fn rustlib(_py: Python, m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(py_decode_vertex_buffer, m)?)?;
    m.add_function(wrap_pyfunction!(py_decode_index_buffer, m)?)?;
    m.add_class::<Vpk>()?;
    Ok(())
}
