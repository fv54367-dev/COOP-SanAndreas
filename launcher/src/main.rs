// CoopSA launcher: one Win32 window in Spanish (dark background, yellow accent).
// No account, no serial: the player types a name and the server (IP, IP:port or connection code), the launcher asks
// that server whether it is open and runs the same version (server.rs), checks the mods, starts
// gta_sa.exe --coop -connect <server> -nick <name> from its own folder and exits (no resident process).
// "ABRIR SERVIDOR" opens SERVIDOR\server.exe for the host; the small top-right button is the update check (update.rs).
#![windows_subsystem = "windows"]

mod launch;
mod mods;
mod server;
mod update;
mod win32;

use std::sync::Mutex;
use win32::*;

// palette (COLORREF = 0x00BBGGRR)
const fn rgb(r: u32, g: u32, b: u32) -> u32 {
    r | (g << 8) | (b << 16)
}
const COL_BG: u32 = rgb(0, 0, 0);
const COL_YELLOW: u32 = rgb(0xF5, 0xB2, 0x18); // acento amarillo #F5B218
const COL_YELLOW_HOT: u32 = rgb(0xFF, 0xC6, 0x2E); // #FFC62E
const COL_YELLOW_DOWN: u32 = rgb(0xD9, 0x9E, 0x0F); // #D99E0F
const COL_TEXT: u32 = rgb(0xF2, 0xF1, 0xEE);
const COL_MUTED: u32 = rgb(0x9C, 0x9A, 0xA4);
const COL_BTN_TEXT: u32 = rgb(0x18, 0x12, 0x02);
const COL_SQUARE: u32 = rgb(0x1C, 0x1B, 0x20);
const COL_SQUARE_HOT: u32 = rgb(0x2C, 0x2A, 0x32);
const COL_GREEN: u32 = rgb(0x4C, 0xC3, 0x8A);
const COL_RED: u32 = rgb(0xE5, 0x5C, 0x5C);

// posted by update::start_check with the state in WPARAM
const WM_UPDATE_STATE: u32 = WM_APP + 1;
// posted by the server status thread (the result is in STATUS)
const WM_SERVER_STATE: u32 = WM_APP + 2;
const UPDATE_TIMER: usize = 1;
const STATUS_TIMER: usize = 2;

const CLIENT_W: i32 = 460;
const CLIENT_H: i32 = 412;
const LOGO_RESOURCE: usize = 101;
const LOGO_SOURCE_W: i32 = 360;
const LOGO_SOURCE_H: i32 = 360;
const LOGO_W: i32 = 110;
const LOGO_H: i32 = LOGO_W;
const VERSION: &str = env!("COOPSA_VERSION"); // shared/config.h, read by build.rs

// the fields (edit controls) and the clickable parts
const ID_NAME: usize = 101;
const ID_ADDRESS: usize = 102;
const ID_PORT: usize = 103;
const PART_NONE: u8 = 0;
const PART_PLAY: u8 = 1;
const PART_SERVER: u8 = 2;
const PART_UPDATE: u8 = 3;

// the last answer of the status thread: (number of the query, what it found)
static STATUS: Mutex<(u32, Option<server::Info>)> = Mutex::new((0, None));

#[derive(PartialEq)]
enum Status {
    Checking,
    Invalid,
    Closed,
    Open(String),
    OtherVersion(String),
}

struct App {
    dpi: i32,
    hover: u8,
    pressed: u8,
    launching: bool,
    upd_state: usize,
    logo: HANDLE,
    hfont_sub: HFONT,
    hfont_btn: HFONT,
    hfont_small: HFONT,
    field_brush: HBRUSH,
    edits: [HWND; 3],
    status: Status,
    query_seq: u32,
    querying: bool,
    dirty: bool,
    last_query: u32,
}

unsafe fn make_font(pt: i32, dpi: i32, bold: bool) -> HFONT {
    let face = wide("Segoe UI");
    CreateFontW(
        -pt * dpi / 72,
        0,
        0,
        0,
        if bold { FW_BOLD } else { FW_NORMAL },
        0,
        0,
        0,
        DEFAULT_CHARSET,
        0,
        0,
        CLEARTYPE_QUALITY,
        FF_SWISS,
        face.as_ptr(),
    )
}

