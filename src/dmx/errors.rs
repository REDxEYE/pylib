use std::io;
use std::rc::Rc;
use std::string::FromUtf8Error;

use thiserror::Error;

use crate::dmx::encoding::DmxEncoding;

#[derive(Error, Debug)]
pub enum DmxError {
    #[error("Failed to read header")]
    FailedToReadHeader,

    #[error("Invalid header")]
    InvalidHeader,

    #[error("Unsupported {} DMX version {}", .enc, .ver)]
    UnsupportedDmxVersion { enc: DmxEncoding, ver: u32 },

    #[error("Unsupported DMX encoding {}", .enc)]
    UnsupportedDmxEncoding { enc: String },

    #[error("Unknown IO error {source}")]
    UnknownIOError {
        #[from]
        source: io::Error,
    },
    #[error("Failed to parse string {source}")]
    StringParseError {
        #[from]
        source: FromUtf8Error,
    },
    #[error("Unknown binread error {source}")]
    UnknownBinReadError {
        #[from]
        source: binread::Error,
    },

    #[error("String index is out of range {0}:{1}")]
    StringIndexOutOfRange(usize, usize),

    #[error("Invalid attribute type({0}) for prop {1}")]
    InvalidAttributeType(u8, Rc<str>),
}

// impl From<io::Error> for DmxError {
//     fn from(value: io::Error) -> Self {
//         DmxError::UnknownIOError { source: value }
//     }
// }
