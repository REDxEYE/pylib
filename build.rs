use std::env;
use std::fs;
use std::path::Path;

fn main(){
    // Tell cargo to rerun this script if the .dll changes
    println!("cargo:rerun-if-changed=target/*/rustlib.dll");

    // Use the OUT_DIR environment variable provided by cargo to get the output directory
    let out_dir = env::var("OUT_DIR").unwrap();
    let target_dir = Path::new(&out_dir).ancestors().nth(3).unwrap();
    println!("HELLO! {:?}", target_dir.display());

    let dll_path = target_dir.join("rustlib.dll");
    let pyd_path = target_dir.join("rustlib.pyd");
    println!("HELLO2! {:?} {:?}", dll_path, pyd_path);

    // Rename the file if it exists
    if dll_path.exists() {
        fs::rename(&dll_path, &pyd_path).expect("Could not rename DLL to PYD");
    }
}
