// Update check for the top-right button: same source as dist/ACTUALIZAR.bat (GitHub release
// channels of the public repo and CoopAndreas_version.txt next to gta_sa.exe). The install is done here too
// (start_install); ACTUALIZAR.bat stays as the fallback when it fails.
use std::os::windows::process::CommandExt;
use std::process::Command;
use std::sync::Mutex;

const REPO: &str = "fv54367-dev/COOP-SanAndreas";
const CREATE_NO_WINDOW: u32 = 0x0800_0000;
const CREATE_NEW_CONSOLE: u32 = 0x0000_0010;

pub const CHECKING: usize = 0;
pub const UP_TO_DATE: usize = 1;
pub const PENDING: usize = 2;
pub const UNKNOWN: usize = 3;

// channel to install (without "canal-") and the text shown next to the button
pub static LAST: Mutex<(String, String)> = Mutex::new((String::new(), String::new()));

fn first_line(path: &str) -> Option<String> {
    let text = std::fs::read_to_string(path).ok()?;
    let line = text.lines().next()?.trim().trim_start_matches('\u{feff}').to_string();
    if line.is_empty() { None } else { Some(line) }
}

// reads the JSON string value of the next `"key"` at or after `from`
fn json_str(s: &str, key: &str, from: usize) -> Option<(String, usize)> {
    let pat = format!("\"{}\"", key);
    let at = from + s[from..].find(&pat)? + pat.len();
    let rest = s[at..].trim_start().strip_prefix(':')?.trim_start();
    let start = s.len() - rest.len();
    let mut chars = rest.strip_prefix('"')?.char_indices();
    let mut out = String::new();
    while let Some((i, c)) = chars.next() {
        match c {
            '"' => return Some((out, start + 1 + i + 1)),
            '\\' => {
                if let Some((_, e)) = chars.next() {
                    out.push(e);
                }
            }
            _ => out.push(c),
        }
    }
    None
}

// (tag, title) of every release
fn releases(json: &str) -> Vec<(String, String)> {
    let mut list = Vec::new();
    let mut pos = 0;
    while let Some((tag, after)) = json_str(json, "tag_name", pos) {
        // GitHub sends "name" right after "tag_name"/"target_commitish"
        let (name, next) = json_str(json, "name", after).unwrap_or((String::new(), after));
        list.push((tag, name));
        pos = next;
    }
    list
}

// the label the CI puts between brackets at the end of the title: "Principal [main-1a2b3c4]"
fn label_of(title: &str) -> Option<String> {
    let open = title.rfind('[')?;
    let close = title[open..].find(']')? + open;
    Some(title[open + 1..close].to_string())
}

fn check(dir: &str) -> (usize, String, String) {
    let installed = first_line(&format!("{}\\CoopAndreas_version.txt", dir)).unwrap_or_default();

    let out = Command::new("curl.exe")
        .args(["-fsSL", "--max-time", "15", "-H", "Accept: application/vnd.github+json"])
        .arg(format!("https://api.github.com/repos/{}/releases?per_page=100", REPO))
        .creation_flags(CREATE_NO_WINDOW)
        .output();
    let json = match out {
        Ok(o) if o.status.success() => String::from_utf8_lossy(&o.stdout).into_owned(),
        _ => return (UNKNOWN, String::new(), "No se pudo consultar GitHub".to_string()),
    };
    let list = releases(&json);

    // stay on the installed channel (main-, todo-, prN-); anything else follows main
    let prefix = installed.split('-').next().unwrap_or("").to_string();
    let wanted = match prefix.as_str() {
        "todo" => list.iter().find(|(tag, _)| tag == "canal-todo"),
        p if p.starts_with("pr") => {
            let head = format!("PR {}:", &p[2..]);
            list.iter().find(|(tag, name)| tag.starts_with("canal-") && name.starts_with(&head))
        }
        _ => None,
    };
    let release = match wanted.or_else(|| list.iter().find(|(tag, _)| tag == "canal-main")) {
        Some(r) => r,
        None => return (UNKNOWN, String::new(), "Todav\u{ed}a no hay versiones publicadas".to_string()),
    };
    let canal = release.0.trim_start_matches("canal-").to_string();
    match label_of(&release.1) {
        Some(label) if label == installed => (UP_TO_DATE, canal, format!("Al d\u{ed}a ({})", label)),
        Some(label) => (PENDING, canal, format!("Actualizar a {}", label)),
        None => (UNKNOWN, canal, "No se pudo leer la versi\u{f3}n publicada".to_string()),
    }
}

