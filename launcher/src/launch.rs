// Starts gta_sa.exe with --coop from the launcher's own folder (the GTA install).
// No serial key, no account, no network: one CreateProcess and the launcher exits.
use crate::win32::*;

fn error_text(err: u32, game_dir: &str) -> String {
    match err {
        ERROR_FILE_NOT_FOUND | ERROR_PATH_NOT_FOUND => {
            let steam = format!("{}\\gta-sa.exe", game_dir);
            let steam_exists = unsafe { GetFileAttributesW(wide(&steam).as_ptr()) != INVALID_FILE_ATTRIBUTES };
            if steam_exists {
                "Se detect\u{f3} la versi\u{f3}n Steam de GTA San Andreas (gta-sa.exe).\n\nEl mod requiere GTA:SA v1.0 US HOODLUM: us\u{e1} un downgrader para convertir tu juego.".to_string()
            } else {
                "No se encontr\u{f3} gta_sa.exe.\n\nPon\u{e9} CoopSA.exe en la carpeta de instalaci\u{f3}n de GTA San Andreas y volv\u{e9} a intentarlo.".to_string()
            }
        }
        ERROR_ACCESS_DENIED | ERROR_ELEVATION_REQUIRED => {
            "Windows bloque\u{f3} el arranque de GTA San Andreas.\n\nProb\u{e1} ejecutar CoopSA.exe como administrador.".to_string()
        }
        ERROR_BAD_EXE_FORMAT => {
            "gta_sa.exe parece corrupto o inv\u{e1}lido.\n\nNecesit\u{e1}s un gta_sa.exe v1.0 US HOODLUM v\u{e1}lido.".to_string()
        }
        ERROR_SHARING_VIOLATION | ERROR_LOCK_VIOLATION => {
            "GTA San Andreas ya est\u{e1} en ejecuci\u{f3}n.\n\nCerr\u{e1} las instancias abiertas del juego y prob\u{e1} de nuevo.".to_string()
        }
        _ => format!("No se pudo iniciar GTA San Andreas (c\u{f3}digo de error {}).", err),
    }
}

// the launcher lives inside the GTA folder
pub fn game_dir() -> String {
    let mut path = [0u16; 1024];
    let n = unsafe { GetModuleFileNameW(0 as HMODULE, path.as_mut_ptr(), 1024) } as usize;
    let exe = String::from_utf16_lossy(&path[..n]);
    match exe.rfind(['\\', '/']) {
        Some(i) => exe[..i].to_string(),
        None => ".".to_string(),
    }
}

// `connect` = the server for -connect (ip:port or a connection code) and the player's name; `autoload` = "new" or
// "slotN" (1-8): the game skips its menus (CLaunchManager)
pub fn launch_game(connect: Option<(&str, &str)>, autoload: Option<&str>) -> Result<(), String> {
    unsafe {
        // start the game from the GTA folder
        let dir = game_dir();
        let dir_w = wide(&dir);
        let mut app = dir.clone();
        app.push_str("\\gta_sa.exe");
        let app_w = wide(&app);
        let mut cmd = wide(&match connect {
            Some((address, name)) => match autoload {
                Some(load) => format!("gta_sa.exe --coop -connect {} -nick {} -autoload {}", address, name, load),
                None => format!("gta_sa.exe --coop -connect {} -nick {}", address, name),
            },
            None => "gta_sa.exe --coop".to_string(),
        });

        let mut si: STARTUPINFOW = std::mem::zeroed();
        si.cb = std::mem::size_of::<STARTUPINFOW>() as DWORD;
        let mut pi: PROCESS_INFORMATION = std::mem::zeroed();

        let ok = CreateProcessW(
            app_w.as_ptr(),
            cmd.as_mut_ptr(),
            std::ptr::null_mut(),
            std::ptr::null_mut(),
            FALSE,
            0,
            std::ptr::null(),
            dir_w.as_ptr(),
            &mut si,
            &mut pi,
        );
        if ok == FALSE {
            let err = GetLastError();
            return Err(error_text(err, &dir));
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        Ok(())
    }
}
