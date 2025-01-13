use std::collections::HashMap;
use std::fmt::Debug;
use std::fs::File;
use std::io;
use std::io::ErrorKind::InvalidData;
use std::io::{BufRead, BufReader, Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

use byteorder::ReadBytesExt;
use fnmatch_regex2::glob_to_regex;

use crate::errors::SourceError;
use crate::utils::reader_utils::{BufReadExt, FromReader, ReadExt, ReadSeekExt};

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
        let file_size = reader.seek(SeekFrom::End(-1))? + 1;
        let vtmb_vpk_version = reader.read_u8()?;
        if vtmb_vpk_version == 0 {
            reader.seek(SeekFrom::End(-9))?;
            let _entry_count = reader.read_u32le()?;
            let dir_offset = reader.read_u32le()?;
            if u64::from(dir_offset) < file_size {
                return Ok(VpkHeader {
                    version: (vtmb_vpk_version.into(), 0),
                    tree_size: 0,
                    file_data_section_size: 0,
                    archive_md5_section_size: 0,
                    other_md5_section_size: 0,
                    signature_section_size: 0,
                });
            }
        }
        reader.seek(SeekFrom::Start(0))?;
        let magic = reader.read_u32le()?;
        if magic != 0x55AA1234 {
            return Err(io::Error::new(
                InvalidData,
                SourceError::InvalidHeader(
                    "Vpk".into(),
                    magic.to_le_bytes(),
                    0x55AA1234u32.to_le_bytes(),
                ),
            ));
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
            Err(io::Error::new(
                InvalidData,
                SourceError::UnsupportedVersion("Vpk".into(), version.0 as u32),
            ))
        }
    }
}

#[derive(Debug, Default)]
struct VtmbVpkEntry {
    file_path: String,
    offset: u32,
    size: u32,
}

impl VtmbVpkEntry {
    fn from_reader<R: BufRead + Seek>(reader: &mut R) -> io::Result<Self> {
        let path_len = reader.read_u32le()?;
        let file_path = reader.read_fixed_string(path_len.try_into().unwrap())?;
        let offset = reader.read_u32le()?;
        let size = reader.read_u32le()?;
        Ok(VtmbVpkEntry { file_path, offset, size })
    }
}

#[derive(Debug, Default)]
struct SourceVpkEntry {
    file_name: String,
    crc32: u32,
    preload_data_size: u16,
    archive_id: u16,
    offset: u32,
    size: u32,
    preload_data: Vec<u8>,
}

impl SourceVpkEntry {
    fn from_reader<R: BufRead>(reader: &mut R) -> io::Result<Self> {
        let mut entry = SourceVpkEntry {
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
            return Err(io::Error::new(
                InvalidData,
                SourceError::InvalidHeader(
                    "Vpk terminator".into(),
                    (term as u32).to_le_bytes(),
                    0x0000FFFFu32.to_le_bytes(),
                ),
            ));
        }

        let mut preload_data = vec![0u8; entry.preload_data_size as usize];
        reader.read_exact(&mut preload_data)?;
        entry.preload_data = preload_data;
        Ok(entry)
    }
}

trait SeekRead: Read + Seek {}

pub trait VpkReader: Debug + Send {
    fn find_file(&mut self, name: &str) -> Option<Vec<u8>>;
    fn contains(&self, name: &str) -> bool;
    fn filter(&mut self, pattern: &str) -> Vec<(String, Vec<u8>)>;
}

#[derive(Debug)]
pub struct VtmbVpk {
    file_path: PathBuf,
    header: VpkHeader,
    entry_list: Vec<VtmbVpkEntry>,
    entries: HashMap<String, usize>,
}

impl VtmbVpk {
    fn from_reader<R: BufRead + Seek>(reader: &mut R, path: &Path, header: VpkHeader) -> io::Result<VtmbVpk> {
        let mut entries = HashMap::new();
        let mut entry_list = Vec::new();
        reader.seek(SeekFrom::End(-9))?;
        let entry_count = reader.read_u32le()?;
        let dir_offset = reader.read_u32le()?;
        reader.seek(SeekFrom::Start(dir_offset.into()))?;
        for _ in 0..entry_count {
            let entry = VtmbVpkEntry::from_reader(reader)?;
            entries.insert(entry.file_path.clone(), entry_list.len() + 1);
            entry_list.push(entry);
        }
        Ok(VtmbVpk{
            file_path: path.to_path_buf(),
            header,
            entry_list,
            entries,
        })
    }
}

#[derive(Debug)]
pub struct SourceVpk {
    file_path: PathBuf,
    header: VpkHeader,
    tree_offset: u64,
    entry_list: Vec<SourceVpkEntry>,
    entries: HashMap<String, usize>,
}

