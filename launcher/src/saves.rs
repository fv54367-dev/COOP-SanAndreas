// The game's save slots for the launcher's own "new / load / delete" screen: GTASAsf1.b .. GTASAsf8.b in the user
// folder. The header is "BLOCK" + CSimpleVariablesSaveStructure (gta-reversed SimpleVariablesSaveStructure.h):
// +4 the save name (100 bytes), +133/+134/+135 game day/hour/minute, +286 SYSTEMTIME of the save (checked against
// real saves of this mod). The game is started with -autoload new|slotN (client CLaunchManager).
use crate::win32::*;

pub const SLOTS: usize = 8;

pub struct Slot {
    pub name: String,
    pub saved: String, // dd/mm/yyyy hh:mm, real date
    pub game_time: String,
}

// <game>\CoopAndreas\coopandreas.ini [config] user-dir (test setups: relative to the game) or Documents\GTA San Andreas User Files
pub fn user_dir() -> String {
    // the same choice as the client's CTestMode::EarlyInit
    let game = crate::launch::game_dir();
    let local = format!("{}\\CoopAndreas\\coopandreas.ini", game);
    let mut custom = crate::server::ini_read(&local, "config", "user-dir");
    let test_mode = crate::server::ini_read(&local, "config", "test-mode");
    if custom.is_empty() && (test_mode.eq_ignore_ascii_case("true") || test_mode == "1") {
        custom = "UserFiles".to_string();
    }
    let custom = custom.trim_end_matches(['\\', '/']).to_string();
    if !custom.is_empty() {
        return if custom.contains(':') || custom.starts_with('\\') { custom } else { format!("{}\\{}", game, custom) };
    }
    format!("{}\\GTA San Andreas User Files", documents_dir())
}

pub fn documents_dir() -> String {
    let mut buf = [0u16; 260];
    let ok = unsafe { SHGetFolderPathW(0 as HWND, CSIDL_PERSONAL, 0 as HANDLE, 0, buf.as_mut_ptr()) } == 0;
    if ok {
        let n = buf.iter().position(|&c| c == 0).unwrap_or(0);
        return String::from_utf16_lossy(&buf[..n]);
    }
    format!("{}\\Documents", std::env::var("USERPROFILE").unwrap_or_default())
}

fn slot_path(slot: usize) -> String {
    format!("{}\\GTASAsf{}.b", user_dir(), slot + 1)
}

fn parse(data: &[u8]) -> Option<Slot> {
    if data.len() < 5 + 0x138 || &data[..5] != b"BLOCK" {
        return None;
    }
    let s = &data[5..];
    let raw = &s[4..104];
    let end = raw.iter().position(|&b| b == 0).unwrap_or(raw.len());
    // GXT text: plain ASCII for these names; anything else shown as Latin-1
    let name: String = raw[..end].iter().map(|&b| b as char).collect();
    let word = |at: usize| u16::from_le_bytes([s[at], s[at + 1]]);
    let (year, month, day, hour, minute) = (word(286), word(288), word(292), word(294), word(296));
    let saved = if (2000..2100).contains(&year) {
        format!("{:02}/{:02}/{} {:02}:{:02}", day, month, year, hour, minute)
    } else {
        String::new()
    };
    let game_time = format!("D\u{ed}a {} {:02}:{:02}", s[133], s[134], s[135]);
    Some(Slot { name: if name.trim().is_empty() { "Partida".to_string() } else { name }, saved, game_time })
}

pub fn scan() -> Vec<Option<Slot>> {
    (0..SLOTS)
        .map(|slot| std::fs::read(slot_path(slot)).ok().and_then(|data| parse(&data)))
        .collect()
}

pub fn delete(slot: usize) -> Result<(), String> {
    std::fs::remove_file(slot_path(slot)).map_err(|e| format!("No se pudo borrar la partida: {}", e))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_the_header() {
        let mut data = vec![0u8; 5 + 0x138];
        data[..5].copy_from_slice(b"BLOCK");
        let s = &mut data[5..];
        s[4..16].copy_from_slice(b"(Coop) Ryder");
        s[133] = 4;
        s[134] = 6;
        s[135] = 38;
        for (at, v) in [(286, 2026u16), (288, 10), (292, 25), (294, 14), (296, 56)] {
            s[at..at + 2].copy_from_slice(&v.to_le_bytes());
        }
        let slot = parse(&data).unwrap();
        assert_eq!(slot.name, "(Coop) Ryder");
        assert_eq!(slot.saved, "25/10/2026 14:56");
        assert_eq!(slot.game_time, "D\u{ed}a 4 06:38");
        assert!(parse(b"NOPE").is_none());
    }
}
