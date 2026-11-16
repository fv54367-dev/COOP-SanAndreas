// Mod compatibility check before starting the game: the GTA folder's plugins (.asi), CLEO scripts and Modloader
// mods against a list of known ones, the dependencies they need and the exe version. It never blocks: it tells what
// it found and the player decides. The in-game check against the host (mod-check, server) is the one that counts.
use std::fs;
use std::path::Path;

// gta_sa.exe 1.0 US (HOODLUM / original): the version the coop hooks
const EXE_10_US_SIZE: u64 = 14_383_616;

#[derive(Clone, Copy, PartialEq)]
enum Verdict {
    Ok,
    Risky,
    Broken,
}

// lower case pieces of a mod folder / file name. First match wins, so the specific ones go first.
const KNOWN: &[(&str, Verdict, &str)] = &[
    // breaks the coop
    ("storyline", Verdict::Broken, "cambia la historia y las misiones; el coop usa su propio main.scm"),
    // works, but each game runs it by itself: the people/cars/events it makes can show up twice or differ
    ("urbanize", Verdict::Risky, "crea gente y objetos en cada juego por separado (pueden verse duplicados)"),
    ("random event", Verdict::Risky, "eventos con gente y autos creados por cada juego por separado"),
    ("street musician", Verdict::Risky, "crea musicos en cada juego por separado"),
    ("immerse", Verdict::Risky, "cambia mucho la jugabilidad con scripts propios"),
    ("environmental kill", Verdict::Risky, "muertes con scripts propios; las de NPC de otro jugador pueden no verse"),
    ("insult", Verdict::Risky, "interacciones con NPC por script; con NPC de otro jugador pueden fallar"),
    ("gangs with music", Verdict::Risky, "objetos y sonidos por script en cada juego"),
    ("peds use vend", Verdict::Risky, "tareas de NPC por script"),
    ("extended gang wars", Verdict::Risky, "las guerras de bandas las maneja el host; con esto puede haber diferencias"),
    ("ladder", Verdict::Risky, "animaciones propias: el otro jugador tiene que tenerlo para verlas"),
    ("handshake", Verdict::Risky, "animaciones propias: el otro jugador tiene que tenerlo para verlas"),
    ("rural car repair", Verdict::Risky, "script que toca autos"),
    ("mulholland safehouse", Verdict::Risky, "cambia el mapa: los dos tienen que tenerlo igual"),
    ("ring donut", Verdict::Risky, "cambia el mapa: los dos tienen que tenerlo igual"),
    ("original peds vary", Verdict::Risky, "agrega personajes: los dos tienen que tenerlo igual (si no, no deja entrar)"),
    ("cutscene characters", Verdict::Risky, "cambia personajes: los dos tienen que tenerlo igual"),
    ("pedskills", Verdict::Risky, "cambia a los NPC: mejor igual en los dos"),
    ("ped tweaks", Verdict::Risky, "cambia a los NPC: mejor igual en los dos"),
    ("mixsets", Verdict::Risky, "cambia opciones del juego (trafico, gente, policia): mismo .ini en los dos"),
    ("wheel detach", Verdict::Risky, "las ruedas sueltas no se sincronizan"),
    ("limit adjuster", Verdict::Risky, "amplia limites del juego; el coop lo soporta en parte, mismo en los dos"),
    ("limitadjuster", Verdict::Risky, "amplia limites del juego; el coop lo soporta en parte, mismo en los dos"),
    // fine: fixes, visuals, sounds, interface, animations of your own CJ
    ("silentpatch", Verdict::Ok, ""),
    ("modloader", Verdict::Ok, ""),
    ("proper fixes", Verdict::Ok, ""),
    ("skygfx", Verdict::Ok, ""),
    ("2dfx", Verdict::Ok, ""),
    ("essentials", Verdict::Ok, ""),
    ("sound", Verdict::Ok, ""),
    ("loadscreen", Verdict::Ok, ""),
    ("radar", Verdict::Ok, ""),
    ("interface", Verdict::Ok, ""),
    ("anim", Verdict::Ok, ""),
    ("streaming", Verdict::Ok, ""),
    ("pedfuncs", Verdict::Ok, ""),
    ("cleo", Verdict::Ok, ""),
    ("enb", Verdict::Ok, ""),
    ("reshade", Verdict::Ok, ""),
    ("gangs guns", Verdict::Ok, ""),
    ("npc tuning", Verdict::Ok, ""),
    ("walkstyle", Verdict::Ok, ""),
    ("pedal", Verdict::Ok, ""),
    ("weapon detail", Verdict::Ok, ""),
    ("cj 90", Verdict::Ok, ""),
    ("fix", Verdict::Ok, ""),
];