impl SourceVpk {
    fn from_reader<R: BufRead + Seek>(reader: &mut R, path: &Path, header: VpkHeader) -> io::Result<SourceVpk> {
        let mut entries = HashMap::new();
        let mut entry_list = Vec::new();
        let tree_offset = reader.stream_position()?;
        loop {
            let type_name = reader.read_ztstring_buf()?;
            if type_name.is_empty() {
                break;
            }
            loop {
                let directory_name = reader.read_ztstring_buf()?;
                if directory_name.is_empty() {
                    break;
                }
                loop {
                    let file_name = reader.read_ztstring_buf()?;
                    if file_name.is_empty() {
                        break;
                    }

                    let full_path =
                        format!("{directory_name}/{file_name}.{type_name}").to_lowercase();
                    let mut entry = SourceVpkEntry::from_reader(reader)?;
                    entry.file_name = full_path.clone();
                    entry_list.push(entry);
                    entries.insert(full_path, entry_list.len() - 1);
                }
            }
        }
        Ok(SourceVpk {
            file_path: path.to_path_buf(),
            header,
            tree_offset,
            entry_list,
            entries,
        })
    }
}

impl SeekRead for BufReader<File> {}

pub struct Vpk {}

impl Vpk {
    pub fn from_path(path: &Path) -> io::Result<Box<dyn VpkReader>> {
        let mut file = File::open(path).map(BufReader::new)?;
        let header = VpkHeader::from_reader(&mut file)?;
        if header.version.0 == 0 {
            Ok(Box::new(VtmbVpk::from_reader(&mut file, path, header)?))
        } else {
            Ok(Box::new(SourceVpk::from_reader(&mut file, path, header)?))
        }
    }
}

impl SourceVpk {
    #[inline(always)]
    fn get_content(&mut self, entry_id: usize) -> Option<Vec<u8>> {
        let entry = &self.entry_list[entry_id];
        let mut res = Vec::with_capacity(entry.preload_data_size as usize);
        if entry.preload_data_size > 0 {
            res.extend(&entry.preload_data);
        }

        if entry.archive_id == 0x7FFF {
            let mut file = File::open(&self.file_path).ok()?;
            file.seek(SeekFrom::Start(
                self.header.tree_size as u64 + entry.offset as u64 + self.tree_offset,
            ))
            .ok()?;
            res.resize(res.len() + entry.size as usize, 0);
            file.read_exact(&mut res[entry.preload_data_size as usize..])
                .ok()?;
            Some(res)
        } else {
            let target_archive_path = {
                let stem_tmp = self.file_path.file_stem()?;
                let stem = stem_tmp.to_str()?[0..stem_tmp.len() - 3].to_owned();
                self.file_path
                    .parent()?
                    .join(format!("{0}{1:03}.vpk", stem, entry.archive_id))
            };
            let mut file = File::open(target_archive_path).ok()?;
            file.seek(SeekFrom::Start(entry.offset as u64)).ok()?;
            res.resize(res.len() + entry.size as usize, 0);
            file.read_exact(&mut res[entry.preload_data_size as usize..])
                .ok()?;
            Some(res)
        }
    }
}

impl VpkReader for SourceVpk {
    fn find_file(&mut self, name: &str) -> Option<Vec<u8>> {
        match self.entries.get(&name.to_lowercase().replace('\\', "/")) {
            None => None,
            Some(&entry_id) => self.get_content(entry_id),
        }
    }

    #[inline(always)]
    fn contains(&self, name: &str) -> bool {
        self.entries
            .contains_key(&name.to_lowercase().replace('\\', "/"))
    }

    fn filter(&mut self, pattern: &str) -> Vec<(String, Vec<u8>)> {
        let rpattern = match glob_to_regex(pattern) {
            Ok(pat) => pat,
            Err(_) => {
                return Vec::new();
            }
        };
        let mut res = Vec::new();
        let entries = self.entries.clone();
        for (key, &entry_id) in entries.iter().filter(|(key, _)| rpattern.is_match(key)) {
            let data = match self.get_content(entry_id) {
                None => {
                    continue;
                }
                Some(data) => data,
            };
            res.push((key.clone(), data))
        }
        res
    }
}

impl VtmbVpk{
    #[inline(always)]
    fn get_content(&mut self, entry_id: usize) -> Option<Vec<u8>> {
        let entry = &self.entry_list[entry_id];
        let mut file = File::open(&self.file_path).ok()?;
        file.seek(SeekFrom::Start(
            entry.offset.into()
        ))
        .ok()?;
        let entry_size = entry.size as usize;
        let mut res = Vec::with_capacity(entry_size);
        res.resize(entry_size, 0);
        file.read_exact(&mut res[0..])
            .ok()?;
        Some(res)
    }
}

impl VpkReader for VtmbVpk {
    fn find_file(&mut self, name: &str) -> Option<Vec<u8>> {
        match self.entries.get(&name.to_lowercase().replace('\\', "/")) {
            None => None,
            Some(&entry_id) => self.get_content(entry_id),
        }
    }

    #[inline(always)]
    fn contains(&self, name: &str) -> bool {
        self.entries
            .contains_key(&name.to_lowercase().replace('\\', "/"))
    }

    fn filter(&mut self, pattern: &str) -> Vec<(String, Vec<u8>)> {
        let rpattern = match glob_to_regex(pattern) {
            Ok(pat) => pat,
            Err(_) => {
                return Vec::new();
            }
        };
        let mut res = Vec::new();
        let entries = self.entries.clone();
        for (key, &entry_id) in entries.iter().filter(|(key, _)| rpattern.is_match(key)) {
            let data = match self.get_content(entry_id) {
                None => {
                    continue;
                }
                Some(data) => data,
            };
            res.push((key.clone(), data))
        }
        res
    }
}