fn rect(left: i32, top: i32, right: i32, bottom: i32) -> RECT {
    RECT { left, top, right, bottom }
}

fn tick() -> u32 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as u32)
        .unwrap_or(0)
}

impl App {
    unsafe fn new() -> Box<App> {
        let hdc = GetDC(0 as HWND);
        let dpi = GetDeviceCaps(hdc, LOGPIXELSX).max(96);
        ReleaseDC(0 as HWND, hdc);
        Box::new(App {
            dpi,
            hover: PART_NONE,
            pressed: PART_NONE,
            launching: false,
            upd_state: update::CHECKING,
            logo: LoadImageW(
                GetModuleHandleW(std::ptr::null()),
                LOGO_RESOURCE as *const u16,
                IMAGE_BITMAP,
                0,
                0,
                LR_CREATEDIBSECTION,
            ),
            hfont_sub: make_font(11, dpi, false),
            hfont_btn: make_font(14, dpi, true),
            hfont_small: make_font(8, dpi, false),
            field_brush: CreateSolidBrush(COL_SQUARE),
            edits: [0 as HWND; 3],
            status: Status::Checking,
            query_seq: 0,
            querying: false,
            dirty: true,
            last_query: 0,
        })
    }

    // logical units (96 dpi) -> device pixels
    fn px(&self, v: i32) -> i32 {
        v * self.dpi / 96
    }

    // the frames of the three fields (logical layout: name on its own row, address and port below)
    fn field_rect(&self, id: usize) -> RECT {
        let p = |v| self.px(v);
        match id {
            ID_NAME => rect(p(48), p(148), p(412), p(180)),
            ID_ADDRESS => rect(p(48), p(208), p(318), p(240)),
            _ => rect(p(330), p(208), p(412), p(240)),
        }
    }

    fn part_rect(&self, part: u8, cr: &RECT) -> RECT {
        match part {
            PART_PLAY => rect(self.px(48), self.px(280), cr.right - self.px(48), self.px(330)),
            PART_SERVER => rect(self.px(48), self.px(342), cr.right - self.px(48), self.px(378)),
            // small square in the top-right corner
            _ => rect(cr.right - self.px(12 + 34), self.px(12), cr.right - self.px(12), self.px(12 + 34)),
        }
    }
}

unsafe fn app_ptr(hwnd: HWND) -> *mut App {
    GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut App
}

fn point_from_lparam(lp: LPARAM) -> POINT {
    POINT {
        x: (lp & 0xFFFF) as i16 as i32,
        y: ((lp >> 16) & 0xFFFF) as i16 as i32,
    }
}

unsafe fn part_at(hwnd: HWND, pt: POINT) -> u8 {
    let app = app_ptr(hwnd);
    if app.is_null() {
        return PART_NONE;
    }
    let mut cr = rect(0, 0, 0, 0);
    GetClientRect(hwnd, &mut cr);
    for part in [PART_PLAY, PART_SERVER, PART_UPDATE] {
        if PtInRect(&(*app).part_rect(part, &cr), pt) != 0 {
            return part;
        }
    }
    PART_NONE
}

unsafe fn message(hwnd: HWND, text: &str, flags: u32) -> i32 {
    MessageBoxW(hwnd, wide(text).as_ptr(), wide("COOP San Andreas").as_ptr(), flags)
}

unsafe fn field_text(app: &App, id: usize) -> String {
    let mut buf = [0u16; 128];
    let n = GetWindowTextW(app.edits[id - ID_NAME], buf.as_mut_ptr(), buf.len() as i32).max(0) as usize;
    String::from_utf16_lossy(&buf[..n]).trim().to_string()
}

unsafe fn set_field(app: &App, id: usize, text: &str) {
    SetWindowTextW(app.edits[id - ID_NAME], wide(text).as_ptr());
}