// files of this PC's test setups, not mods
const IGNORED: &[&str] = &["sa_bridge.asi", "_nodep.asi"];

pub struct Report {
    pub broken: Vec<String>,
    pub risky: Vec<String>,
    pub missing: Vec<String>,
    pub unknown: usize,
}

impl Report {
    pub fn is_clean(&self) -> bool {
        self.broken.is_empty() && self.risky.is_empty() && self.missing.is_empty()
    }

    pub fn text(&self) -> String {
        let mut text = String::new();
        if !self.broken.is_empty() {
            text.push_str("NO ANDAN CON EL COOP:\n");
            for line in &self.broken {
                text.push_str(&format!("  - {}\n", line));
            }
            text.push('\n');
        }
        if !self.missing.is_empty() {
            text.push_str("FALTAN DEPENDENCIAS:\n");
            for line in &self.missing {
                text.push_str(&format!("  - {}\n", line));
            }
            text.push('\n');
        }
        if !self.risky.is_empty() {
            text.push_str("PUEDEN DAR PROBLEMAS EN COOP:\n");
            for line in &self.risky {
                text.push_str(&format!("  - {}\n", line));
            }
            text.push('\n');
        }
        if self.unknown > 0 {
            text.push_str(&format!("Hay {} mods que no conocemos todavia.\n\n", self.unknown));
        }
        text.push_str("Al entrar a una partida tambien se comparan los autos/personajes/armas con los del host.");
        text
    }
}

fn verdict(name: &str) -> Option<(Verdict, &'static str)> {
    let lower = name.to_lowercase();
    KNOWN.iter().find(|(piece, _, _)| lower.contains(piece)).map(|(_, verdict, why)| (*verdict, *why))
}

fn has_ext(name: &str, exts: &[&str]) -> bool {
    let lower = name.to_lowercase();
    exts.iter().any(|ext| lower.ends_with(ext))
}

// every file name under `dir` (lower case), a few levels deep
fn files_under(dir: &Path, depth: u32, out: &mut Vec<String>) {
    if depth == 0 {
        return;
    }
    let Ok(entries) = fs::read_dir(dir) else { return };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().to_lowercase();
        match entry.file_type() {
            Ok(kind) if kind.is_dir() => files_under(&entry.path(), depth - 1, out),
            Ok(_) => out.push(name),
            Err(_) => {}
        }
    }
}

fn file_names(dir: &Path) -> Vec<String> {
    let Ok(entries) = fs::read_dir(dir) else { return Vec::new() };
    entries
        .flatten()
        .filter(|e| e.file_type().map(|t| t.is_file()).unwrap_or(false))
        .map(|e| e.file_name().to_string_lossy().to_lowercase())
        .collect()
}

