#![allow(dead_code)]

use std::ffi::c_int;
use std::fs::File;
use std::io::{Cursor, Write};
use std::path::PathBuf;
use std::sync::Mutex;

use exr::image::{Image, SpecificChannels};
use exr::math::Vec2;
use exr::prelude::WritableImage;
use lz4_sys::{LZ4StreamDecode, LZ4_compress_default, LZ4_decompress_safe, LZ4_decompress_safe_continue};
use numpy::{PyArray1, PyArrayMethods};
use pyo3::exceptions::{PyBufferError, PyException, PyValueError};
use pyo3::prelude::*;
use pyo3::types::{PyBytes, PyString, PyType};
use rayon::prelude::*;
use vtflib2::{ImageFormat, MipmapFilter, VtfFile};
use zstd::stream::{copy_encode as zstd_encode_stream, decode_all as zstd_decode_stream};
use zstd::zstd_safe::compress as zstd_compress;
use zstd::zstd_safe::compress_bound as zstd_compress_bound;
use zstd::zstd_safe::decompress as zstd_decompress;

use utils::decode_index_buffer;
use utils::decode_vertex_buffer;

use crate::utils::bc6::decode_bc6;
use crate::utils::lz4_chain::{LZ4ChainDecoder as LZ4ChainDecoderInner, SAFE_C_INT_MAX};
use crate::vpk::Vpk as InnerVpk;
use crate::vpk::VpkReader;

pub mod dmx;
pub mod errors;
pub mod shared;
pub mod source_model;
pub mod utils;
pub mod vpk;

#[pyclass]
pub struct Vpk {
    pub vpk: Box<dyn VpkReader>,
}

#[pymethods]
impl Vpk {
    #[classmethod]
    #[pyo3(signature = (path))]
    fn from_path<'p>(_: &Bound<'p, PyType>, path: &Bound<'p, PyAny>) -> PyResult<Self> {
        Ok(Vpk {
            vpk: InnerVpk::from_path(&path.extract::<PathBuf>()?)?,
        })
    }

    #[pyo3(signature = (path))]
    fn find_file<'p>(
        &mut self,
        py: Python<'p>,
        path: &Bound<'p, PyAny>,
    ) -> PyResult<Option<Bound<'p, PyBytes>>> {
        match self.vpk.find_file(&path.to_string()) {
            None => Ok(None),
            Some(data) => Ok(Some(PyBytes::new_bound(py, data.as_slice()))),
        }
    }

    #[pyo3(signature = (pattern))]
    fn glob<'p>(
        &mut self,
        py: Python<'p>,
        pattern: &Bound<'p, PyString>,
    ) -> PyResult<Vec<(Bound<'p, PyString>, Bound<'p, PyBytes>)>> {
        let res = self.vpk.filter(pattern.to_string().as_str());
        Ok(res
            .iter()
            .map(|(key, data)| {
                (
                    PyString::new_bound(py, key.as_str()),
                    PyBytes::new_bound(py, data.as_slice()),
                )
            })
            .collect())
    }

    fn __contains__(&mut self, path: &Bound<PyAny>) -> PyResult<bool> {
        Ok(self.vpk.contains(&path.to_string()))
    }
}

// #[pyclass(unsendable)]
// struct LZ4ContinueDecoder {
//     inner: *mut LZ4StreamDecode,
//     ring_buffer: Vec<u8>,
//     ring_offset: usize,
// }
// 
// #[pymethods]
// impl LZ4ContinueDecoder {
//     #[new]
//     #[pyo3(signature = (block_size))]
//     fn new(block_size: u32) -> Self {
//         let ring_size = (65536 + 14 + (block_size));
//         LZ4ContinueDecoder {
//             inner: unsafe { LZ4_createStreamDecode() },
//             ring_buffer: vec![0; ring_size as usize],
//             ring_offset: 0,
//         }
//     }
// 
//     #[pyo3(signature = (src, decompressed_size))]
//     fn decompress<'p>(
//         &mut self,
//         py: Python<'p>,
//         src: Vec<u8>,
//         decompressed_size: u32,
//     ) -> PyResult<Bound<'p, PyBytes>> {
// 
//         unsafe{
//             LZ4_decompress_safe_continue(self.inner,src.as_ptr(),)
//         }
//         Ok(PyBytes::new_bound(py, dst.as_slice()))
//     }
// 
//     fn __del__(&mut self) {
//         unsafe { LZ4_freeStreamDecode(self.inner); }
//     }
// }

