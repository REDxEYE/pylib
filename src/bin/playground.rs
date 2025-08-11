#![allow(dead_code)]


use std::fs::File;
use std::path::Path;
use std::time::Instant;

use binread::io;

use rustlib::dmx;
use rustlib::source_model::mdl::read_mdl_v49;
use rustlib::vpk::Vpk;
use rustlib::vtf::load_vtf;

fn test_mdl() -> io::Result<()> {
    let mut mdl_file = File::open(r"D:\models\Sangheili-1.0\models\aaa\Sangheili\arbiter_f.mdl").expect("");
    let _vtx_file = File::open(r"D:\models\Sangheili-1.0\models\aaa\Sangheili\arbiter_f.dx90.vtx").expect("");
    // let mut vvd_file = File::open(r"D:\models\Sangheili-1.0\models\aaa\Sangheili\arbiter_f.vvd").expect("");

    let _mdl = read_mdl_v49(&mut mdl_file)?;
    // let vvd = read_vvd_v4(&mut vvd_file)?;
    // println!("{:#?}", mdl);
    // println!("{:?}", vvd);
    // for bodypart in mdl.bodyparts {
    //     for model in bodypart.get_models(&mut mdl_file)? {
    //         // println!("{:?}", model);
    //         for mesh in model.get_meshes(&mut mdl_file)? {
    //             // println!("{:?}", mesh);
    //             for flex in mesh.get_flexes(&mut mdl_file)? {
    //                 // println!("{:?}", flex);
    //                 for anim in flex.get_vertex_anims(&mut mdl_file)? {
    //                     // println!("{:?}", anim);
    //                 }
    //             }
    //         }
    //     }
    // }
    Ok(())
}

fn test_dmx() -> io::Result<()> {
    let dmx = dmx::load_dmx(r"D:\SteamLibrary\steamapps\common\SourceFilmmaker\game\tf_movies\elements\sessions\mtt_heavy\mtt_heavy.dmx").expect("");
    // let dmx = dmx::load_dmx(r"D:\SteamLibrary\steamapps\common\SourceFilmmaker\game\tf_movies\elements\sessions\logo\sfm_logo.dmx")?;
    // let dmx = dmx::load_dmx(r"C:\Users\RED\Documents\Plane.dmx")?;
    println!("{:?}", dmx.elements[0]);

    Ok(())
}

fn test_vpk() -> io::Result<()> {
    let path = r"D:\SteamLibrary\steamapps\common\Left 4 Dead 2\left4dead2_dlc3\pak01_dir.vpk";
    let mut vpk = Vpk::from_path(Path::new(path))?;
    let data = vpk.find_file(r"gamepadui\schemetab.res".into());
    println!("{:?}", data);
    Ok(())
}

fn test_vtf() -> io::Result<()> {
    let path = r"D:\SteamLibrary\steamapps\common\SourceFilmmaker\game\usermod\materials\models\red_eye\MSplashDoggy\Yorha_2b\body.vtf";
    let image = load_vtf(Path::new(path)).map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
    // image.save_with_format("./test.exr", image::ImageFormat::OpenExr).unwrap();
    println!("{}", 1);
    Ok(())
}

fn main() -> io::Result<()> {
    let start = Instant::now();

    test_vpk()?;
    // test_vtf()?;

    let duration = start.elapsed();
    println!("Time taken: {} milliseconds", duration.as_micros() as f32 / 1000f32);

    Ok(())
}
