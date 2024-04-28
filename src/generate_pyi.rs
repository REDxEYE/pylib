use std::{env, fs, process};
use std::fs::File;
use std::io::Write;
use std::path::Path;

use pyo3::prelude::*;
use pyo3::types::PyDict;

fn generate_pyi(path:&Path) -> PyResult<()> {
    pyo3::prepare_freethreaded_python();
    Python::with_gil(|py| {
        let module = py.import_bound("rustlib").unwrap();
        py.run_bound(r#"import inspect, types
def generate_pyi(module):
    buf = ""
    for name, obj in inspect.getmembers(module):
        if inspect.isclass(obj):
            buf += f"class {name}:\n"
            for cname, cobj in inspect.getmembers(obj):
                if cname.startswith("__"):continue
                if isinstance(cobj, (types.FunctionType,types.MethodType, types.MethodDescriptorType)):
                    args = inspect.signature(cobj)
                    print(args)
                    buf += f"    def {cname}{args}: ...\n"
                elif isinstance(cobj, types.BuiltinFunctionType):
                    args = str(inspect.signature(cobj))
                    buf += "    @classmethod\n"
                    args = f"(cls, {args[1:]}"
                    buf += f"    def {cname}{args}: ...\n"
            buf += "\n"
        elif inspect.isfunction(obj):
            args = inspect.signature(obj)
            buf+=(f"def {name}{args}: ...\n")
        elif inspect.isbuiltin(obj):
            # Assuming all built-ins are functions for simplicity
            args = inspect.signature(obj)
            buf+=(f"def {name}{args}: ...\n")
        elif isinstance(obj, (int, float, str, bool)):
            t = type(obj).__name__
            buf+=(f"{name}: {t}\n")
    return buf
            "#, None, None).unwrap();
        let locals = PyDict::new_bound(py);
        locals.set_item("module", module).unwrap();
        let res = py.eval_bound("generate_pyi(module)", None, Some(&locals)).unwrap();
        let mut file = File::create(path.join("rustlib.pyi")).unwrap();
        file.write_all(res.to_string().as_bytes()).unwrap();
        println!("{}", res);
    });
    Ok(())
}

fn main() -> PyResult<()> {
    let exe_path = match env::current_exe() {
        Ok(path) => path,
        Err(e) => {
            eprintln!("Failed to get the current executable path: {}", e);
            process::exit(1);
        }
    };

    // Get the directory containing the executable
    let target_dir = exe_path.parent().unwrap_or_else(|| {
        eprintln!("Failed to determine the directory of the executable.");
        process::exit(1);
    });

    let dll_path = target_dir.join("rustlib.dll");
    let pyd_path = target_dir.join("rustlib.pyd");

    // Rename the file if it exists
    if dll_path.exists() {
        fs::copy(&dll_path, &pyd_path).expect("Could not rename DLL to PYD");
    } else {
        println!("File not found!")
    }

    generate_pyi(target_dir)
}