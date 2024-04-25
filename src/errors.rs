use std::io;
use std::io::{Error, ErrorKind};

use thiserror::Error;

#[derive(Error, Debug)]
pub enum SourceError {
    #[error("Invalid header of {0}, expected {1:?}, got {2:?}")]
    InvalidHeader(String, [u8; 4], [u8; 4]),

    #[error("Unsupported version of {0}, expected {1}, got {2}")]
    UnsupportedVersionExact(String, u32, u32),

    #[error("Unsupported version of {0} {1}")]
    UnsupportedVersion(String, u32),

    #[error("IO error: {source}")]
    IOError {
        #[from]
        source: io::Error,
    },
}


impl From<SourceError> for io::Error {
    fn from(value: SourceError) -> Self {
        Error::new(ErrorKind::Other, value)
    }
}

