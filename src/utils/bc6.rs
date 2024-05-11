use std::io;
use std::io::Read;

use half::f16;
use thiserror::Error;

use crate::utils::bit_reader::BitReader;

#[derive(Error, Debug)]
pub enum Error {
    #[error("IO error {0}")]
    IOError(#[from] io::Error)
}

struct ModeInfo {
    index_bits: u32,
    subsets: u32,
    transformed_endpoints: bool,
    partition_bits: u32,
    endpoints_bits: u32,
    red_bits: u32,
    green_bits: u32,
    blue_bits: u32,
    bits: &'static [u8],
}

const MODES: [ModeInfo; 14] = [
    // @formatter:off
    ModeInfo { index_bits: 3, subsets: 2, transformed_endpoints: true,  partition_bits: 5, endpoints_bits: 10, red_bits: 5,  green_bits: 5,  blue_bits: 5,  bits: &[116, 132, 180, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49, 50, 51, 52, 164, 112, 113, 114, 115, 64, 65, 66, 67, 68, 176, 160, 161, 162, 163, 80, 81, 82, 83, 84, 177, 128, 129, 130, 131, 96, 97, 98, 99, 100, 178, 144, 145, 146, 147, 148, 179] },
    ModeInfo { index_bits: 3, subsets: 2, transformed_endpoints: true,  partition_bits: 5, endpoints_bits: 7,  red_bits: 6,  green_bits: 6,  blue_bits: 6,  bits: &[117, 164, 165, 0, 1, 2, 3, 4, 5, 6, 176, 177, 132, 16, 17, 18, 19, 20, 21, 22, 133, 178, 116, 32, 33, 34, 35, 36, 37, 38, 179, 181, 180, 48, 49, 50, 51, 52, 53, 112, 113, 114, 115, 64, 65, 66, 67, 68, 69, 160, 161, 162, 163, 80, 81, 82, 83, 84, 85, 128, 129, 130, 131, 96, 97, 98, 99, 100, 101, 144, 145, 146, 147, 148, 149] },
    ModeInfo { index_bits: 3, subsets: 2, transformed_endpoints: true,  partition_bits: 5, endpoints_bits: 11, red_bits: 5,  green_bits: 4,  blue_bits: 4,  bits: &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49, 50, 51, 52, 10, 112, 113, 114, 115, 64, 65, 66, 67, 26, 176, 160, 161, 162, 163, 80, 81, 82, 83, 42, 177, 128, 129, 130, 131, 96, 97, 98, 99, 100, 178, 144, 145, 146, 147, 148, 179] },
    ModeInfo { index_bits: 3, subsets: 2, transformed_endpoints: true,  partition_bits: 5, endpoints_bits: 11, red_bits: 4,  green_bits: 5,  blue_bits: 4,  bits: &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49, 50, 51, 10, 164, 112, 113, 114, 115, 64, 65, 66, 67, 68, 26, 160, 161, 162, 163, 80, 81, 82, 83, 42, 177, 128, 129, 130, 131, 96, 97, 98, 99, 176, 178, 144, 145, 146, 147, 116, 179] },
    ModeInfo { index_bits: 3, subsets: 2, transformed_endpoints: true,  partition_bits: 5, endpoints_bits: 11, red_bits: 4,  green_bits: 4,  blue_bits: 5,  bits: &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49, 50, 51, 10, 132, 112, 113, 114, 115, 64, 65, 66, 67, 26, 176, 160, 161, 162, 163, 80, 81, 82, 83, 84, 42, 128, 129, 130, 131, 96, 97, 98, 99, 177, 178, 144, 145, 146, 147, 180, 179] },
    ModeInfo { index_bits: 3, subsets: 2, transformed_endpoints: true,  partition_bits: 5, endpoints_bits: 9,  red_bits: 5,  green_bits: 5,  blue_bits: 5,  bits: &[0, 1, 2, 3, 4, 5, 6, 7, 8, 132, 16, 17, 18, 19, 20, 21, 22, 23, 24, 116, 32, 33, 34, 35, 36, 37, 38, 39, 40, 180, 48, 49, 50, 51, 52, 164, 112, 113, 114, 115, 64, 65, 66, 67, 68, 176, 160, 161, 162, 163, 80, 81, 82, 83, 84, 177, 128, 129, 130, 131, 96, 97, 98, 99, 100, 178, 144, 145, 146, 147, 148, 179] },
    ModeInfo { index_bits: 3, subsets: 2, transformed_endpoints: true,  partition_bits: 5, endpoints_bits: 8,  red_bits: 6,  green_bits: 5,  blue_bits: 5,  bits: &[0, 1, 2, 3, 4, 5, 6, 7, 164, 132, 16, 17, 18, 19, 20, 21, 22, 23, 178, 116, 32, 33, 34, 35, 36, 37, 38, 39, 179, 180, 48, 49, 50, 51, 52, 53, 112, 113, 114, 115, 64, 65, 66, 67, 68, 176, 160, 161, 162, 163, 80, 81, 82, 83, 84, 177, 128, 129, 130, 131, 96, 97, 98, 99, 100, 101, 144, 145, 146, 147, 148, 149] },
    ModeInfo { index_bits: 3, subsets: 2, transformed_endpoints: true,  partition_bits: 5, endpoints_bits: 8,  red_bits: 5,  green_bits: 6,  blue_bits: 5,  bits: &[0, 1, 2, 3, 4, 5, 6, 7, 176, 132, 16, 17, 18, 19, 20, 21, 22, 23, 117, 116, 32, 33, 34, 35, 36, 37, 38, 39, 165, 180, 48, 49, 50, 51, 52, 164, 112, 113, 114, 115, 64, 65, 66, 67, 68, 69, 160, 161, 162, 163, 80, 81, 82, 83, 84, 177, 128, 129, 130, 131, 96, 97, 98, 99, 100, 178, 144, 145, 146, 147, 148, 179] },
    ModeInfo { index_bits: 3, subsets: 2, transformed_endpoints: true,  partition_bits: 5, endpoints_bits: 8,  red_bits: 5,  green_bits: 5,  blue_bits: 6,  bits: &[0, 1, 2, 3, 4, 5, 6, 7, 177, 132, 16, 17, 18, 19, 20, 21, 22, 23, 133, 116, 32, 33, 34, 35, 36, 37, 38, 39, 181, 180, 48, 49, 50, 51, 52, 164, 112, 113, 114, 115, 64, 65, 66, 67, 68, 176, 160, 161, 162, 163, 80, 81, 82, 83, 84, 85, 128, 129, 130, 131, 96, 97, 98, 99, 100, 178, 144, 145, 146, 147, 148, 179] },
    ModeInfo { index_bits: 3, subsets: 2, transformed_endpoints: false, partition_bits: 5, endpoints_bits: 6,  red_bits: 6,  green_bits: 6,  blue_bits: 6,  bits: &[0, 1, 2, 3, 4, 5, 164, 176, 177, 132, 16, 17, 18, 19, 20, 21, 117, 133, 178, 116, 32, 33, 34, 35, 36, 37, 165, 179, 181, 180, 48, 49, 50, 51, 52, 53, 112, 113, 114, 115, 64, 65, 66, 67, 68, 69, 160, 161, 162, 163, 80, 81, 82, 83, 84, 85, 128, 129, 130, 131, 96, 97, 98, 99, 100, 101, 144, 145, 146, 147, 148, 149] },
    ModeInfo { index_bits: 4, subsets: 1, transformed_endpoints: false, partition_bits: 0, endpoints_bits: 10, red_bits: 10, green_bits: 10, blue_bits: 10, bits: &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89] },
    ModeInfo { index_bits: 4, subsets: 1, transformed_endpoints: true,  partition_bits: 0, endpoints_bits: 11, red_bits: 9,  green_bits: 9,  blue_bits: 9,  bits: &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49, 50, 51, 52, 53, 54, 55, 56, 10, 64, 65, 66, 67, 68, 69, 70, 71, 72, 26, 80, 81, 82, 83, 84, 85, 86, 87, 88, 42] },
    ModeInfo { index_bits: 4, subsets: 1, transformed_endpoints: true,  partition_bits: 0, endpoints_bits: 12, red_bits: 8,  green_bits: 8,  blue_bits: 8,  bits: &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49, 50, 51, 52, 53, 54, 55, 11, 10, 64, 65, 66, 67, 68, 69, 70, 71, 27, 26, 80, 81, 82, 83, 84, 85, 86, 87, 43, 42] },
    ModeInfo { index_bits: 4, subsets: 1, transformed_endpoints: true,  partition_bits: 0, endpoints_bits: 16, red_bits: 4,  green_bits: 4,  blue_bits: 4,  bits: &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49, 50, 51, 15, 14, 13, 12, 11, 10, 64, 65, 66, 67, 31, 30, 29, 28, 27, 26, 80, 81, 82, 83, 47, 46, 45, 44, 43, 42] }
    // @formatter:on
];