// asks the typed server in a thread; the window shows the answer when WM_SERVER_STATE comes back
unsafe fn start_status_query(hwnd: HWND) {
    let app = app_ptr(hwnd);
    if app.is_null() || (*app).querying || (*app).launching {
        return;
    }
    let app = &mut *app;
    app.dirty = false;
    app.last_query = tick();
    let target = server::resolve(&field_text(app, ID_ADDRESS), &field_text(app, ID_PORT));
    let Some((addr, _)) = target else {
        if app.status != Status::Invalid {
            app.status = Status::Invalid;
            InvalidateRect(hwnd, std::ptr::null(), FALSE);
        }
        return;
    };
    app.querying = true;
    app.query_seq = app.query_seq.wrapping_add(1);
    let seq = app.query_seq;
    let hwnd_value = hwnd as isize;
    std::thread::spawn(move || {
        let info = server::query(addr, 700);
        if let Ok(mut status) = STATUS.lock() {
            *status = (seq, info);
        }
        unsafe {
            PostMessageW(hwnd_value as HWND, WM_SERVER_STATE, seq as WPARAM, 0);
        }
    });
}

fn status_of(info: &Option<server::Info>) -> Status {
    match info {
        None => Status::Closed,
        Some(info) if info.version != VERSION => Status::OtherVersion(info.version.clone()),
        Some(info) => {
            let name = if info.name.is_empty() { "sin nombre".to_string() } else { info.name.clone() };
            Status::Open(format!("{} ({}/{})", name, info.players, info.max))
        }
    }
}

// arrow = update pending: hand over to ACTUALIZAR.bat and exit; otherwise check again
unsafe fn try_update(hwnd: HWND) {
    let app = app_ptr(hwnd);
    if app.is_null() || (*app).launching || (*app).upd_state == update::CHECKING {
        return;
    }
    let (canal, text) = update::LAST.lock().map(|l| l.clone()).unwrap_or_default();
    if (*app).upd_state == update::PENDING {
        (*app).launching = true;
        match update::run_updater(&canal) {
            Ok(()) => {
                DestroyWindow(hwnd);
            }
            Err(err) => {
                (*app).launching = false;
                message(hwnd, &err, MB_OK | MB_ICONERROR);
            }
        }
        return;
    }
    if (*app).upd_state == update::UNKNOWN && !text.is_empty() {
        message(hwnd, &text, MB_OK | MB_ICONERROR);
    }
    (*app).upd_state = update::CHECKING;
    InvalidateRect(hwnd, std::ptr::null(), FALSE);
    update::start_check(hwnd, WM_UPDATE_STATE);
}

// the host: opens SERVIDOR\server.exe (or finds it already open) and points the fields at it
unsafe fn try_open_server(hwnd: HWND) {
    let app = app_ptr(hwnd);
    if app.is_null() || (*app).launching {
        return;
    }
    let app = &mut *app;
    let port = server::local_port();
    let local = std::net::SocketAddrV4::new(std::net::Ipv4Addr::LOCALHOST, port);
    let already = server::query(local, 400).is_some();
    if !already {
        if let Err(text) = server::start_local_server() {
            message(hwnd, &text, MB_OK | MB_ICONERROR);
            return;
        }
    }
    set_field(app, ID_ADDRESS, "127.0.0.1");
    set_field(app, ID_PORT, &port.to_string());
    app.status = Status::Checking;
    app.dirty = true;
    InvalidateRect(hwnd, std::ptr::null(), FALSE);
    if already {
        message(hwnd, "El servidor ya estaba abierto en esta PC.\n\nTus amigos se conectan con tu IP y el puerto de arriba.",
            MB_OK | MB_ICONINFORMATION);
    }
}

