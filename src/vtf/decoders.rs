use bcndecode::{decode, BcnDecoderFormat, BcnEncoding};
use rayon::prelude::ParallelSlice;
use rayon::iter::{IntoParallelIterator, ParallelIterator};
use crate::vtf::structs::VtfImageFormat;

fn reorder_rgba(input: &[u8], order: [usize; 4]) -> Vec<f32> {
    input
        .par_chunks_exact(4)
        .flat_map(|chunk| order.map(|i| chunk[i] as f32 / 255.0))
        .collect()
}

fn extend_and_reorder_rgb(input: &[u8], order: [usize; 3]) -> Vec<f32> {
    input
        .par_chunks_exact(3)
        .flat_map(|chunk| {
            [
                chunk[order[0]] as f32 / 255.0,
                chunk[order[1]] as f32 / 255.0,
                chunk[order[2]] as f32 / 255.0,
                1f32,
            ]
        })
        .collect()
}

fn extend_rg(input: &[u8]) -> Vec<f32> {
    input
        .par_chunks_exact(2)
        .flat_map(|chunk| [chunk[0] as f32 / 255.0, chunk[1] as f32 / 255.0, 0f32, 1f32])
        .collect()
}

fn extend_r(input: &[u8]) -> Vec<f32> {
    input
        .par_chunks_exact(1)
        .flat_map(|chunk| [chunk[0] as f32 / 255.0, 0f32, 0f32, 1f32])
        .collect()
}

fn decode_16bit<F>(input: &[u8], decode_fn: F) -> Vec<f32>
where
    F: Fn(u16) -> [f32; 4] + Sync + Send,
{
    input
        .par_chunks_exact(2)
        .flat_map(|chunk| decode_fn(u16::from_le_bytes(chunk.try_into().unwrap())))
        .collect()
}

fn decode_rgb565(input: &[u8]) -> Vec<f32> {
    decode_16bit(input, move |px| {
        let (r, g, b) = ((px >> 11) & 0x1F, (px >> 5) & 0x3F, px & 0x1F);
        [r as f32 / 31.0, g as f32 / 63.0, b as f32 / 31.0, 1.0]
    })
}

fn decode_bgra5551(input: &[u8]) -> Vec<f32> {
    decode_16bit(input, |px| {
        let (b, g, r, a) = (
            (px >> 11) & 0x1F,
            (px >> 6) & 0x1F,
            (px >> 1) & 0x1F,
            px & 0x01,
        );
        [r as f32 / 31.0, g as f32 / 31.0, b as f32 / 31.0, a as f32]
    })
}

fn decode_bgra4444(input: &[u8]) -> Vec<f32> {
    decode_16bit(input, |px| {
        let (b, g, r, a) = (
            (px >> 12) & 0x0F,
            (px >> 8) & 0x0F,
            (px >> 4) & 0x0F,
            px & 0x0F,
        );
        [
            r as f32 / 15.0,
            g as f32 / 15.0,
            b as f32 / 15.0,
            a as f32 / 15.0,
        ]
    })
}

fn decode_bgr565(input: &[u8]) -> Vec<f32> {
    decode_16bit(input, |px| {
        let (r, g, b) = ((px >> 11) & 0x1F, (px >> 5) & 0x3F, px & 0x1F);
        [b as f32 / 31.0, g as f32 / 63.0, r as f32 / 31.0, 1.0]
    })
}

fn decode_bcn(input: &[u8], dimensions: (u32, u32), encoding: BcnEncoding) -> Vec<f32> {
    decode(
        input,
        dimensions.0 as usize,
        dimensions.1 as usize,
        encoding,
        BcnDecoderFormat::RGBA,
    )
    .unwrap()
    .into_par_iter()
    .map(|v| v as f32 / 255.0)
    .collect()
}

pub fn decode_texture(format: VtfImageFormat, data: &[u8], dimensions: (u32, u32)) -> Option<Vec<f32>> {
    use bcndecode::BcnEncoding;
    use rayon::iter::IntoParallelRefIterator;
    use crate::vtf::structs::VtfImageFormat::*;
    Some(match format {
        NONE | RGBA16161616F | RGBA16161616 => return None,
        RGBA8888 | UVLX8888 => data.par_iter().map(|&v| v as f32 / 255.0).collect(),
        ABGR8888 => reorder_rgba(data, [3, 2, 1, 0]),
        RGB888 | RGB888Bluescreen => extend_and_reorder_rgb(data, [0, 1, 2]),
        BGR888 | BGR888Bluescreen => extend_and_reorder_rgb(data, [2, 1, 0]),
        RGB565 => decode_rgb565(data),
        I8 | P8 | A8 => extend_r(data),
        IA88 | UV88 => extend_rg(data),
        ARGB8888 => reorder_rgba(data, [1, 2, 3, 0]),
        BGRA8888 | BGRX8888 => reorder_rgba(data, [2, 1, 0, 3]),
        BGR565 => decode_rgb565(data),
        BGRX5551 | BGRA5551 => decode_bgra5551(data),
        BGRA4444 => decode_bgra4444(data),
        DXT1 | DXT1Onebitalpha => decode_bcn(data, dimensions, BcnEncoding::Bc1),
        DXT3 => decode_bcn(data, dimensions, BcnEncoding::Bc2),
        DXT5 => decode_bcn(data, dimensions, BcnEncoding::Bc3),
        UVWQ8888 => reorder_rgba(data, [2, 1, 0, 3]),
    })
}