const WEIGHTS_2: [u8; 4] = [0, 21, 43, 64];
const WEIGHTS_3: [u8; 8] = [0, 9, 18, 27, 37, 46, 55, 64];
const WEIGHTS_4: [u8; 16] = [0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64];
const ANCHOR_INDICES_0: [u8; 64] = [
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2, 8, 2, 2, 8,
    8, 15, 2, 8, 2, 2, 8, 8, 2, 2, 15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2,
    2, 15, 15, 6, 6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15
];

const SUBSET_INDICES_2: [u32; 64] = [
    0xcccc, 0x8888, 0xeeee, 0xecc8, 0xc880, 0xfeec, 0xfec8, 0xec80, 0xc800, 0xffec,
    0xfe80, 0xe800, 0xffe8, 0xff00, 0xfff0, 0xf000, 0xf710, 0x008e, 0x7100, 0x08ce,
    0x008c, 0x7310, 0x3100, 0x8cce, 0x088c, 0x3110, 0x6666, 0x366c, 0x17e8, 0x0ff0,
    0x718e, 0x399c, 0xaaaa, 0xf0f0, 0x5a5a, 0x33cc, 0x3c3c, 0x55aa, 0x9696, 0xa55a,
    0x73ce, 0x13c8, 0x324c, 0x3bdc, 0x6996, 0xc33c, 0x9966, 0x0660, 0x0272, 0x04e4,
    0x4e40, 0x2720, 0xc936, 0x936c, 0x39c6, 0x639c, 0x9336, 0x9cc6, 0x817e, 0xe718,
    0xccf0, 0x0fcc, 0x7744, 0xee22
];