unsafe fn try_launch(hwnd: HWND) {
    let app = app_ptr(hwnd);
    if app.is_null() || (*app).launching {
        return;
    }
    let app = &mut *app;

    let name = server::clean_name(&field_text(app, ID_NAME));
    if name.is_empty() {
        message(hwnd, "Escrib\u{ed} tu nombre de jugador.", MB_OK | MB_ICONWARNING);
        SetFocus(app.edits[0]);
        return;
    }
    let (address, port) = (field_text(app, ID_ADDRESS), field_text(app, ID_PORT));
    let Some((addr, target)) = server::resolve(&address, &port) else {
        message(hwnd, "La direcci\u{f3}n del servidor no es v\u{e1}lida.\n\nEscrib\u{ed} una IP (ej. 192.168.0.10), una IP con puerto (ej. 192.168.0.10:6767) o el c\u{f3}digo de conexi\u{f3}n que te pas\u{f3} el host.",
            MB_OK | MB_ICONWARNING);
        SetFocus(app.edits[1]);
        return;
    };
    set_field(app, ID_NAME, &name);
    server::save_fields(&name, &address, &port);
    app.launching = true;

    // mods that break the coop keep the game from starting; the risky ones and missing dependencies are asked
    let report = mods::scan(&launch::game_dir());
    if report.blocks() {
        let text = format!("{}\n\nSac\u{e1} esos mods de la carpeta del juego para jugar en coop.", report.text());
        message(hwnd, &text, MB_OK | MB_ICONERROR);
        app.launching = false;
        return;
    }
    if !report.is_clean() {
        let text = format!("{}\n\n\u{bf}Jugar igual?", report.text());
        if message(hwnd, &text, MB_YESNO | MB_ICONWARNING) != IDYES {
            app.launching = false;
            return;
        }
    }

    // the server must be open, on the same version and with room
    match server::query(addr, 700) {
        None => {
            let text = format!(
                "No hay ning\u{fa}n servidor abierto en {}.\n\nSi vos sos el host, apret\u{e1} ABRIR SERVIDOR.\nSi no, pedile al host que lo abra y revis\u{e1} la IP y el puerto.\n\n(Un servidor de una versi\u{f3}n vieja tampoco contesta: actualizalo.)",
                target
            );
            message(hwnd, &text, MB_OK | MB_ICONWARNING);
            app.launching = false;
            return;
        }
        Some(info) if info.version != VERSION => {
            let text = format!(
                "El servidor tiene la versi\u{f3}n {} ({}) y vos ten\u{e9}s la {}.\n\nTienen que tener la misma: actualicen los dos con el bot\u{f3}n de arriba a la derecha.",
                info.version, info.build, VERSION
            );
            message(hwnd, &text, MB_OK | MB_ICONWARNING);
            app.launching = false;
            return;
        }
        Some(info) if info.max > 0 && info.players >= info.max => {
            let text = format!("El servidor est\u{e1} lleno ({}/{}).", info.players, info.max);
            message(hwnd, &text, MB_OK | MB_ICONWARNING);
            app.launching = false;
            return;
        }
        Some(_) => {}
    }

    match launch::launch_game(Some((&target, &name))) {
        Ok(()) => {
            DestroyWindow(hwnd);
        }
        Err(text) => {
            app.launching = false;
            message(hwnd, &text, MB_OK | MB_ICONERROR);
        }
    }
}

unsafe fn line(hdc: HDC, points: &[(i32, i32)]) {
    MoveToEx(hdc, points[0].0, points[0].1, std::ptr::null_mut());
    for p in &points[1..] {
        LineTo(hdc, p.0, p.1);
    }
}

unsafe fn fill_round(hdc: HDC, rc: &RECT, color: u32, radius: i32) {
    let brush = CreateSolidBrush(color);
    let old_pen = SelectObject(hdc, GetStockObject(NULL_PEN));
    let old_brush = SelectObject(hdc, brush as HGDIOBJ);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(brush as HGDIOBJ);
}

unsafe fn text_in(hdc: HDC, text: &str, rc: &RECT, color: u32, flags: u32) {
    SetTextColor(hdc, color);
    let mut r = *rc;
    DrawTextW(hdc, wide(text).as_ptr(), -1, &mut r, flags | DT_SINGLELINE | DT_NOPREFIX);
}

