#![allow(dead_code)]

use std::ffi::c_int;
use std::io::Cursor;
use std::path::PathBuf;

use lz4_sys::{LZ4_compress_default, LZ4_decompress_safe};
use pyo3::exceptions::{PyBufferError, PyException};
use pyo3::prelude::*;
use pyo3::types::{PyBytes, PyString, PyType};
use zstd::stream::{copy_encode as zstd_encode_stream, decode_all as zstd_decode_stream};
use zstd::zstd_safe::compress as zstd_compress;
use zstd::zstd_safe::compress_bound as zstd_compress_bound;
use zstd::zstd_safe::decompress as zstd_decompress;

use utils::decode_index_buffer;
use utils::decode_vertex_buffer;

use crate::utils::lz4_chain::LZ4ChainDecoder as LZ4ChainDecoderInner;
use crate::vpk::Vpk as InnerVpk;

mod utils;
mod dmx;
mod shared;
mod vpk;
mod errors;

#[pyclass]
pub struct Vpk {
    pub vpk: InnerVpk,
}

#[pymethods]
impl Vpk {
    #[classmethod]
    #[pyo3(signature = (path))]
    fn from_path<'p>(_: &Bound<'p, PyType>, path: &Bound<'p, PyAny>) -> PyResult<Self> {
        Ok(Vpk {
            vpk: InnerVpk::from_path(&path.extract::<PathBuf>()?)?
        })
    }

    #[pyo3(signature = (path))]
    fn find_file<'p>(&mut self, py: Python<'p>, path: &Bound<'p, PyAny>) -> PyResult<Option<Bound<'p, PyBytes>>> {
        match self.vpk.find_file(&path.to_string()) {
            None => { Ok(None) }
            Some(data) => { Ok(Some(PyBytes::new_bound(py, data.as_slice()))) }
        }
    }

    #[pyo3(signature = (pattern))]
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
#[pyo3(signature = (input_data, vertex_size, vertex_count, ), name = "decode_vertex_buffer")]
pub fn py_decode_vertex_buffer(py: Python, input_data: Vec<u8>, vertex_size: u32, vertex_count: u32) -> PyResult<Bound<PyBytes>> {
    let mut dest = vec![0u8; (vertex_size * vertex_count) as usize];
    decode_vertex_buffer(dest.as_mut_slice(), vertex_count as usize, vertex_size as usize, input_data.as_slice()).map_err(|e| { PyException::new_err(e.to_string()) })?;
    Ok(PyBytes::new_bound(py, dest.as_slice()))
}

#[pyfunction]
#[pyo3(signature = (input_data, index_size, index_count ), name = "decode_index_buffer")]
pub fn py_decode_index_buffer(py: Python, input_data: Vec<u8>, index_size: u32, index_count: u32) -> PyResult<Bound<PyBytes>> {
    let mut dest = vec![0u8; (index_count * index_size) as usize];
    decode_index_buffer(dest.as_mut_slice(), index_count as usize, index_size as usize, input_data.as_slice()).map_err(|e| { PyException::new_err(e.to_string()) })?;
    Ok(PyBytes::new_bound(py, dest.as_slice()))
}

#[pyfunction]
#[pyo3(signature = (input_data, decompressed_size), name = "zstd_decompress")]
pub fn py_zstd_decompress(py: Python, input_data: Vec<u8>, decompressed_size: usize) -> PyResult<Bound<PyBytes>> {
    let mut dest = vec![0u8; decompressed_size];
    zstd_decompress(&mut dest, input_data.as_slice()).map_err(|e| { PyBufferError::new_err(e.to_string()) })?;
    Ok(PyBytes::new_bound(py, dest.as_slice()))
}

#[pyfunction]
#[pyo3(signature = (input_data), name = "zstd_compress")]
pub fn py_zstd_compress(py: Python, input_data: Vec<u8>) -> PyResult<Bound<PyBytes>> {
    let mut dest = vec![0u8; 16];
    let dest_size = zstd_compress(&mut dest, input_data.as_slice(), 0).map_err(|e| { PyBufferError::new_err(e.to_string()) })?;
    Ok(PyBytes::new_bound(py, &dest[..dest_size]))
}

