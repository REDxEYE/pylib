mod binary;
mod serializer;
pub mod errors;
pub mod header;
mod string_dict;
mod encoding;
pub mod element;


use std::fs::File;
use std::io::{BufRead, BufReader, Seek};
use std::path::Path;
use regex::Regex;
use errors::DmxError;
use header::DmxHeader;
use binary::DmxBinaryV5;
use element::RcDmElement;
use encoding::DmxEncoding;
use errors::DmxError::UnsupportedDmxVersion;
use serializer::DmxDeserialize;

#[derive(Debug)]
pub struct Dmx {
    pub elements: Vec<RcDmElement>,
}


pub fn load_dmx<P: AsRef<Path>>(filepath: P) -> Result<Dmx, DmxError> {
    let mut file = File::open(filepath).map(BufReader::new)?;

    read_dmx(&mut file)
}

pub fn read_dmx<R: BufRead + Seek>(reader: &mut R) -> Result<Dmx, DmxError> {
    let mut header = Vec::new();
    reader.read_until(0u8, &mut header)?;
    let header_str = String::from_utf8(header).map_err(|_| { DmxError::FailedToReadHeader })?;
    let header_format = Regex::new(r"<!-- dmx encoding (\w+) (\d+) format (\w+) (\d+) -->").unwrap();

    let dmx_header = header_format.captures(&header_str).map(|caps| {
        let enc_str = caps.get(1).unwrap().as_str();
        let enc = match enc_str {
            "binary" => { Ok(DmxEncoding::Binary) }
            "keyvalues" => { Ok(DmxEncoding::KeyValues) }
            "keyvalues2" => { Ok(DmxEncoding::KeyValues2) }
            "keyvalues2_flat" => { Ok(DmxEncoding::KeyValues2Flat) }
            _ => { Err(DmxError::UnsupportedDmxEncoding { enc: enc_str.parse().unwrap() }) }
        }?;

        Ok::<DmxHeader, DmxError>(DmxHeader {
            encoding: enc,
            encoding_ver: caps.get(2).unwrap().as_str().parse().unwrap(),
            format: caps.get(3).unwrap().as_str().parse().unwrap(),
            format_ver: caps.get(4).unwrap().as_str().parse().unwrap(),
        })
    }).ok_or(DmxError::InvalidHeader)??;


    let res = match (dmx_header.encoding, dmx_header.encoding_ver) {
        // (DmxEncoding::Binary, 1) => { DmxBinaryV5::deserialize(reader) }
        // (DmxEncoding::Binary, 2) => { DmxBinaryV5::deserialize(reader) }
        // (DmxEncoding::Binary, 3) => { DmxBinaryV5::deserialize(reader) }
        // (DmxEncoding::Binary, 4) => { DmxBinaryV5::deserialize(reader) }
        (DmxEncoding::Binary, 5) => { DmxBinaryV5::deserialize(reader) }
        // (DmxEncoding::Binary, 6) => { DmxBinaryV5::deserialize(reader) }
        // (DmxEncoding::Binary, 9) => { DmxBinaryV5::deserialize(reader) }
        // DmxEncoding::KeyValues2Flat => { Ok(Dmx { header: dmx_header, elements: Vec::new() }) }
        // DmxEncoding::KeyValues2 => { Ok(Dmx { header: dmx_header, elements: Vec::new() }) }
        // DmxEncoding::KeyValues => { Ok(Dmx { header: dmx_header, elements: Vec::new() }) }
        _ => { Err(UnsupportedDmxVersion { enc: dmx_header.encoding, ver: dmx_header.encoding_ver }) }
    }?;


    Ok(res)
}