#[pyclass(unsendable)]
struct LZ4ChainDecoder {
    inner: LZ4ChainDecoderInner,
}

#[pymethods]
impl LZ4ChainDecoder {
    #[new]
    #[pyo3(signature = (block_size, extra_blocks))]
    fn new(block_size: u32, extra_blocks:u32) -> Self {
        LZ4ChainDecoder {
            inner: LZ4ChainDecoderInner::new(block_size as isize, extra_blocks as isize),
        }
    }

    fn __del__(&mut self) {
        self.inner.free();
    }

    #[pyo3(signature = (src, block_size))]
    fn decompress<'p>(
        &mut self,
        py: Python<'p>,
        src: Vec<u8>,
        block_size: u32,
    ) -> PyResult<Bound<'p, PyBytes>> {
        let mut dst = vec![0u8; block_size as usize];

        self.inner
            .decode_and_drain(src.as_slice(), dst.as_mut_slice())
            .map_err(|e| PyBufferError::new_err(e))?;
        Ok(PyBytes::new_bound(py, dst.as_slice()))
    }
}

#[pyfunction]
#[pyo3(signature = (input_data, vertex_size, vertex_count, ), name = "decode_vertex_buffer")]
pub fn py_decode_vertex_buffer(
    py: Python,
    input_data: Vec<u8>,
    vertex_size: u32,
    vertex_count: u32,
) -> PyResult<Bound<PyBytes>> {
    let mut dest = vec![0u8; (vertex_size * vertex_count) as usize];
    decode_vertex_buffer(
        dest.as_mut_slice(),
        vertex_count as usize,
        vertex_size as usize,
        input_data.as_slice(),
    )
        .map_err(|e| PyException::new_err(e.to_string()))?;
    Ok(PyBytes::new_bound(py, dest.as_slice()))
}

#[pyfunction]
#[pyo3(signature = (input_data, index_size, index_count ), name = "decode_index_buffer")]
pub fn py_decode_index_buffer(
    py: Python,
    input_data: Vec<u8>,
    index_size: u32,
    index_count: u32,
) -> PyResult<Bound<PyBytes>> {
    let mut dest = vec![0u8; (index_count * index_size) as usize];
    decode_index_buffer(
        dest.as_mut_slice(),
        index_count as usize,
        index_size as usize,
        input_data.as_slice(),
    )
        .map_err(|e| PyException::new_err(e.to_string()))?;
    Ok(PyBytes::new_bound(py, dest.as_slice()))
}

#[pyfunction]
#[pyo3(signature = (input_data, decompressed_size), name = "zstd_decompress")]
pub fn py_zstd_decompress(
    py: Python,
    input_data: Vec<u8>,
    decompressed_size: usize,
) -> PyResult<Bound<PyBytes>> {
    let mut dest = vec![0u8; decompressed_size];
    zstd_decompress(&mut dest, input_data.as_slice())
        .map_err(|e| PyBufferError::new_err(e.to_string()))?;
    Ok(PyBytes::new_bound(py, dest.as_slice()))
}

#[pyfunction]
#[pyo3(signature = (input_data), name = "zstd_compress")]
pub fn py_zstd_compress(py: Python, input_data: Vec<u8>) -> PyResult<Bound<PyBytes>> {
    let mut dest = vec![0u8; 16];
    let dest_size = zstd_compress(&mut dest, input_data.as_slice(), 0)
        .map_err(|e| PyBufferError::new_err(e.to_string()))?;
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
    let dest = zstd_decode_stream(Cursor::new(input_data.as_slice()))
        .map_err(|e| PyBufferError::new_err(e.to_string()))?;
    Ok(PyBytes::new_bound(py, dest.as_slice()))
}

