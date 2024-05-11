use std::cell::RefCell;
use std::io::{BufRead, Error, ErrorKind, Read, Seek};
use std::rc::Rc;
use std::str::FromStr;

use binread::BinReaderExt;
use byteorder::{LE, ReadBytesExt};
use uuid::Uuid;

use crate::dmx::{Dmx, serializer};
use crate::dmx::element::{DmElement, DmPropValue};
use crate::dmx::errors::DmxError;
use crate::dmx::errors::DmxError::InvalidAttributeType;
use crate::dmx::string_dict::{StringDictionary, StringDictionaryV5};
use crate::shared::types::{ArrayFromReader, Color, Matrix, Vector2, Vector3, Vector4};
use crate::utils::reader_utils::{BufReadExt, FromReader};

pub struct DmxBinaryV1 {}

pub struct DmxBinaryV2 {}

pub struct DmxBinaryV4 {}

pub struct DmxBinaryV5 {}

fn read_i32_array<R: Read + Seek>(reader: &mut R) -> Result<Vec<i32>, DmxError> {
    let count = reader.read_u32::<LE>()?;
    let mut items = vec![0i32; count as usize];
    reader.read_i32_into::<LE>(items.as_mut_slice())?;
    Ok(items)
}

fn read_f32_array<R: Read + Seek>(reader: &mut R) -> Result<Vec<f32>, DmxError> {
    let count = reader.read_u32::<LE>()?;
    let mut items = vec![0f32; count as usize];
    reader.read_f32_into::<LE>(items.as_mut_slice())?;
    Ok(items)
}

fn read_u8_array<R: Read + Seek>(reader: &mut R) -> Result<Vec<u8>, DmxError> {
    let count = reader.read_u32::<LE>()?;
    let mut items = vec![0u8; count as usize];
    if count > 0 {
        reader.read_exact(items.as_mut_slice())?;
    }
    Ok(items)
}



impl DmxBinaryV5 {
    fn read_element_prop<R: BufRead + Seek, S: StringDictionary<R>>(string_dictionary: &S, reader: &mut R) -> Result<(Rc<str>, DmPropValue), DmxError> {
        let attrib_name = string_dictionary.read_string(reader)?;
        let attrib_type = reader.read_u8()?;
        let value = match attrib_type {
            1 => Ok(DmPropValue::ElementRef(reader.read_i32::<LE>()?)),
            2 => Ok(DmPropValue::Int32(reader.read_i32::<LE>()?)),
            3 => Ok(DmPropValue::Float32(reader.read_f32::<LE>()?)),
            4 => Ok(DmPropValue::Bool(reader.read_u8()? == 1)),
            5 => Ok(DmPropValue::String(string_dictionary.read_string(reader)?)),
            6 => Ok(DmPropValue::BinaryBlob(read_u8_array(reader)?)),
            7 => Ok(DmPropValue::Time((reader.read_i32::<LE>()? as f32) / 10000f32)),
            8 => Ok(DmPropValue::Color(Color::from_reader(reader)?)),
            9 => Ok(DmPropValue::Vector2(Vector2::from_reader(reader)?)),
            10 => Ok(DmPropValue::Vector3(Vector3::from_reader(reader)?)),
            11 => Ok(DmPropValue::Vector4(Vector4::from_reader(reader)?)),
            12 => Ok(DmPropValue::Angle(Vector3::from_reader(reader)?)),
            13 => Ok(DmPropValue::Quaternion(Vector4::from_reader(reader)?)),
            14 => Ok(DmPropValue::Matrix(Matrix::from_reader(reader)?)),
            15 => { Ok(DmPropValue::ElementRefVector(read_i32_array(reader)?)) }
            16 => { Ok(DmPropValue::Int32Array(read_i32_array(reader)?)) }
            17 => { Ok(DmPropValue::Float32Array(read_f32_array(reader)?)) }
            18 => { Ok(DmPropValue::BoolArray(read_u8_array(reader)?.iter().map(|x| { *x == 1u8 }).collect())) }
            19 => {
                let count = reader.read_u32::<LE>()?;
                let mut items = Vec::with_capacity(count as usize);
                for _ in 0..count {
                    items.push(Rc::from(reader.read_ztstring_buf()?));
                }
                Ok(DmPropValue::StringArray(items))
            }
            20 => {
                let count = reader.read_u32::<LE>()?;
                let mut items = Vec::<Vec<u8>>::with_capacity(count as usize);
                for _ in 0..count {
                    items.push(read_u8_array(reader)?);
                }
                Ok(DmPropValue::BinaryBlobArray(items))
            }
            21 => { Ok(DmPropValue::TimeArray(read_i32_array(reader)?.iter().map(|time| { *time as f32 / 10000f32 }).collect())) }
            22 => { Ok(DmPropValue::ColorArray(Color::array_from_reader(reader)?)) }
            23 => { Ok(DmPropValue::Vector2Array(Vector2::array_from_reader(reader)?)) }
            24 => { Ok(DmPropValue::Vector3Array(Vector3::array_from_reader(reader)?)) }
            25 => { Ok(DmPropValue::Vector4Array(Vector4::array_from_reader(reader)?)) }
            26 => { Ok(DmPropValue::AngleArray(Vector3::array_from_reader(reader)?)) }
            27 => { Ok(DmPropValue::QuaternionArray(Vector4::array_from_reader(reader)?)) }
            28 => { Ok(DmPropValue::MatrixArray(Matrix::array_from_reader(reader)?)) }
            _ => Err(InvalidAttributeType(attrib_type, attrib_name.clone()))
        }?;
        Ok((attrib_name.clone(), value))
    }
}