// runs the check off the UI thread and posts `msg` with the state in WPARAM
pub fn start_check(hwnd: crate::win32::HWND, msg: u32) {
    let dir = crate::launch::game_dir();
    std::thread::spawn(move || {
        let (state, canal, text) = check(&dir);
        if let Ok(mut last) = LAST.lock() {
            *last = (canal, text);
        }
        unsafe {
            crate::win32::PostMessageW(hwnd, msg, state, 0);
        }
    });
}

// opens ACTUALIZAR.bat in its own console for `canal`; the launcher must exit right after so
// the .bat can replace CoopSA.exe (it waits for it and starts it again when it finishes)
pub fn run_updater(canal: &str) -> Result<(), String> {
    let dir = crate::launch::game_dir();
    let bat = format!("{}\\ACTUALIZAR.bat", dir);
    if std::fs::metadata(&bat).is_err() {
        return Err("No se encontr\u{f3} ACTUALIZAR.bat junto a gta_sa.exe.".to_string());
    }
    Command::new("cmd.exe")
        .arg("/c")
        .arg(&bat)
        .env("CANAL", canal)
        .env("COOPSA_LANZADOR", "1")
        .current_dir(&dir)
        .creation_flags(CREATE_NEW_CONSOLE)
        .spawn()
        .map(|_| ())
        .map_err(|e| format!("No se pudo abrir ACTUALIZAR.bat: {}", e))
}

// ---- install: what ACTUALIZAR.bat does, done by the launcher itself ----

pub const INSTALLING: usize = 4;
pub const INSTALLED: usize = 5;
pub const FAILED: usize = 6;

// the package files and the folder (inside the GTA one) each goes to, as in ACTUALIZAR.bat
const FILES: [(&str, &str); 7] = [
    ("CoopAndreasSA.dll", ""),
    ("CoopSA.exe", ""),
    ("CoopSA.exe.manifest", ""),
    ("MODO_SCRIPTS_ORIGINALES.bat", ""),
    ("main.scm", "CoopAndreas"),
    ("script.img", "CoopAndreas"),
    ("server.exe", "SERVIDOR"),
];

fn join(dir: &str, sub: &str, name: &str) -> String {
    if sub.is_empty() { format!("{}\\{}", dir, name) } else { format!("{}\\{}\\{}", dir, sub, name) }
}

fn folder(dir: &str, sub: &str) -> String {
    if sub.is_empty() { dir.to_string() } else { format!("{}\\{}", dir, sub) }
}

fn exists(path: &str) -> bool {
    std::fs::metadata(path).is_ok()
}

// System32 tools by full path: a tar.exe from Git in the PATH can't open zips
fn system_tool(name: &str) -> String {
    let root = std::env::var("SystemRoot").unwrap_or_else(|_| "C:\\Windows".to_string());
    format!("{}\\System32\\{}", root, name)
}

fn running(names: &[&str]) -> bool {
    let out = Command::new(system_tool("tasklist.exe"))
        .args(["/NH", "/FO", "CSV"])
        .creation_flags(CREATE_NO_WINDOW)
        .output();
    let list = match out {
        Ok(o) => String::from_utf8_lossy(&o.stdout).to_lowercase(),
        Err(_) => return false,
    };
    names.iter().any(|n| list.contains(&format!("\"{}\"", n)))
}

