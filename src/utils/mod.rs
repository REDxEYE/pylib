use std::io::{self, BufRead, ErrorKind};

mod comp_ibuf;
mod comp_vbuf;

pub mod reader_utils;

#[allow(unused)]
pub use comp_vbuf::compressed_vertex_buffer::decode_vertex_buffer;
#[allow(unused)]
pub use comp_ibuf::compressed_index_buffer::decode_index_buffer;


pub fn read_nullstring<R: BufRead>(reader: &mut R) -> io::Result<String> {
    optick::event!("Read null string");
    let mut buf = vec![];
    reader.read_until(0, &mut buf)?;
    buf.remove(buf.len()-1);
    String::from_utf8(buf).map_err(|e| { io::Error::new(ErrorKind::Other, e) })
}