#[pyfunction]
#[pyo3(signature = (input_data, decompressed_size), name = "lz4_decompress")]
pub fn py_lz4_decompress(
    py: Python,
    input_data: Vec<u8>,
    decompressed_size: u32,
) -> PyResult<Bound<PyBytes>> {
    if input_data.len() > SAFE_C_INT_MAX as usize || decompressed_size > SAFE_C_INT_MAX {
        return Err(PyValueError::new_err(
            "input_data or decompressed_size is too big",
        ));
    }
    let mut data = vec![0u8; decompressed_size as usize];
    let real_decompressed_size = unsafe {
        LZ4_decompress_safe(
            input_data.as_ptr().cast(),
            data.as_mut_ptr().cast(),
            input_data.len() as c_int,
            decompressed_size as c_int,
        ) as u32
    };
    if real_decompressed_size != decompressed_size {
        return Err(PyBufferError::new_err(format!(
            "Decompressed size does not match: {:?}!={:?}",
            decompressed_size, real_decompressed_size
        )));
    }
    Ok(PyBytes::new_bound(py, data.as_slice()))
}

#[pyfunction]
#[pyo3(signature = (context,input_data, decompressed_size), name = "lz4_decompress_continue")]
pub fn py_lz4_decompress_continue(
    py: Python,
    context: usize,
    input_data: Vec<u8>,
    decompressed_size: u32,
) -> PyResult<Bound<PyBytes>> {
    if input_data.len() > SAFE_C_INT_MAX as usize || decompressed_size > SAFE_C_INT_MAX {
        return Err(PyValueError::new_err(
            "input_data or decompressed_size is too big",
        ));
    }
    let mut data = vec![0u8; decompressed_size as usize];
    let real_decompressed_size = unsafe {
        LZ4_decompress_safe_continue(
            context as (*mut LZ4StreamDecode),
            input_data.as_ptr().cast(),
            data.as_mut_ptr().cast(),
            input_data.len() as c_int,
            decompressed_size as c_int,
        ) as u32
    };
    if real_decompressed_size != decompressed_size {
        return Err(PyBufferError::new_err(format!(
            "Decompressed size does not match: {:?}!={:?}",
            decompressed_size, real_decompressed_size
        )));
    }
    Ok(PyBytes::new_bound(py, data.as_slice()))
}

#[pyfunction]
#[pyo3(signature = (input_data), name = "lz4_compress")]
pub fn py_lz4_compress(py: Python, input_data: Vec<u8>) -> PyResult<Bound<PyBytes>> {
    if input_data.len() > SAFE_C_INT_MAX as usize {
        return Err(PyValueError::new_err(
            "input_data or decompressed_size is too big",
        ));
    }
    let mut dst = vec![0u8; input_data.len()];
    let real_compressed_size = unsafe {
        LZ4_compress_default(
            input_data.as_ptr().cast(),
            dst.as_mut_ptr().cast(),
            input_data.len() as c_int,
            dst.len() as c_int,
        ) as u32
    };

    Ok(PyBytes::new_bound(
        py,
        &dst[..real_compressed_size as usize],
    ))
}

