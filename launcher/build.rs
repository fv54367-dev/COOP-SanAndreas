// Compiles version.rc (VersionInfo + icon + embedded manifest) with rc.exe from the Windows SDK.
// No external crates: rc.exe is located via the SDK env or by scanning the standard install.
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let manifest_dir = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let out_dir = PathBuf::from(std::env::var("OUT_DIR").unwrap());
    println!("cargo:rerun-if-changed=version.rc");
    println!("cargo:rerun-if-changed=icon.ico");
    println!("cargo:rerun-if-changed=coopsa-logo.bmp");
    println!("cargo:rerun-if-changed=CoopSA.exe.manifest");

    // the footer shows the mod version: taken from shared/config.h so it never falls behind
    let config = manifest_dir.join("..").join("shared").join("config.h");
    println!("cargo:rerun-if-changed={}", config.display());
    let version = std::fs::read_to_string(&config)
        .ok()
        .and_then(|s| {
            let line = s.lines().find(|l| l.contains("#define COOPSA_VERSION"))?.to_string();
            let start = line.find('"')? + 1;
            let end = line[start..].find('"')? + start;
            Some(line[start..end].to_string())
        })
        .unwrap_or_else(|| "?".to_string());
    println!("cargo:rustc-env=COOPSA_VERSION={}", version);

    let rc = find_rc().expect("no se encontro rc.exe (Windows SDK)");
    let res = out_dir.join("version.res");
    let status = Command::new(&rc)
        .args(["/nologo", "/fo"])
        .arg(&res)
        .arg("version.rc")
        .current_dir(&manifest_dir)
        .status()
        .expect("rc.exe no se pudo ejecutar");
    if !status.success() {
        panic!("rc.exe fallo al compilar version.rc");
    }
    println!("cargo:rustc-link-arg={}", res.display());
}

fn find_rc() -> Option<PathBuf> {
    // honor the SDK env when present (vcvars / VS terminals)
    if let (Ok(dir), Ok(ver)) = (std::env::var("WindowsSdkDir"), std::env::var("WindowsSdkVersion")) {
        for arch in ["x64", "x86"] {
            let p = PathBuf::from(&dir).join("bin").join(&ver).join(arch).join("rc.exe");
            if p.exists() {
                return Some(p);
            }
        }
    }
    // scan the standard SDK install for the newest version
    let root = PathBuf::from(r"C:\Program Files (x86)\Windows Kits\10\bin");
    let mut best: Option<(String, PathBuf)> = None;
    if let Ok(rd) = std::fs::read_dir(&root) {
        for e in rd.flatten() {
            let name = e.file_name().to_string_lossy().to_string();
            if !name.starts_with("10.") {
                continue;
            }
            for arch in ["x64", "x86"] {
                let p = e.path().join(arch).join("rc.exe");
                if p.exists() && best.as_ref().map_or(true, |(v, _)| name > *v) {
                    best = Some((name.clone(), p));
                }
            }
        }
    }
    best.map(|(_, p)| p)
}
