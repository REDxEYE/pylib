use std::collections::HashMap;
use std::fs::File;
use std::io;
use std::io::{ErrorKind, Read, Seek, SeekFrom};
use std::io::ErrorKind::InvalidData;
use std::path::{Path, PathBuf};

use crate::errors::SourceError;
use crate::utils::reader_utils::{FromReader, ReadExt};

#[derive(Debug, Default)]
struct VpkHeader {
    version: (u16, u16),
    tree_size: u32,
    file_data_section_size: u32,
    archive_md5_section_size: u32,
    other_md5_section_size: u32,
    signature_section_size: u32,
}

impl<R: Read + Seek> FromReader<R> for VpkHeader {
    fn from_reader(reader: &mut R) -> io::Result<Self> {
        let magic = reader.read_u32le()?;
        if magic != 0x55AA1234 {
            return Err(io::Error::new(ErrorKind::InvalidData, SourceError::InvalidHeader("Vpk".into(), magic.to_le_bytes(), 0x55AA1234u32.to_le_bytes())));
        }
        let version = (reader.read_u16le()?, reader.read_u16le()?);
        let tree_size = reader.read_u32le()?;
        if version.0 == 1 {
            Ok(VpkHeader {
                version,
                tree_size,
                file_data_section_size: 0,
                archive_md5_section_size: 0,
                other_md5_section_size: 0,
                signature_section_size: 0,
            })
        } else if version.0 == 2 && version.1 == 0 {
            Ok(VpkHeader {
                version,
                tree_size,
                file_data_section_size: reader.read_u32le()?,
                archive_md5_section_size: reader.read_u32le()?,
                other_md5_section_size: reader.read_u32le()?,
                signature_section_size: reader.read_u32le()?,
            })
        } else if version.0 == 2 && version.1 == 3 {
            Ok(VpkHeader {
                version,
                tree_size,
                file_data_section_size: reader.read_u32le()?,
                archive_md5_section_size: 0,
                other_md5_section_size: 0,
                signature_section_size: 0,
            })
        } else {
            Err(io::Error::new(InvalidData, SourceError::UnsupportedVersion("Vpk".into(), version.0 as u32)))
        }
    }
}

#[derive(Debug, Default)]
struct VpkEntry {
    file_name: String,
    crc32: u32,
    preload_data_size: u16,
    archive_id: u16,
    offset: u32,
    size: u32,
    preload_data: Vec<u8>,

}

impl<R: Read + Seek> FromReader<R> for VpkEntry {
    fn from_reader(reader: &mut R) -> io::Result<Self> {
        let mut entry = VpkEntry {
            file_name: "".into(),
            crc32: reader.read_u32le()?,
            preload_data_size: reader.read_u16le()?,
            archive_id: reader.read_u16le()?,
            offset: reader.read_u32le()?,
            size: reader.read_u32le()?,
            preload_data: vec![],
        };

        let term = reader.read_u16le()?;
        if term != 0xFFFF {
            return Err(io::Error::new(InvalidData, SourceError::InvalidHeader("Vpk terminator".into(), (term as u32).to_le_bytes(), 0x0000FFFFu32.to_le_bytes())));
        }

        let mut preload_data = vec![0u8; entry.preload_data_size as usize];
        reader.read_exact(&mut preload_data)?;
        entry.preload_data = preload_data;
        Ok(entry)
    }
}

#[derive(Debug)]
pub struct Vpk {
    file_path: PathBuf,
    file_buffer: File,
    header: VpkHeader,
    tree_offset: u64,
    entries: HashMap<String, VpkEntry>,
}


impl Vpk {
    pub fn from_path(path: &Path) -> io::Result<Self> {
        let mut file = File::open(path)?;
        let header = VpkHeader::from_reader(&mut file)?;
        let mut entries = HashMap::new();
        let tree_offset = file.stream_position()?;
        loop {
            let type_name = file.read_ztstring()?;
            if type_name.is_empty() {
                break;
            }
            loop {
                let directory_name = file.read_ztstring()?;
                if directory_name.is_empty() {
                    break;
                }
                loop {
                    let file_name = file.read_ztstring()?;
                    if file_name.is_empty() {
                        break;
                    }

                    let full_path = format!("{directory_name}/{file_name}.{type_name}").to_lowercase();
                    let mut entry = VpkEntry::from_reader(&mut file)?;
                    entry.file_name = full_path.clone();
                    entries.insert(full_path, entry);
                }
            }
        }


        Ok(Vpk {
            file_path: path.to_path_buf(),
            file_buffer: file,
            header,
            tree_offset,
            entries,
        })
    }

    pub fn find_file(&mut self, name: String) -> Option<Vec<u8>> {
        match self.entries.get(&name) {
            None => { None }
            Some(entry) => {
                let mut res = Vec::with_capacity(entry.preload_data_size as usize);
                if entry.preload_data_size > 0 {
                    res.extend(&entry.preload_data);
                }

                if entry.archive_id == 0x7FFF {
                    self.file_buffer.seek(SeekFrom::Start((self.header.tree_size + entry.offset) as u64)).ok()?;
                    res.resize(res.len() + entry.size as usize, 0);
                    self.file_buffer.read_exact(&mut res[entry.preload_data_size as usize..]).ok()?;
                    Some(res)
                } else {
                    let target_archive_path = {
                        let stem_tmp = self.file_path.file_stem()?;
                        let stem = stem_tmp.to_str()?[0..stem_tmp.len() - 3].to_owned();
                        self.file_path.parent()?.join(format!("{0}{1:03}.vpk", stem, entry.archive_id))
                    };
                    let mut file = File::open(target_archive_path).ok()?;
                    file.seek(SeekFrom::Start(entry.offset as u64)).ok()?;
                    res.resize(res.len() + entry.size as usize, 0);
                    file.read_exact(&mut res[entry.preload_data_size as usize..]).ok()?;
                    Some(res)
                }
            }
        }
    }
}