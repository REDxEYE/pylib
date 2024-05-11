use std::io::{BufRead, Seek};
use std::rc::Rc;

use byteorder::{LE, ReadBytesExt};

use crate::dmx::errors::DmxError;
use crate::utils::reader_utils::BufReadExt;


#[derive(Debug, Clone, Default)]
pub struct StringDictionaryV1 {}

#[derive(Debug, Clone, Default)]
pub struct StringDictionaryV2 {
    strings: Vec<Rc<str>>,
}

#[derive(Debug, Clone, Default)]
pub struct StringDictionaryV4 {
    strings: Vec<Rc<str>>,
}

#[derive(Debug, Clone, Default)]
pub struct StringDictionaryV5 {
    strings: Vec<Rc<str>>,
}

pub trait StringDictionary<R: BufRead + Seek> {
    fn read_string(&self, reader: &mut R) -> Result<Rc<str>, DmxError>;
}

impl StringDictionaryV1 {
    pub fn from_file<R: BufRead + Seek>(_: &mut R) -> Result<StringDictionaryV1, DmxError> {
        Ok(StringDictionaryV1 {})
    }
}

impl<R: BufRead + Seek> StringDictionary<R> for StringDictionaryV1 {
    fn read_string(&self, reader: &mut R) -> Result<Rc<str>, DmxError> {
        Ok(Rc::from(reader.read_ztstring_buf()?))
    }
}

impl StringDictionaryV2 {
    pub fn from_file<R: BufRead + Seek>(reader: &mut R) -> Result<StringDictionaryV2, DmxError> {
        let string_count = reader.read_u16::<LE>()? as usize;
        let mut strings = Vec::with_capacity(string_count);
        for _ in 0..string_count {
            strings.push(Rc::from(reader.read_ztstring_buf()?))
        }
        Ok(StringDictionaryV2 { strings })
    }
}

impl<R: BufRead + Seek> StringDictionary<R> for StringDictionaryV2 {
    fn read_string(&self, reader: &mut R) -> Result<Rc<str>, DmxError> {
        let string_id = reader.read_u16::<LE>()? as usize;
        if string_id >= self.strings.len() {
            return Err(DmxError::StringIndexOutOfRange(string_id, self.strings.len()));
        }
        Ok(Rc::clone(&self.strings[string_id]))
    }
}

impl StringDictionaryV4 {
    pub fn from_file<R: BufRead + Seek>(reader: &mut R) -> Result<StringDictionaryV4, DmxError> {
        let string_count = reader.read_u32::<LE>()? as usize;
        let mut strings = Vec::with_capacity(string_count);
        for _ in 0..string_count {
            strings.push(Rc::from(reader.read_ztstring_buf()?))
        }
        Ok(StringDictionaryV4 { strings })
    }
}

impl<R: BufRead + Seek> StringDictionary<R> for StringDictionaryV4 {
    fn read_string(&self, reader: &mut R) -> Result<Rc<str>, DmxError> {
        let string_id = reader.read_u16::<LE>()? as usize;
        if string_id >= self.strings.len() {
            return Err(DmxError::StringIndexOutOfRange(string_id, self.strings.len()));
        }
        Ok(Rc::clone(&self.strings[string_id]))
    }
}

impl StringDictionaryV5 {
    pub fn from_file<R: BufRead + Seek>(reader: &mut R) -> Result<StringDictionaryV5, DmxError> {
        let string_count = reader.read_u32::<LE>()? as usize;
        let mut strings = Vec::with_capacity(string_count);
        for _ in 0..string_count {
            strings.push(Rc::from(reader.read_ztstring_buf()?))
        }
        Ok(StringDictionaryV5 { strings })
    }
}

impl<R: BufRead + Seek> StringDictionary<R> for StringDictionaryV5 {
    fn read_string(&self, reader: &mut R) -> Result<Rc<str>, DmxError> {
        let string_id = reader.read_u32::<LE>()? as usize;
        Ok(Rc::clone(&self.strings[string_id]))
    }
}