fn download(url: &str, dest: &str) -> Result<(), String> {
    let ok = Command::new("curl.exe")
        .args(["-fsSL", "--max-time", "600", "-o"])
        .arg(dest)
        .arg(url)
        .creation_flags(CREATE_NO_WINDOW)
        .status()
        .map(|s| s.success())
        .unwrap_or(false);
    if ok { Ok(()) } else { Err("Fall\u{f3} la descarga. Revis\u{e1} la conexi\u{f3}n y reintent\u{e1}.".to_string()) }
}

fn sha256_hex(data: &[u8]) -> String {
    const K: [u32; 64] = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    ];
    let mut h: [u32; 8] = [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19];
    let mut tail = data[data.len() - data.len() % 64..].to_vec();
    tail.push(0x80);
    while tail.len() % 64 != 56 {
        tail.push(0);
    }
    tail.extend_from_slice(&((data.len() as u64).wrapping_mul(8)).to_be_bytes());
    for chunk in data[..data.len() - data.len() % 64].chunks(64).chain(tail.chunks(64)) {
        let mut w = [0u32; 64];
        for i in 0..16 {
            w[i] = u32::from_be_bytes([chunk[4 * i], chunk[4 * i + 1], chunk[4 * i + 2], chunk[4 * i + 3]]);
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16].wrapping_add(s0).wrapping_add(w[i - 7]).wrapping_add(s1);
        }
        let mut v = h;
        for i in 0..64 {
            let s1 = v[4].rotate_right(6) ^ v[4].rotate_right(11) ^ v[4].rotate_right(25);
            let ch = (v[4] & v[5]) ^ (!v[4] & v[6]);
            let t1 = v[7].wrapping_add(s1).wrapping_add(ch).wrapping_add(K[i]).wrapping_add(w[i]);
            let s0 = v[0].rotate_right(2) ^ v[0].rotate_right(13) ^ v[0].rotate_right(22);
            let maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            let t2 = s0.wrapping_add(maj);
            v = [t1.wrapping_add(t2), v[0], v[1], v[2], v[3].wrapping_add(t1), v[4], v[5], v[6]];
        }
        for i in 0..8 {
            h[i] = h[i].wrapping_add(v[i]);
        }
    }
    h.iter().map(|x| format!("{:08X}", x)).collect()
}

fn stamp() -> String {
    let mut t = crate::win32::SYSTEMTIME::default();
    unsafe {
        crate::win32::GetLocalTime(&mut t);
    }
    format!("{:04}{:02}{:02}-{:02}{:02}{:02}", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond)
}

// Open Limit Adjuster moves the script memory unless these two are 0: the coop then misses every mission (14-11)
fn fix_limit_adjuster(path: &str) {
    let Ok(data) = std::fs::read(path) else { return };
    let mut lines: Vec<Vec<u8>> =
        data.split(|&b| b == b'\n').map(|l| l.strip_suffix(b"\r").unwrap_or(l).to_vec()).collect();
    if lines.last().map_or(false, |l| l.is_empty()) {
        lines.pop();
    }
    lines.retain(|l| {
        let s = String::from_utf8_lossy(l);
        match s.split_once('=') {
            Some((key, _)) => {
                let key = key.trim();
                !(key.eq_ignore_ascii_case("MAINsegmentSize") || key.eq_ignore_ascii_case("MissionSize"))
            }
            None => true,
        }
    });
    let section = lines.iter().position(|l| String::from_utf8_lossy(l).trim().eq_ignore_ascii_case("[SALIMITS]"));
    let at = match section {
        Some(i) => i + 1,
        None => {
            lines.push(b"[SALIMITS]".to_vec());
            lines.len()
        }
    };
    lines.insert(at, b"MissionSize = 0".to_vec());
    lines.insert(at, b"MAINsegmentSize = 0".to_vec());
    let mut out = Vec::new();
    for l in lines {
        out.extend_from_slice(&l);
        out.extend_from_slice(b"\r\n");
    }
    let _ = std::fs::write(path, out);
}