// rounded square with a circle; arrow inside when an update is pending, tick when up to date
unsafe fn paint_update(hdc: HDC, app: &App, cr: &RECT) {
    let rc = app.part_rect(PART_UPDATE, cr);
    let hot = app.hover == PART_UPDATE || app.pressed == PART_UPDATE;
    fill_round(hdc, &rc, if hot { COL_SQUARE_HOT } else { COL_SQUARE }, app.px(8));

    let color = match app.upd_state {
        update::PENDING => COL_YELLOW,
        update::UP_TO_DATE => COL_GREEN,
        _ => COL_MUTED,
    };
    let brush = LOGBRUSH { lbStyle: BS_SOLID, lbColor: color, lbHatch: 0 };
    let pen = ExtCreatePen(PS_GEOMETRIC, app.px(2).max(2) as DWORD, &brush, 0, std::ptr::null());
    let old_pen = SelectObject(hdc, pen);
    let old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    let cx = (rc.left + rc.right) / 2;
    let cy = (rc.top + rc.bottom) / 2;
    let r = app.px(10);
    Ellipse(hdc, cx - r, cy - r, cx + r + 1, cy + r + 1);
    match app.upd_state {
        update::PENDING => {
            line(hdc, &[(cx, cy - app.px(5)), (cx, cy + app.px(5))]);
            line(hdc, &[(cx - app.px(4), cy + app.px(1)), (cx, cy + app.px(5)), (cx + app.px(4), cy + app.px(1))]);
        }
        update::UP_TO_DATE => {
            line(hdc, &[(cx - app.px(5), cy), (cx - app.px(1), cy + app.px(4)), (cx + app.px(5), cy - app.px(4))]);
        }
        _ => {}
    }
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);

    // hover hint to the left of the square
    if app.hover == PART_UPDATE {
        let text = match app.upd_state {
            update::CHECKING => "Buscando actualizaciones...".to_string(),
            _ => update::LAST.lock().map(|l| l.1.clone()).unwrap_or_default(),
        };
        SelectObject(hdc, app.hfont_small as HGDIOBJ);
        let hint = rect(0, rc.top, rc.left - app.px(8), rc.bottom);
        text_in(hdc, &text, &hint, COL_MUTED, DT_RIGHT | DT_VCENTER);
    }
}