pub fn scan(game_dir: &str) -> Report {
    let root = Path::new(game_dir);
    let mut report = Report { broken: Vec::new(), risky: Vec::new(), missing: Vec::new(), unknown: 0 };

    match fs::metadata(root.join("gta_sa.exe")) {
        Ok(meta) if meta.len() != EXE_10_US_SIZE => report.risky.push(format!(
            "gta_sa.exe no es el 1.0 US original ({} bytes): el coop esta hecho para ese exe",
            meta.len()
        )),
        _ => {}
    }

    // what is installed: name shown to the player + every file it brings
    let mut mods: Vec<(String, Vec<String>)> = Vec::new();
    let root_files = file_names(root);
    for name in root_files.iter().filter(|n| has_ext(n, &[".asi"])) {
        mods.push((name.clone(), vec![name.clone()]));
    }
    for sub in ["scripts", "plugins"] {
        for name in file_names(&root.join(sub)).iter().filter(|n| has_ext(n, &[".asi"])) {
            mods.push((name.clone(), vec![name.clone()]));
        }
    }
    let mut cleo_files = Vec::new();
    files_under(&root.join("cleo"), 3, &mut cleo_files);
    for name in cleo_files.iter().filter(|n| has_ext(n, &[".cs", ".cs3", ".cs4", ".cs5", ".cleo"])) {
        mods.push((name.clone(), vec![name.clone()]));
    }
    let modloader = root.join("modloader");
    if let Ok(entries) = fs::read_dir(&modloader) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_string();
            if name.starts_with('.') || !entry.file_type().map(|t| t.is_dir()).unwrap_or(false) {
                continue;
            }
            let mut files = Vec::new();
            files_under(&entry.path(), 6, &mut files);
            mods.push((name, files));
        }
    }
    mods.retain(|(name, _)| !IGNORED.contains(&name.to_lowercase().as_str()));

    let all_files: Vec<&String> = mods.iter().flat_map(|(_, files)| files.iter()).chain(root_files.iter()).collect();
    let has_file = |pred: &dyn Fn(&str) -> bool| all_files.iter().any(|f| pred(f));

    let mut limit_adjusters = 0;
    for (name, files) in &mods {
        if files.iter().any(|f| f == "main.scm" || f == "script.img") {
            report.broken.push(format!("{}: reemplaza main.scm/script.img (el coop usa los suyos)", name));
            continue;
        }
        let lower = name.to_lowercase();
        if lower.contains("limit adjuster") || lower.contains("limitadjuster") || lower.contains("fastman92") {
            limit_adjusters += 1;
        }
        match verdict(name) {
            Some((Verdict::Broken, why)) => report.broken.push(format!("{}: {}", name, why)),
            Some((Verdict::Risky, why)) => report.risky.push(format!("{}: {}", name, why)),
            Some((Verdict::Ok, _)) => {}
            None => report.unknown += 1,
        }
    }
    if limit_adjusters > 1 {
        report.risky.push("hay mas de un ampliador de limites (fastman92 y Open Limit Adjuster): usar uno solo".to_string());
    }

    // dependencies
    let any_cleo_script = has_file(&|f| has_ext(f, &[".cs", ".cs3", ".cs4", ".cs5"]));
    if any_cleo_script && !has_file(&|f| f == "cleo.asi") {
        report.missing.push("CLEO (cleo.asi): hay scripts CLEO que sin el no corren".to_string());
    }
    if modloader.is_dir() && !has_file(&|f| f == "modloader.asi") {
        report.missing.push("Modloader (modloader.asi): esta la carpeta modloader pero no el plugin".to_string());
    }
    let has_mod = |piece: &str| mods.iter().any(|(name, _)| name.to_lowercase().contains(piece));
    let needs_ola = has_mod("urbanize") || has_mod("proper fixes") || has_mod("project props");
    if needs_ola && limit_adjusters == 0 && !has_file(&|f| f.contains("limitadjuster") || f.contains("limit_adjuster")) {
        report.missing.push("Open Limit Adjuster: lo piden Urbanize / Proper Fixes / Project Props".to_string());
    }
    if has_mod("urbanize") && !has_file(&|f| f == "cleo+.cleo") {
        report.missing.push("CLEO+ (cleo+.cleo): lo pide Urbanize".to_string());
    }
    report
}

#[cfg(test)]
mod tests {
    // the Modloader folder of a friend's game (2026-11-14), to see what the check says about it
    #[test]
    fn friend_setup() {
        let dir = std::env::temp_dir().join("coopsa_mods_test");
        let _ = std::fs::remove_dir_all(&dir);
        let mods = [".data", ".profiles", "_ESSENTIALS", "Animation Fix + M4k3 New Car Animations",
            "Atmosphere Interface Pack", "Beta Mulholland Safehouse", "Change Gear Anim", "CJ 90's Edition V3.0",
            "DeathAnimsMod", "Diego4Fun Insults", "DRUNKMAN walkstyle fix", "Dynamic Weapon Draw Sounds",
            "environmental kills", "Extended Gang Wars", "Fastman92 Limit Adjuster", "Gangs Guns Variations",
            "Gangs With Music", "Handshake Mod", "Improved Streaming", "Ladders Mod", "loadscreens4kdefinitive",
            "mixsets", "More Radar Icons", "Next Gen Weapon Sounds", "NPC Tuning", "Open Limit Adjuster",
            "Original Peds Vary", "Ped Attractor Chair Anim Fix", "Ped Run Fix", "Ped Tweaks", "PedFuncs", "PedSkills",
            "Peds use Vend", "Project Immerse-Yourself V 0.3", "Project2DFX", "Proper Fixes",
            "Random event With Drivers", "Random Events Project", "Rural car repairs",
            "Rusty Brown's Ring Donuts Overhaul", "SA Cutscene Characters for gameplay", "SkyGfx",
            "Slow pedal Mod", "Storyline Enhancement", "Street musicians", "Urbanize", "Weapon Detaills", "Wheel Detach"];
        for m in mods {
            std::fs::create_dir_all(dir.join("modloader").join(m)).unwrap();
        }
        std::fs::write(dir.join("modloader").join("Urbanize").join("urbanize.cs"), b"").unwrap();
        let report = super::scan(dir.to_str().unwrap());
        println!("{}", report.text());
        let _ = std::fs::remove_dir_all(&dir);
    }
}
