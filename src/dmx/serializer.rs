use std::io::{Write, Seek, BufRead};
use crate::dmx::Dmx;
use crate::dmx::errors::DmxError;

pub trait DmxSerialize {
    fn serialize<W: Write + Seek>(dmx: &Dmx, writer: &mut W) -> Result<(), DmxError>;
}

pub trait DmxDeserialize {
    fn deserialize<R: BufRead + Seek>(reader: &mut R) -> Result<Dmx, DmxError>;
}