#[pyfunction]
#[pyo3(signature = (vtf_data), name = "load_vtf_texture")]
pub fn py_load_vtf_texture(
    py: Python,
    vtf_data: Vec<u8>,
) -> PyResult<(Bound<PyBytes>, u32, u32, u32)> {
    let mut vtf = VtfFile::new();
    match vtf.load(vtf_data.as_slice()) {
        Ok(_) => {}
        Err(_) => {
            return Err(PyException::new_err("Failed to load VTF"));
        }
    };
    if !vtf.has_image() {
        return Err(PyException::new_err("VTF image wasnt loaded"));
    }
    let format = vtf
        .format()
        .ok_or(PyException::new_err("Failed to get VTFFile format"))?;
    let pixel_data = vtf
        .data(0, 0, 0, 0)
        .ok_or(PyException::new_err("Failed to get pixel data"))?;
    let (converted_data, bpp) = match format {
        ImageFormat::Rgba8888 => (Vec::from(pixel_data), 8u32),
        ImageFormat::Agbr8888
        | ImageFormat::Rgb888
        | ImageFormat::Bgr888
        | ImageFormat::Rgb565
        | ImageFormat::I8
        | ImageFormat::Ia88
        | ImageFormat::P8
        | ImageFormat::A8
        | ImageFormat::Rgb888Bluescreen
        | ImageFormat::Bgr888Bluescreen
        | ImageFormat::Argb8888
        | ImageFormat::Bgra8888
        | ImageFormat::Dxt1
        | ImageFormat::Dxt3
        | ImageFormat::Dxt5
        | ImageFormat::Bgrx8888
        | ImageFormat::Bgr565
        | ImageFormat::Bgrx5551
        | ImageFormat::Bgra4444
        | ImageFormat::Dxt1OneBitAlpha
        | ImageFormat::Bgra5551
        | ImageFormat::Uv88
        | ImageFormat::Uvlx8888
        | ImageFormat::Ati2N
        | ImageFormat::Ati1N
        | ImageFormat::Uvwq8888 => (
            VtfFile::convert_image_to_rgba8888(pixel_data, vtf.width(), vtf.height(), format)
                .map_err(|_| PyException::new_err("Failed to convert to RGBA8888"))?,
            8u32,
        ),
        ImageFormat::Rgba16161616F
        | ImageFormat::Rgba16161616
        | ImageFormat::R32F
        | ImageFormat::Rgb323232F
        | ImageFormat::Rgba32323232F => (
            VtfFile::convert_image(
                pixel_data,
                vtf.width(),
                vtf.height(),
                format,
                ImageFormat::Rgba32323232F,
            )
                .map_err(|_| PyException::new_err("Failed to convert to RGBA8888"))?,
            32u32,
        ),
        ImageFormat::NvDst16 => {
            return Err(PyException::new_err("Unsupported format"));
        }
        ImageFormat::NvDst24 => {
            return Err(PyException::new_err("Unsupported format"));
        }
        ImageFormat::NvIntz => {
            return Err(PyException::new_err("Unsupported format"));
        }
        ImageFormat::NvRawz => {
            return Err(PyException::new_err("Unsupported format"));
        }
        ImageFormat::AtiDst16 => {
            return Err(PyException::new_err("Unsupported format"));
        }
        ImageFormat::AtiDst24 => {
            return Err(PyException::new_err("Unsupported format"));
        }
        ImageFormat::NvNull => {
            return Err(PyException::new_err("Unsupported format"));
        }
    };
    Ok((
        PyBytes::new_bound(py, converted_data.as_slice()),
        vtf.width(),
        vtf.height(),
        bpp,
    ))
}

#[pyfunction]
#[pyo3(signature = (output_path, width, height, format, generate_mips, resize, version, resize_size, pixel_data), name = "save_vtf_texture"
)]
pub fn py_save_vtf_texture<'p>(
    py: Python<'p>,
    output_path: &Bound<'p, PyAny>,
    width: u32,
    height: u32,
    format: u32,
    generate_mips: bool,
    resize: bool,
    version: (u32, u32),
    resize_size: (u32, u32),
    pixel_data: Vec<u8>,
) -> PyResult<()> {
    let mut vtf = VtfFile::new();
    let mut pixels = pixel_data.clone();
    vtf.from_rgba8888(width, height)
        .version(version.0, version.1)
        .format(unsafe { std::mem::transmute(format) })
        .create(pixels.as_mut_slice())
        .map_err(|e| PyException::new_err(format!("Failed to create VTFFile {:?}", e)))?;
    let vtf_data = vtf
        .save_to_vec()
        .map_err(|e| PyException::new_err(format!("Failed to save VTFFile to memory {:?}", e)))?;
    let mut file = File::open(output_path.extract::<PathBuf>()?)?;
    file.write_all(vtf_data.as_slice())
        .map_err(|e| PyException::new_err(format!("Failed to save VTFFile to disk {:?}", e)))?;
    return Ok(());
}