const SUBSET_INDICES_3: [u32; 64] = [
    0xaa685050, 0x6a5a5040, 0x5a5a4200, 0x5450a0a8, 0xa5a50000, 0xa0a05050, 0x5555a0a0,
    0x5a5a5050, 0xaa550000, 0xaa555500, 0xaaaa5500, 0x90909090, 0x94949494, 0xa4a4a4a4,
    0xa9a59450, 0x2a0a4250, 0xa5945040, 0x0a425054, 0xa5a5a500, 0x55a0a0a0, 0xa8a85454,
    0x6a6a4040, 0xa4a45000, 0x1a1a0500, 0x0050a4a4, 0xaaa59090, 0x14696914, 0x69691400,
    0xa08585a0, 0xaa821414, 0x50a4a450, 0x6a5a0200, 0xa9a58000, 0x5090a0a8, 0xa8a09050,
    0x24242424, 0x00aa5500, 0x24924924, 0x24499224, 0x50a50a50, 0x500aa550, 0xaaaa4444,
    0x66660000, 0xa5a0a5a0, 0x50a050a0, 0x69286928, 0x44aaaa44, 0x66666600, 0xaa444444,
    0x54a854a8, 0x95809580, 0x96969600, 0xa85454a8, 0x80959580, 0xaa141414, 0x96960000,
    0xaaaa1414, 0xa05050a0, 0xa0a5a5a0, 0x96000000, 0x40804080, 0xa9a8a9a8, 0xaaaaaa44,
    0x2a4a5254
];

pub fn decode_bc6(buffer: &mut impl Read, image: &mut [f32], width: usize, height: usize, signed:bool) -> Result<(), Error> {
    for y in (0..height).step_by(4) {
        for x in (0..width).step_by(4) {
            read_block(buffer, image, x, y, width, signed)?;
        }
    }
    Ok(())
}

