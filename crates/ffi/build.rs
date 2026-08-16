use std::env;
use std::path::PathBuf;
use std::process::Command;

// 运行外部命令，失败时终止构建。
fn run(cmd: &mut Command, what: &str) {
    let status = cmd
        .status()
        .unwrap_or_else(|e| panic!("failed to run {what}: {e}"));
    if !status.success() {
        panic!("{what} failed with status {status}");
    }
}

// 编译 cpp/ 下全部源文件为静态库并链接进本 crate。
// 任何 C++ 告警或错误都会让整个 cargo 构建失败。
fn main() {
    println!("cargo:rerun-if-changed=../../cpp/src");
    println!("cargo:rerun-if-changed=../../cpp/include");

    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let repo_root = manifest.parent().unwrap().parent().unwrap();
    let cpp_dir = repo_root.join("cpp");
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let include_dir = cpp_dir.join("include");
    let src_dir = cpp_dir.join("src");

    let mut objects = Vec::new();
    for entry in std::fs::read_dir(&src_dir).unwrap() {
        let path = entry.unwrap().path();
        if path.extension().and_then(|e| e.to_str()) == Some("cpp") {
            let object = out_dir.join(path.file_stem().unwrap()).with_extension("o");
            run(
                Command::new("g++")
                    .args(["-std=c++20", "-O2", "-fPIC", "-Wall", "-Wextra", "-Werror"])
                    .arg(format!("-I{}", include_dir.display()))
                    .arg("-c")
                    .arg(&path)
                    .arg("-o")
                    .arg(&object),
                "C++ compile",
            );
            objects.push(object);
        }
    }

    let lib = out_dir.join("libsentinel_core.a");
    run(
        Command::new("ar").arg("rcs").arg(&lib).args(&objects),
        "C++ archive",
    );

    println!("cargo:rustc-link-search=native={}", out_dir.display());
    println!("cargo:rustc-link-lib=static=sentinel_core");
    println!("cargo:rustc-link-lib=dylib=stdc++");
}