#[pyfunction]
#[pyo3(signature = (data, width, height, format), name = "decode_texture")]
pub fn py_decode_texture<'py>(
    py: Python<'py>,
    data: Vec<u8>,
    width: u32,
    height: u32,
    format: &str,
) -> PyResult<Bound<'py, PyBytes>> {
    return if format == "BC6" {
        let mut pixels = vec![0f32; (width * height * 3) as usize];
        decode_bc6(
            &mut Cursor::new(data),
            &mut pixels,
            width as usize,
            height as usize,
            false,
        )
            .map_err(|e| PyException::new_err(e.to_string()))?;
        let decompressed = Mutex::new(vec![0; (height * width * 4 * 4) as usize]); // Use Mutex to protect the vector

        const CHUNK_SIZE: usize = 128 * 128;
        pixels
            .par_chunks(CHUNK_SIZE)
            .enumerate()
            .for_each(|(index, chunk)| {
                let mut local_buf = vec![0; chunk.len() * 4];
                chunk.iter().enumerate().for_each(|(i, pixel)| {
                    let bytes = pixel.to_le_bytes();
                    let pos = i * 4;
                    local_buf[pos..pos + 4].copy_from_slice(&bytes);
                });
                let mut decompressed = decompressed.lock().unwrap();
                let start = index * CHUNK_SIZE * 4;
                decompressed[start..start + local_buf.len()].copy_from_slice(&local_buf);
            });

        Ok(PyBytes::new_bound(
            py,
            decompressed.into_inner().unwrap().as_slice(),
        ))
    } else {
        let mut pixels = vec![0u32; (width * height) as usize];
        match format {
            "BC1" | "DXT1" => {
                texture2ddecoder::decode_bc1(
                    data.as_slice(),
                    width as usize,
                    height as usize,
                    pixels.as_mut_slice(),
                )
                    .map_err(|e| PyException::new_err(e))?;
            }
            "BC3" | "DXT5" => {
                texture2ddecoder::decode_bc3(
                    data.as_slice(),
                    width as usize,
                    height as usize,
                    pixels.as_mut_slice(),
                )
                    .map_err(|e| PyException::new_err(e))?;
            }
            "BC4" | "ATI1N" => {
                texture2ddecoder::decode_bc4(
                    data.as_slice(),
                    width as usize,
                    height as usize,
                    pixels.as_mut_slice(),
                )
                    .map_err(|e| PyException::new_err(e))?;
            }
            "BC5" | "ATI2N" => {
                texture2ddecoder::decode_bc5(
                    data.as_slice(),
                    width as usize,
                    height as usize,
                    pixels.as_mut_slice(),
                )
                    .map_err(|e| PyException::new_err(e))?;
            }
            "BC7" => {
                texture2ddecoder::decode_bc7(
                    data.as_slice(),
                    width as usize,
                    height as usize,
                    pixels.as_mut_slice(),
                )
                    .map_err(|e| PyException::new_err(e))?;
            }
            "ETC1" => {
                texture2ddecoder::decode_etc1(
                    data.as_slice(),
                    width as usize,
                    height as usize,
                    pixels.as_mut_slice(),
                )
                    .map_err(|e| PyException::new_err(e))?;
            }
            "ETC2" => {
                texture2ddecoder::decode_etc2_rgba8(
                    data.as_slice(),
                    width as usize,
                    height as usize,
                    pixels.as_mut_slice(),
                )
                    .map_err(|e| PyException::new_err(e))?;
            }
            "EACRG" => {
                texture2ddecoder::decode_eacrg(
                    data.as_slice(),
                    width as usize,
                    height as usize,
                    pixels.as_mut_slice(),
                )
                    .map_err(|e| PyException::new_err(e))?;
            }
            "EACR" => {
                texture2ddecoder::decode_eacr(
                    data.as_slice(),
                    width as usize,
                    height as usize,
                    pixels.as_mut_slice(),
                )
                    .map_err(|e| PyException::new_err(e))?;
            }
            _ => {
                return Err(PyException::new_err(format!(
                    "Unsupported format: {}",
                    format
                )));
            }
        };
        let decompressed = Mutex::new(vec![0; (height * width * 4) as usize]); // Use Mutex to protect the vector

        const CHUNK_SIZE: usize = 128 * 128;
        pixels
            .par_chunks(CHUNK_SIZE)
            .enumerate()
            .for_each(|(index, chunk)| {
                let mut local_buf = vec![0; chunk.len() * 4];
                chunk.iter().enumerate().for_each(|(i, pixel)| {
                    let bytes = pixel.to_le_bytes();
                    let pos = i * 4;
                    local_buf[pos] = bytes[2];
                    local_buf[pos + 1] = bytes[1];
                    local_buf[pos + 2] = bytes[0];
                    local_buf[pos + 3] = bytes[3];
                });
                let mut decompressed = decompressed.lock().unwrap();
                let start = index * CHUNK_SIZE * 4;
                decompressed[start..start + local_buf.len()].copy_from_slice(&local_buf);
            });
        Ok(PyBytes::new_bound(
            py,
            decompressed.into_inner().unwrap().as_slice(),
        ))
    };
}

