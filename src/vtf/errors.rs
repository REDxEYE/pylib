use std::io;
use thiserror::Error;
use crate::vtf::structs::VtfImageFormat;

#[derive(Error, Debug)]
pub enum VTFError {
    #[error("Failed to read texture")]
    FailedToReadTexture,

    #[error("Invalid VTF header")]
    InvalidHeader,

    #[error("Unsupported image format {0:?}")]
    UnsupportedFormat(VtfImageFormat),

    #[error("Failed to read mip level {0}")]
    MipLevelError(u32),

    #[error("Missing high-resolution texture data")]
    MissingHighResTexture,

    #[error("Invalid resource entry for texture")]
    InvalidResourceEntry,

    #[error("IO error: {source}")]
    UnknownIOError {
        #[from]
        source: io::Error,
    },
}