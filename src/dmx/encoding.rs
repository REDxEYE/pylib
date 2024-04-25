use std::fmt;

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum DmxEncoding{
    Binary,
    KeyValues,
    KeyValues2,
    KeyValues2Flat,
}

impl fmt::Display for DmxEncoding {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DmxEncoding::Binary => write!(f, "Binary"),
            DmxEncoding::KeyValues => write!(f, "KeyValues"),
            DmxEncoding::KeyValues2 => write!(f, "KeyValues2"),
            DmxEncoding::KeyValues2Flat => write!(f, "KeyValues2Flat")
        }
    }
}