pub fn read_block(buffer: &mut impl Read, image: &mut [f32], x: usize, y: usize, width: usize, singed: bool) -> Result<(), Error> {
    let mut block = [0u8; 16];
    buffer.read_exact(&mut block)?;
    let mut bits = BitReader::from_slice(&block);

    let info = {
        match bits.get(2)? {
            0 => { &MODES[0] }
            1 => { &MODES[1] }
            2 => { &MODES[(bits.get(3)? + 2) as usize] }
            _ => { &MODES[(bits.get(3)? + 10) as usize] }
        }
    };

    let mut endpoints = [0u16; 12];
    for mask in info.bits {
        let ei = (*mask >> 4) & 15;
        let bi = *mask & 15;
        endpoints[ei as usize] |= bits.get(1)? << bi;
    }

    let partition = bits.get(info.partition_bits)?;

    if singed {
        endpoints[0] = sign_extend(endpoints[0], info.endpoints_bits);
        endpoints[1] = sign_extend(endpoints[1], info.endpoints_bits);
        endpoints[2] = sign_extend(endpoints[2], info.endpoints_bits);
    }

    if singed || info.transformed_endpoints {
        for i in (3..12).step_by(3) {
            endpoints[i + 0] = sign_extend(endpoints[i + 0], info.red_bits);
            endpoints[i + 1] = sign_extend(endpoints[i + 1], info.green_bits);
            endpoints[i + 2] = sign_extend(endpoints[i + 2], info.blue_bits);
        }
    }

    if info.transformed_endpoints {
        for i in (3..12).step_by(3) {
            let mask = ((1u32 << info.endpoints_bits) - 1) as u16;
            endpoints[i + 0] = endpoints[i + 0].wrapping_add(endpoints[0]) & mask;
            endpoints[i + 1] = endpoints[i + 1].wrapping_add(endpoints[1]) & mask;
            endpoints[i + 2] = endpoints[i + 2].wrapping_add(endpoints[2]) & mask;
        }
    }

    for endpoint in &mut endpoints {
        *endpoint = unquantize(*endpoint, info.endpoints_bits, singed);
    }

    let weights: &[u8] = match info.index_bits {
        2 => &WEIGHTS_2,
        3 => &WEIGHTS_3,
        4 => &WEIGHTS_4,
        _ => unreachable!()
    };

    for i in 0..16 {
        let ib2 = if i == 0 || info.subsets == 2 && ANCHOR_INDICES_0[partition as usize] == i as u8 {
            info.index_bits - 1
        } else {
            info.index_bits
        };

        let subset = get_subset(info.subsets, partition as u32, i) * 6;
        let index = bits.get(ib2)? as usize;
        let gx = x + i % 4;
        let gy = y + i / 4;
        image[gy * width * 3 + gx * 3 + 0] = lerp(endpoints[subset + 0], endpoints[subset + 3], weights[index], singed);
        image[gy * width * 3 + gx * 3 + 1] = lerp(endpoints[subset + 1], endpoints[subset + 4], weights[index], singed);
        image[gy * width * 3 + gx * 3 + 2] = lerp(endpoints[subset + 2], endpoints[subset + 5], weights[index], singed);
    }

    Ok(())
}

fn unquantize(mut x: u16, ebp: u32, signed: bool) -> u16 {
    if signed {
        if ebp >= 16 {
            return x;
        }
        let mut sign = false;
        if (x as i16) < 0 {
            sign = true;
            x = (-(x as i16)) as u16;
        }
        let unq: u16 = {
            if x == 0 {
                0
            } else if x >= (1 << (ebp - 1)) - 1 {
                0x7FFF
            } else {
                ((x << 15) + 0x400) >> (ebp - 1)
            }
        };
        if sign { (-(unq as i16)) as u16 } else { unq }
    } else if ebp >= 15 {
        x
    } else if x == 0 {
        0
    } else if x == ((1 << ebp) - 1) {
        0xFFFF
    } else {
        ((((x as u32) << 15) as i32 + 0x4000) >> (ebp - 1)) as u16
    }
}

fn sign_extend(value: u16, bits: u32) -> u16 {
    let shift = 32 - bits;
    (value as i32).wrapping_shl(shift).wrapping_shr(shift) as u16
}

fn get_subset(ns: u32, partition: u32, n: usize) -> usize {
    match ns {
        2 => (SUBSET_INDICES_2[partition as usize] >> n & 1) as usize,
        3 => (SUBSET_INDICES_3[partition as usize] >> (n * 2) & 3) as usize,
        _ => 0,
    }
}

fn lerp(e0: u16, e1: u16, weight: u8, signed: bool) -> f32 {
    let interpolated = ((64 - weight as u32) * e0 as u32 + (weight as u32 * e1 as u32) + 32) >> 6;
    finalize(interpolated as u16, signed)
}

fn finalize(value: u16, signed: bool) -> f32 {
    if signed {
        if (value as i16) < 0 {
            f16::from_bits(0x8000 | (-(value as i32) * 31 / 32) as u16).to_f32()
        } else {
            f16::from_bits((value as u32 * 31 / 32) as u16).to_f32()
        }
    } else {
        f16::from_bits((value as u32 * 31 / 64) as u16).to_f32()
    }
}