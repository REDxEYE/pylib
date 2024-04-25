use crate::dmx::encoding::DmxEncoding;

#[derive(Debug, Clone)]
pub struct DmxHeader {
    pub encoding: DmxEncoding,
    pub encoding_ver: u32,
    pub format: String,
    pub format_ver: u32,
}