unsafe fn paint(hwnd: HWND) {
    let app = app_ptr(hwnd);
    if app.is_null() {
        return;
    }
    let mut ps: PAINTSTRUCT = std::mem::zeroed();
    let hdc = BeginPaint(hwnd, &mut ps);
    let mut cr = rect(0, 0, 0, 0);
    GetClientRect(hwnd, &mut cr);
    let app = &*app;

    let background = CreateSolidBrush(COL_BG);
    FillRect(hdc, &cr, background);
    DeleteObject(background as HGDIOBJ);
    SetBkMode(hdc, TRANSPARENT);

    let old_font = SelectObject(hdc, app.hfont_sub as HGDIOBJ);
    if app.logo != 0 {
        let source = CreateCompatibleDC(hdc);
        if source != 0 {
            let previous = SelectObject(source, app.logo);
            let old_mode = SetStretchBltMode(hdc, HALFTONE);
            StretchBlt(
                hdc, (cr.right - app.px(LOGO_W)) / 2, app.px(12),
                app.px(LOGO_W), app.px(LOGO_H), source, 0, 0,
                LOGO_SOURCE_W, LOGO_SOURCE_H, SRCCOPY,
            );
            SetStretchBltMode(hdc, old_mode);
            SelectObject(source, previous);
            DeleteDC(source);
        }
    } else {
        let title = rect(0, app.px(20), cr.right, app.px(120));
        text_in(hdc, "COOP San Andreas", &title, COL_TEXT, DT_CENTER | DT_VCENTER);
    }

    // the fields: label above, rounded frame (the edit control draws itself inside)
    SelectObject(hdc, app.hfont_small as HGDIOBJ);
    for (id, label) in [(ID_NAME, "TU NOMBRE"), (ID_ADDRESS, "IP O C\u{d3}DIGO DEL SERVIDOR"), (ID_PORT, "PUERTO")] {
        let frame = app.field_rect(id);
        let lr = rect(frame.left + app.px(2), frame.top - app.px(17), frame.right, frame.top - app.px(2));
        text_in(hdc, label, &lr, COL_MUTED, 0);
        fill_round(hdc, &frame, COL_SQUARE, app.px(8));
    }

    // what the server said
    let (status, color) = match &app.status {
        Status::Checking => ("Buscando el servidor...".to_string(), COL_MUTED),
        Status::Invalid => ("Direcci\u{f3}n no v\u{e1}lida (IP, IP:puerto o c\u{f3}digo)".to_string(), COL_RED),
        Status::Closed => ("No hay ning\u{fa}n servidor abierto en esa direcci\u{f3}n".to_string(), COL_RED),
        Status::OtherVersion(v) => (format!("Servidor abierto, pero con otra versi\u{f3}n ({})", v), COL_YELLOW),
        Status::Open(text) => (format!("Servidor abierto: {}", text), COL_GREEN),
    };
    let sr = rect(app.px(24), app.px(248), cr.right - app.px(24), app.px(270));
    text_in(hdc, &format!("\u{25cf}  {}", status), &sr, color, DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);

    // big yellow button
    let btn = app.part_rect(PART_PLAY, &cr);
    let color = if app.pressed == PART_PLAY {
        COL_YELLOW_DOWN
    } else if app.hover == PART_PLAY {
        COL_YELLOW_HOT
    } else {
        COL_YELLOW
    };
    fill_round(hdc, &btn, color, app.px(14));
    SelectObject(hdc, app.hfont_btn as HGDIOBJ);
    text_in(hdc, "JUGAR", &btn, COL_BTN_TEXT, DT_CENTER | DT_VCENTER);

    // the host's button: dark, yellow text
    let srv = app.part_rect(PART_SERVER, &cr);
    let hot = app.hover == PART_SERVER || app.pressed == PART_SERVER;
    fill_round(hdc, &srv, if hot { COL_SQUARE_HOT } else { COL_SQUARE }, app.px(12));
    SelectObject(hdc, app.hfont_sub as HGDIOBJ);
    text_in(hdc, "ABRIR SERVIDOR (soy el host)", &srv, COL_YELLOW, DT_CENTER | DT_VCENTER);

    SelectObject(hdc, app.hfont_small as HGDIOBJ);
    let footer = rect(app.px(24), cr.bottom - app.px(28), cr.right - app.px(24), cr.bottom - app.px(8));
    text_in(hdc, "COOP San Andreas", &footer, COL_MUTED, 0);
    text_in(hdc, VERSION, &footer, COL_MUTED, DT_RIGHT);

    paint_update(hdc, app, &cr);

    SelectObject(hdc, old_font);
    EndPaint(hwnd, &ps);
}

