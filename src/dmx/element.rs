use std::cell::RefCell;
use std::collections::HashMap;
use std::fmt::{Debug, Formatter};
use std::rc::Rc;

use uuid::Uuid;

use crate::shared::types::{Color, Matrix, Vector2, Vector3, Vector4};

#[derive(Debug, Clone, PartialEq)]
pub enum DmPropValue {
    ElementRef(i32),
    Element(RcDmElement),
    NullElement,
    ExternalElement(Uuid),
    Int32(i32),
    Float32(f32),
    Bool(bool),
    Byte(u8),
    String(Rc<str>),
    BinaryBlob(Vec<u8>),
    ObjectId,
    Color(Color),
    Vector2(Vector2),
    Vector3(Vector3),
    Vector4(Vector4),
    Angle(Vector3),
    Quaternion(Vector4),
    Matrix(Matrix),
    ElementRefVector(Vec<i32>),
    ElementVector(Vec<DmPropValue>),
    Int32Array(Vec<i32>),
    Float32Array(Vec<f32>),
    BoolArray(Vec<bool>),
    StringArray(Vec<Rc<str>>),
    BinaryBlobArray(Vec<Vec<u8>>),
    ObjectIdArray,
    ColorArray(Vec<Color>),
    Vector2Array(Vec<Vector2>),
    Vector3Array(Vec<Vector3>),
    Vector4Array(Vec<Vector4>),
    AngleArray(Vec<Vector3>),
    QuaternionArray(Vec<Vector4>),
    MatrixArray(Vec<Matrix>),
    Time(f32),
    TimeArray(Vec<f32>),
    UInt64Array(Vec<u64>),
    UInt8Array(Vec<u8>),
}

#[derive(Clone, PartialEq)]
pub struct DmElement {
    pub name: Rc<str>,
    pub type_name: Rc<str>,
    id: Uuid,

    pub properties: HashMap<Rc<str>, DmPropValue>,
}

pub type RcDmElement = Rc<RefCell<DmElement>>;

impl DmElement {
    pub fn new_from_rc(name: Rc<str>, type_name: Rc<str>, id: Uuid) -> DmElement {
        DmElement {
            name,
            type_name,
            id,
            properties: HashMap::new(),
        }
    }
    pub fn new<N, T>(name: N, type_name: T, id: Uuid) -> DmElement
        where
            N: Into<String>,
            T: Into<String>
    {
        DmElement {
            name: Rc::from(name.into()),
            type_name: Rc::from(type_name.into()),
            id,
            properties: HashMap::new(),
        }
    }

    pub fn set_prop(&mut self, name: Rc<str>, value: DmPropValue) {
        self.properties.insert(name, value);
    }
}

impl Debug for DmElement {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}({:?}, {}, {} properties)", self.type_name, self.name, self.id, self.properties.len())
    }
}