#[pyfunction]
#[pyo3(signature = (input_data), name = "zstd_compress_stream")]
pub fn py_zstd_compress_stream(py: Python, input_data: Vec<u8>) -> PyResult<Bound<PyBytes>> {
    let mut dest = vec![0u8; zstd_compress_bound(input_data.len())];
    zstd_encode_stream(Cursor::new(input_data), Cursor::new(&mut dest), 0)?;
    Ok(PyBytes::new_bound(py, dest.as_slice()))
}

#[pyfunction]
#[pyo3(signature = (input_data), name = "zstd_decompress_stream")]
pub fn py_zstd_decompress_stream(py: Python, input_data: Vec<u8>) -> PyResult<Bound<PyBytes>> {
    let dest = zstd_decode_stream(Cursor::new(input_data.as_slice())).map_err(|e| { PyBufferError::new_err(e.to_string()) })?;
    Ok(PyBytes::new_bound(py, dest.as_slice()))
}

#[pyfunction]
#[pyo3(signature = (input_data, decompressed_size), name = "lz4_decompress")]
pub fn py_lz4_decompress(py: Python, input_data: Vec<u8>, decompressed_size: u32) -> PyResult<Bound<PyBytes>> {
    let mut data = vec![0u8; decompressed_size as usize];
    let real_decompressed_size = unsafe { LZ4_decompress_safe(input_data.as_ptr().cast(), data.as_mut_ptr().cast(), input_data.len() as c_int, decompressed_size as c_int) as u32 };
    if real_decompressed_size != decompressed_size {
        return Err(PyBufferError::new_err("Decompressed size does not match"));
    }
    Ok(PyBytes::new_bound(py, data.as_slice()))
}

#[pyfunction]
#[pyo3(signature = (input_data), name = "lz4_compress")]
pub fn py_lz4_compress(py: Python, input_data: Vec<u8>) -> PyResult<Bound<PyBytes>> {
    let mut dst = vec![0u8; input_data.len()];
    let real_compressed_size = unsafe { LZ4_compress_default(input_data.as_ptr().cast(), dst.as_mut_ptr().cast(), input_data.len() as c_int, dst.len() as c_int) as u32 };

    Ok(PyBytes::new_bound(py, &dst[..real_compressed_size as usize]))
}

#[pyclass(unsendable)]
struct LZ4ChainDecoder {
    inner: LZ4ChainDecoderInner,
}

#[pymethods]
impl LZ4ChainDecoder {
    #[new]
    #[pyo3(signature = (block_size, extra_blocks))]
    fn new(block_size: u32, extra_blocks: u32) -> Self {
        LZ4ChainDecoder {
            inner: LZ4ChainDecoderInner::new(block_size as usize, extra_blocks as usize)
        }
    }

    fn __del__(&mut self) {
        self.inner.free();
    }

    #[pyo3(signature = (src, block_size))]
    fn decompress<'p>(&mut self, py: Python<'p>, src: Vec<u8>, block_size: u32) -> PyResult<Bound<'p, PyBytes>> {
        let mut dst = vec![0u8; block_size as usize];

        self.inner.decode_and_drain(src.as_slice(), dst.as_mut_slice()).map_err(|e| { PyBufferError::new_err(e) })?;
        Ok(PyBytes::new_bound(py, dst.as_slice()))
    }
}

#[pymodule]
fn rustlib(_: Python, m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add_class::<Vpk>()?;
    m.add_class::<LZ4ChainDecoder>()?;
    m.add_function(wrap_pyfunction!(py_decode_vertex_buffer, m)?)?;
    m.add_function(wrap_pyfunction!(py_decode_index_buffer, m)?)?;
    m.add_function(wrap_pyfunction!(py_zstd_compress, m)?)?;
    m.add_function(wrap_pyfunction!(py_zstd_decompress, m)?)?;
    m.add_function(wrap_pyfunction!(py_zstd_compress_stream, m)?)?;
    m.add_function(wrap_pyfunction!(py_zstd_decompress_stream, m)?)?;
    m.add_function(wrap_pyfunction!(py_lz4_compress, m)?)?;
    m.add_function(wrap_pyfunction!(py_lz4_decompress, m)?)?;
    Ok(())
}