unsafe fn create_fields(hwnd: HWND, app: &mut App) {
    let (name, address, port) = server::load_fields();
    let class = wide("EDIT");
    for (i, (id, text, limit, extra)) in
        [(ID_NAME, name, server::MAX_NAME, 0), (ID_ADDRESS, address, 64, 0), (ID_PORT, port, 5, ES_NUMBER)]
            .into_iter()
            .enumerate()
    {
        let frame = app.field_rect(id);
        let pad_x = app.px(10);
        let height = app.px(20);
        let top = frame.top + (frame.bottom - frame.top - height) / 2;
        let edit = CreateWindowExW(
            0,
            class.as_ptr(),
            wide(&text).as_ptr(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extra,
            frame.left + pad_x,
            top,
            frame.right - frame.left - 2 * pad_x,
            height,
            hwnd,
            id as HMENU,
            GetModuleHandleW(std::ptr::null()),
            std::ptr::null(),
        );
        SendMessageW(edit, WM_SETFONT, app.hfont_sub as WPARAM, 1);
        SendMessageW(edit, EM_LIMITTEXT, limit as WPARAM, 0);
        app.edits[i] = edit;
    }
    if field_text(app, ID_NAME).is_empty() {
        SetFocus(app.edits[0]);
    }
}

unsafe extern "system" fn wndproc(hwnd: HWND, msg: UINT, wp: WPARAM, lp: LPARAM) -> LRESULT {
    match msg {
        WM_CREATE => {
            let app = Box::into_raw(App::new());
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, app as LONG_PTR);
            // size and center the window around the wanted client area
            let mut rc = rect(0, 0, CLIENT_W * (*app).dpi / 96, CLIENT_H * (*app).dpi / 96);
            let style = GetWindowLongW(hwnd, GWL_STYLE) as u32;
            AdjustWindowRect(&mut rc, style, FALSE);
            let w = rc.right - rc.left;
            let h = rc.bottom - rc.top;
            let x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
            let y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
            SetWindowPos(hwnd, 0 as HWND, x, y, w, h, SWP_NOZORDER);
            create_fields(hwnd, &mut *app);
            update::start_check(hwnd, WM_UPDATE_STATE);
            // look again while open: a friend's launcher shows a new package without reopening
            SetTimer(hwnd, UPDATE_TIMER, 45_000, std::ptr::null());
            // the server status: right after typing, and every few seconds
            SetTimer(hwnd, STATUS_TIMER, 700, std::ptr::null());
            0
        }
        WM_TIMER if wp == UPDATE_TIMER => {
            let app = app_ptr(hwnd);
            if !app.is_null() && !(*app).launching && (*app).upd_state != update::CHECKING {
                update::start_check(hwnd, WM_UPDATE_STATE);
            }
            0
        }
        WM_TIMER if wp == STATUS_TIMER => {
            let app = app_ptr(hwnd);
            if !app.is_null() && ((*app).dirty || tick().wrapping_sub((*app).last_query) >= 4000) {
                start_status_query(hwnd);
            }
            0
        }
        WM_UPDATE_STATE => {
            let app = app_ptr(hwnd);
            if !app.is_null() {
                (*app).upd_state = wp;
                InvalidateRect(hwnd, std::ptr::null(), FALSE);
            }
            0
        }
        WM_SERVER_STATE => {
            let app = app_ptr(hwnd);
            if !app.is_null() {
                (*app).querying = false;
                // an answer for an address typed before the last change is dropped (the next query is on its way)
                if wp as u32 == (*app).query_seq && !(*app).dirty {
                    if let Ok(status) = STATUS.lock() {
                        (*app).status = status_of(&status.1);
                    }
                    InvalidateRect(hwnd, std::ptr::null(), FALSE);
                }
            }
            0
        }
        WM_COMMAND => {
            let id = wp & 0xFFFF;
            let code = ((wp >> 16) & 0xFFFF) as u32;
            let app = app_ptr(hwnd);
            if id == IDOK {
                try_launch(hwnd);
            } else if !app.is_null() && code == EN_CHANGE && (id == ID_ADDRESS || id == ID_PORT) {
                (*app).dirty = true;
                if (*app).status != Status::Checking {
                    (*app).status = Status::Checking;
                    InvalidateRect(hwnd, std::ptr::null(), FALSE);
                }
            }
            0
        }
        WM_CTLCOLOREDIT => {
            let app = app_ptr(hwnd);
            if app.is_null() {
                return DefWindowProcW(hwnd, msg, wp, lp);
            }
            let hdc = wp as HDC;
            SetTextColor(hdc, COL_TEXT);
            SetBkColor(hdc, COL_SQUARE);
            (*app).field_brush as LRESULT
        }
        WM_DESTROY => {
            let app = app_ptr(hwnd);
            if !app.is_null() {
                if (*app).logo != 0 {
                    DeleteObject((*app).logo);
                }
                DeleteObject((*app).hfont_sub as HGDIOBJ);
                DeleteObject((*app).hfont_btn as HGDIOBJ);
                DeleteObject((*app).hfont_small as HGDIOBJ);
                DeleteObject((*app).field_brush as HGDIOBJ);
                drop(Box::from_raw(app));
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            PostQuitMessage(0);
            0
        }
        WM_PAINT => {
            paint(hwnd);
            0
        }
        WM_ERASEBKGND => 1,
        WM_KEYDOWN => {
            if wp as u32 == VK_RETURN {
                try_launch(hwnd);
            }
            0
        }
        WM_MOUSEMOVE => {
            let over = part_at(hwnd, point_from_lparam(lp));
            let app = app_ptr(hwnd);
            if !app.is_null() {
                if (*app).hover != over {
                    (*app).hover = over;
                    InvalidateRect(hwnd, std::ptr::null(), FALSE);
                }
                if over != PART_NONE {
                    let mut tme = TRACKMOUSEEVENT {
                        cbSize: std::mem::size_of::<TRACKMOUSEEVENT>() as DWORD,
                        dwFlags: TME_LEAVE,
                        hwndTrack: hwnd,
                        dwHoverTime: 0,
                    };
                    TrackMouseEvent(&mut tme);
                }
            }
            0
        }
        WM_MOUSELEAVE => {
            let app = app_ptr(hwnd);
            if !app.is_null() && (*app).hover != PART_NONE {
                (*app).hover = PART_NONE;
                InvalidateRect(hwnd, std::ptr::null(), FALSE);
            }
            0
        }
        WM_LBUTTONDOWN => {
            let part = part_at(hwnd, point_from_lparam(lp));
            let app = app_ptr(hwnd);
            if part != PART_NONE && !app.is_null() {
                (*app).pressed = part;
                InvalidateRect(hwnd, std::ptr::null(), FALSE);
                SetCapture(hwnd);
            }
            0
        }
        WM_LBUTTONUP => {
            let part = part_at(hwnd, point_from_lparam(lp));
            let app = app_ptr(hwnd);
            if !app.is_null() && (*app).pressed != PART_NONE {
                let pressed = (*app).pressed;
                (*app).pressed = PART_NONE;
                InvalidateRect(hwnd, std::ptr::null(), FALSE);
                ReleaseCapture();
                if part == pressed {
                    match part {
                        PART_PLAY => try_launch(hwnd),
                        PART_SERVER => try_open_server(hwnd),
                        _ => try_update(hwnd),
                    }
                }
            }
            0
        }
        WM_SETCURSOR => {
            if (lp & 0xFFFF) as isize == HTCLIENT && wp as HWND == hwnd {
                let mut pt = POINT { x: 0, y: 0 };
                GetCursorPos(&mut pt);
                ScreenToClient(hwnd, &mut pt);
                let hand = part_at(hwnd, pt) != PART_NONE;
                SetCursor(LoadCursorW(0 as HINSTANCE, (if hand { IDC_HAND } else { IDC_ARROW }) as *const u16));
                return 1;
            }
            DefWindowProcW(hwnd, msg, wp, lp)
        }
        _ => DefWindowProcW(hwnd, msg, wp, lp),
    }
}

fn main() {
    unsafe {
        let hinstance = GetModuleHandleW(std::ptr::null());
        let class_name = wide("CoopSA");
        let mut wc: WNDCLASSW = std::mem::zeroed();
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = Some(wndproc);
        wc.hInstance = hinstance;
        wc.hIcon = LoadIconW(hinstance, 1isize as *const u16);
        wc.hCursor = LoadCursorW(0 as HINSTANCE, IDC_ARROW as *const u16);
        wc.hbrBackground = CreateSolidBrush(COL_BG);
        wc.lpszClassName = class_name.as_ptr();
        RegisterClassW(&wc);

        let hwnd = CreateWindowExW(
            0,
            class_name.as_ptr(),
            wide("COOP San Andreas").as_ptr(),
            WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            0 as HWND,
            0 as HMENU,
            hinstance,
            std::ptr::null(),
        );
        if hwnd == 0 {
            std::process::exit(1);
        }
        let small_icon = LoadImageW(
            hinstance, 1usize as *const u16, IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED,
        );
        if small_icon != 0 {
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, small_icon);
        }

        let dark_mode: BOOL = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, (&dark_mode as *const BOOL).cast(), 4);
        for (attribute, color) in [(DWMWA_CAPTION_COLOR, COL_BG), (DWMWA_TEXT_COLOR, COL_TEXT)] {
            DwmSetWindowAttribute(hwnd, attribute, (&color as *const u32).cast(), 4);
        }
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        let mut msg: MSG = std::mem::zeroed();
        while GetMessageW(&mut msg, 0 as HWND, 0, 0) > 0 {
            // Tab between the fields, Enter = JUGAR (WM_COMMAND IDOK)
            if IsDialogMessageW(hwnd, &mut msg) == 0 {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }
}
