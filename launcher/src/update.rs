// Update check for the top-right button: same source as dist/ACTUALIZAR.bat (GitHub release
// channels of the public repo and CoopAndreas_version.txt next to gta_sa.exe). The install itself is
// left to ACTUALIZAR.bat, started with CANAL set so it does not ask.
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