#[pyfunction]
#[pyo3(signature = (pixel_data, width, height, path), name = "save_png")]
pub fn py_save_png(
    pixel_data: Bound<PyArray1<u8>>,
    width: u32,
    height: u32,
    path: PathBuf,
) -> PyResult<()> {
    let file = File::create(path)?;
    let mut encoder = png::Encoder::new(file, width, height);
    encoder.set_depth(png::BitDepth::Eight);
    encoder.set_color(png::ColorType::Rgba);
    encoder.set_compression(png::Compression::Fast);
    let mut writer = encoder
        .write_header()
        .map_err(|e| PyException::new_err(e.to_string()))?;
    writer
        .write_image_data(pixel_data.to_vec()?.as_slice())
        .map_err(|e| PyException::new_err(e.to_string()))?;
    Ok(())
}

#[pyfunction]
#[pyo3(signature = (pixel_data, width, height, path), name = "save_exr")]
pub fn py_save_exr(
    pixel_data: Bound<PyArray1<f32>>,
    width: u32,
    height: u32,
    path: PathBuf,
) -> PyResult<()> {
    use exr::prelude::*;
    let tmp = pixel_data.to_vec()?;
    write_rgba_file(path, width as usize, height as usize, |x, y| {
        let i = y * width as usize * 4 + x * 4;
        (tmp[i + 0], tmp[i + 1], tmp[i + 2], tmp[i + 3])
    })
        .map_err(|e| PyException::new_err(e.to_string()))?;
    Ok(())
}

#[pyfunction]
#[pyo3(signature = (pixel_data, width, height), name = "encode_png")]
pub fn py_encode_png<'py>(
    py: Python<'py>,
    pixel_data: Bound<PyArray1<u8>>,
    width: u32,
    height: u32,
) -> PyResult<Bound<'py, PyBytes>> {
    let mut cursor = Cursor::new(Vec::with_capacity(64));
    let mut encoder = png::Encoder::new(&mut cursor, width, height);
    encoder.set_depth(png::BitDepth::Eight);
    encoder.set_color(png::ColorType::Rgba);
    encoder.set_compression(png::Compression::Fast);
    let mut writer = encoder
        .write_header()
        .map_err(|e| PyException::new_err(e.to_string()))?;
    writer
        .write_image_data(pixel_data.to_vec()?.as_slice())
        .map_err(|e| PyException::new_err(e.to_string()))?;
    writer
        .finish()
        .map_err(|e| PyException::new_err(e.to_string()))?;
    Ok(PyBytes::new_bound(py, cursor.into_inner().as_slice()))
}

#[pyfunction]
#[pyo3(signature = (pixel_data, width, height), name = "encode_exr")]
pub fn py_encode_exr<'py>(
    py: Python<'py>,
    pixel_data: Bound<PyArray1<f32>>,
    width: u32,
    height: u32,
) -> PyResult<Bound<'py, PyBytes>> {
    let mut cursor = Cursor::new(Vec::with_capacity(64));
    let tmp = pixel_data.to_vec()?;
    let channels = SpecificChannels::rgba(|Vec2(x, y)| {
        let i = y * width as usize * 4 + x * 4;
        (tmp[i + 0], tmp[i + 1], tmp[i + 2], tmp[i + 3])
    });
    Image::from_channels((width as usize, height as usize), channels)
        .write()
        .to_buffered(&mut cursor)
        .map_err(|e| PyException::new_err(e.to_string()))?;
    Ok(PyBytes::new_bound(py, cursor.into_inner().as_slice()))
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
    m.add_function(wrap_pyfunction!(py_lz4_decompress_continue, m)?)?;
    m.add_function(wrap_pyfunction!(py_lz4_decompress, m)?)?;
    m.add_function(wrap_pyfunction!(py_save_png, m)?)?;
    m.add_function(wrap_pyfunction!(py_encode_png, m)?)?;
    m.add_function(wrap_pyfunction!(py_save_exr, m)?)?;
    m.add_function(wrap_pyfunction!(py_encode_exr, m)?)?;
    m.add_function(wrap_pyfunction!(py_load_vtf_texture, m)?)?;
    m.add_function(wrap_pyfunction!(py_save_vtf_texture, m)?)?;
    m.add_function(wrap_pyfunction!(py_decode_texture, m)?)?;
    Ok(())
}