fn install(dir: &str, canal: &str, progress: &dyn Fn(&str)) -> Result<String, String> {
    if !exists(&join(dir, "", "gta_sa.exe")) {
        return Err("CoopSA.exe tiene que estar junto a gta_sa.exe.".to_string());
    }
    if running(&["gta_sa.exe", "server.exe"]) {
        return Err("Cerr\u{e1} el GTA y el servidor y volv\u{e9} a intentar.".to_string());
    }
    let backup = format!("{}\\_backup_mod\\canal-{}", dir, stamp());
    let stage = format!("{}\\descarga", backup);
    let files = format!("{}\\archivos", stage);
    std::fs::create_dir_all(&files).map_err(|e| format!("No se pudo crear {}: {}", files, e))?;

    progress("Descargando...");
    let base = format!("https://github.com/{}/releases/download/canal-{}", REPO, canal);
    let zip = format!("{}\\coopandreas.zip", stage);
    let man = format!("{}\\manifest.json", stage);
    download(&format!("{}/coopandreas.zip", base), &zip)?;
    download(&format!("{}/manifest.json", base), &man)?;

    progress("Verificando...");
    let unreadable = || "No se pudo leer el manifest.json del canal.".to_string();
    let manifest = std::fs::read_to_string(&man).map_err(|_| unreadable())?;
    let manifest = manifest.trim_start_matches('\u{feff}');
    let (want, _) = json_str(manifest, "zipSha256", 0).ok_or_else(unreadable)?;
    let (label, _) = json_str(manifest, "etiqueta", 0).ok_or_else(unreadable)?;
    let data = std::fs::read(&zip).map_err(|e| format!("No se pudo leer el paquete bajado: {}", e))?;
    if !sha256_hex(&data).eq_ignore_ascii_case(&want) {
        return Err("El archivo bajado no coincide con su hash. No se instal\u{f3} nada. Reintent\u{e1} en un minuto.".to_string());
    }
    let extracted = Command::new(system_tool("tar.exe"))
        .arg("-xf")
        .arg(&zip)
        .arg("-C")
        .arg(&files)
        .creation_flags(CREATE_NO_WINDOW)
        .status()
        .map(|s| s.success())
        .unwrap_or(false);
    if !extracted {
        return Err("No se pudo descomprimir el paquete (hace falta Windows 10 o m\u{e1}s nuevo).".to_string());
    }
    if let Some((name, _)) = FILES.iter().find(|(name, _)| !exists(&join(&files, "", name))) {
        return Err(format!("Al paquete le falta {}. No se instal\u{f3} nada.", name));
    }

    progress("Instalando...");
    for (name, sub) in FILES {
        let current = join(dir, sub, name);
        if exists(&current) {
            let folder = folder(&backup, sub);
            std::fs::create_dir_all(&folder)
                .and_then(|_| std::fs::copy(&current, join(&folder, "", name)))
                .map_err(|e| format!("No se pudo respaldar {}: {}. No se instal\u{f3} nada.", name, e))?;
        }
    }
    // the running launcher can't be overwritten, but it can be renamed; the .old goes at the next start
    let me = join(dir, "", "CoopSA.exe");
    let old = format!("{}.old", me);
    let _ = std::fs::remove_file(&old);
    let moved = std::fs::rename(&me, &old).is_ok();
    let mut copied: Vec<(&str, &str)> = Vec::new();
    for (name, sub) in FILES {
        let result = std::fs::create_dir_all(folder(dir, sub))
            .and_then(|_| std::fs::copy(join(&files, "", name), join(dir, sub, name)));
        if let Err(e) = result {
            for (n, s) in &copied {
                let previous = join(&backup, s, n);
                if exists(&previous) {
                    let _ = std::fs::copy(&previous, join(dir, s, n));
                }
            }
            if moved {
                let _ = std::fs::remove_file(&me);
                let _ = std::fs::rename(&old, &me);
            }
            return Err(format!("No se pudo copiar {}; se volvi\u{f3} a la versi\u{f3}n anterior. {}", name, e));
        }
        copied.push((name, sub));
    }

    // the loader (proxy eax.dll, starts the mod only with --coop): the game's own eax.dll becomes eax_orig.dll the first time
    let proxy = join(&files, "", "eax.dll");
    if exists(&proxy) {
        let eax = join(dir, "", "eax.dll");
        let orig = join(dir, "", "eax_orig.dll");
        if !exists(&orig) && exists(&eax) {
            let _ = std::fs::rename(&eax, &orig);
        }
        if exists(&eax) {
            let _ = std::fs::copy(&eax, join(&backup, "", "eax.dll"));
        }
        std::fs::copy(&proxy, &eax).map_err(|e| format!("No se pudo copiar eax.dll: {}", e))?;
    }
    for name in ["LaunchCoopAndreas.exe", "LaunchCoopAndreas.exe.manifest"] {
        let previous = join(dir, "", name);
        if exists(&previous) {
            let _ = std::fs::rename(&previous, join(&backup, "", name));
        }
    }
    fix_limit_adjuster(&join(dir, "", "III.VC.SA.LimitAdjuster.ini"));
    fix_limit_adjuster(&join(dir, "scripts", "III.VC.SA.LimitAdjuster.ini"));

    let _ = std::fs::write(join(dir, "", "CoopAndreas_version.txt"), format!("{}\r\n", label));
    let _ = std::fs::copy(join(&files, "", "ACTUALIZAR.bat"), join(dir, "", "ACTUALIZAR.bat"));
    let _ = std::fs::remove_dir_all(&stage);
    Ok(format!("Listo: instalado {}.\n\nTu amigo tiene que tener este mismo c\u{f3}digo. Respaldo: {}", label, backup))
}