impl serializer::DmxDeserialize for DmxBinaryV5 {
    fn deserialize<R: BufRead + Seek>(reader: &mut R) -> Result<Dmx, DmxError> {
        let string_dict = StringDictionaryV5::from_file(reader)?;
        let element_count = reader.read_le::<u32>()?;
        let mut elements = Vec::with_capacity(element_count as usize);
        for _ in 0..element_count {
            let dm_type_name = string_dict.read_string(reader)?;
            let dm_name = string_dict.read_string(reader)?;
            let dm_uuid = Uuid::from_u128(reader.read_be()?);
            elements.push(Rc::new(RefCell::new(DmElement::new_from_rc(dm_name, dm_type_name, dm_uuid))));
        }
        for element in elements.iter() {
            let prop_count = reader.read_le::<u32>()?;
            for _ in 0..prop_count {
                let (prop_name, prop_value) = Self::read_element_prop(&string_dict, reader)?;
                element.borrow_mut().set_prop(prop_name, prop_value); // Adjust method to work with this pattern if necessary
            }
        }
        let new_elements = elements.clone();
        for element in new_elements.iter() {
            for property in element.borrow_mut().properties.iter_mut() {
                match *property.1 { // Obtain a mutable reference to the property value
                    DmPropValue::ElementRef(ref mut index) => {
                        *property.1 = match index {
                            -1 => DmPropValue::NullElement,
                            -2 => DmPropValue::ExternalElement(Uuid::from_str(reader.read_ztstring_buf()?.as_str()).map_err(|e| { DmxError::UnknownIOError { source: Error::new(ErrorKind::Other, e) } })?),
                            _ => DmPropValue::Element(Rc::clone(&elements[*index as usize]))
                        }
                    }
                    DmPropValue::ElementRefVector(ref mut indices) => {
                        let mut referenced_elements = Vec::with_capacity(indices.len());
                        for index in indices.iter() {
                            let elem = match index {
                                -1 => DmPropValue::NullElement,
                                -2 => DmPropValue::ExternalElement(Uuid::from_str(reader.read_ztstring_buf()?.as_str()).map_err(|e| { DmxError::UnknownIOError { source: Error::new(ErrorKind::Other, e) } })?),
                                _ => DmPropValue::Element(Rc::clone(&elements[*index as usize]))
                            };
                            referenced_elements.push(elem)
                        }
                        *property.1 = DmPropValue::ElementVector(referenced_elements); // Adjust as necessary
                    }
                    _ => {}
                }
            }
        }

        Ok(Dmx { elements })
    }
}