// installs `canal` off the UI thread; posts `msg` with INSTALLING on each step, then INSTALLED or FAILED
// (the text of each one is in LAST)
pub fn start_install(hwnd: crate::win32::HWND, msg: u32, canal: String) {
    let dir = crate::launch::game_dir();
    std::thread::spawn(move || {
        let post = |state: usize, text: &str| {
            if let Ok(mut last) = LAST.lock() {
                last.1 = text.to_string();
            }
            unsafe {
                crate::win32::PostMessageW(hwnd, msg, state, 0);
            }
        };
        match install(&dir, &canal, &|text| post(INSTALLING, text)) {
            Ok(text) => post(INSTALLED, &text),
            Err(text) => post(FAILED, &text),
        }
    });
}

// opens the freshly installed launcher; this one exits right after
pub fn restart() -> Result<(), String> {
    let dir = crate::launch::game_dir();
    Command::new(join(&dir, "", "CoopSA.exe"))
        .current_dir(&dir)
        .spawn()
        .map(|_| ())
        .map_err(|e| format!("No se pudo volver a abrir CoopSA.exe: {}", e))
}

// the launcher replaced by the last update: it exits a moment after starting this one
pub fn remove_old() {
    let old = join(&crate::launch::game_dir(), "", "CoopSA.exe.old");
    if !exists(&old) {
        return;
    }
    std::thread::spawn(move || {
        for _ in 0..20 {
            if std::fs::remove_file(&old).is_ok() {
                return;
            }
            std::thread::sleep(std::time::Duration::from_millis(500));
        }
    });
}

#[cfg(test)]
mod tests {
    #[test]
    fn sha256() {
        assert_eq!(super::sha256_hex(b""), "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855");
        assert_eq!(super::sha256_hex(b"abc"), "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
        let long = vec![b'a'; 1_000_000];
        assert_eq!(super::sha256_hex(&long), "CDC76E5C9914FB9281A1C7E284D73E67F1809A48A497200E046D39CCC7112CD0");
    }

    // downloads the main channel into COOPSA_TEST_DIR (a folder with a gta_sa.exe, real or not)
    #[test]
    #[ignore]
    fn install_main() {
        let dir = std::env::var("COOPSA_TEST_DIR").expect("COOPSA_TEST_DIR");
        let result = super::install(&dir, "main", &|text| println!("{}", text));
        println!("{:?}", result);
        assert!(result.is_ok());
    }
}
