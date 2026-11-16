// CoopSA launcher: one Win32 window in Spanish (dark background, yellow accent).
// No account, no serial: it stays idle until "JUGAR" is clicked, starts gta_sa.exe --coop
// from its own folder and exits (no resident process). The only network use is the update
// check behind the small top-right button (update.rs).
#![windows_subsystem = "windows"]

mod launch;
mod mods;
mod update;
mod win32;

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

// posted by update::start_check with the state in WPARAM
const WM_UPDATE_STATE: u32 = WM_APP + 1;
const UPDATE_TIMER: usize = 1;

const CLIENT_W: i32 = 460;
const CLIENT_H: i32 = 352;
const LOGO_RESOURCE: usize = 101;
const LOGO_SOURCE_W: i32 = 360;
const LOGO_SOURCE_H: i32 = 360;
const LOGO_W: i32 = 172;
const LOGO_H: i32 = LOGO_W;
const VERSION: &str = env!("COOPSA_VERSION");  // shared/config.h, read by build.rs

struct App {
    dpi: i32,
    hover: bool,
    pressed: bool,
    launching: bool,
    upd_state: usize,
    upd_hover: bool,
    upd_pressed: bool,
    logo: HANDLE,
    hfont_sub: HFONT,
    hfont_btn: HFONT,
    hfont_small: HFONT,
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

impl App {
    unsafe fn new() -> Box<App> {
        let hdc = GetDC(0 as HWND);
        let dpi = GetDeviceCaps(hdc, LOGPIXELSX).max(96);
        ReleaseDC(0 as HWND, hdc);
        Box::new(App {
            dpi,
            hover: false,
            pressed: false,
            launching: false,
            upd_state: update::CHECKING,
            upd_hover: false,
            upd_pressed: false,
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
        })
    }

    // logical units (96 dpi) -> device pixels
    fn px(&self, v: i32) -> i32 {
        v * self.dpi / 96
    }

    fn button_rect(&self, cr: &RECT) -> RECT {
        RECT {
            left: self.px(48),
            top: self.px(220),
            right: cr.right - self.px(48),
            bottom: self.px(272),
        }
    }

    // small square in the top-right corner
    fn update_rect(&self, cr: &RECT) -> RECT {
        RECT {
            left: cr.right - self.px(12 + 34),
            top: self.px(12),
            right: cr.right - self.px(12),
            bottom: self.px(12 + 34),
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

unsafe fn pt_in_btn(hwnd: HWND, pt: POINT) -> bool {
    let app = app_ptr(hwnd);
    if app.is_null() {
        return false;
    }
    let mut cr = RECT { left: 0, top: 0, right: 0, bottom: 0 };
    GetClientRect(hwnd, &mut cr);
    let rc = (*app).button_rect(&cr);
    PtInRect(&rc, pt) != 0
}

unsafe fn pt_in_update(hwnd: HWND, pt: POINT) -> bool {
    let app = app_ptr(hwnd);
    if app.is_null() {
        return false;
    }
    let mut cr = RECT { left: 0, top: 0, right: 0, bottom: 0 };
    GetClientRect(hwnd, &mut cr);
    let rc = (*app).update_rect(&cr);
    PtInRect(&rc, pt) != 0
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
                MessageBoxW(hwnd, wide(&err).as_ptr(), wide("CoopSA").as_ptr(), MB_OK | MB_ICONERROR);
            }
        }
        return;
    }
    if (*app).upd_state == update::UNKNOWN && !text.is_empty() {
        MessageBoxW(hwnd, wide(&text).as_ptr(), wide("CoopSA").as_ptr(), MB_OK | MB_ICONERROR);
    }
    (*app).upd_state = update::CHECKING;
    InvalidateRect(hwnd, std::ptr::null(), FALSE);
    update::start_check(hwnd, WM_UPDATE_STATE);
}

unsafe fn line(hdc: HDC, points: &[(i32, i32)]) {
    MoveToEx(hdc, points[0].0, points[0].1, std::ptr::null_mut());
    for p in &points[1..] {
        LineTo(hdc, p.0, p.1);
    }
}

// rounded square with a circle; arrow inside when an update is pending, tick when up to date
unsafe fn paint_update(hdc: HDC, app: &App, cr: &RECT) {
    let rc = app.update_rect(cr);
    let fill = CreateSolidBrush(if app.upd_hover || app.upd_pressed { COL_SQUARE_HOT } else { COL_SQUARE });
    let old_pen = SelectObject(hdc, GetStockObject(NULL_PEN));
    let old_brush = SelectObject(hdc, fill as HGDIOBJ);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, app.px(8), app.px(8));

    let color = match app.upd_state {
        update::PENDING => COL_YELLOW,
        update::UP_TO_DATE => COL_GREEN,
        _ => COL_MUTED,
    };
    let brush = LOGBRUSH { lbStyle: BS_SOLID, lbColor: color, lbHatch: 0 };
    let pen = ExtCreatePen(PS_GEOMETRIC, app.px(2).max(2) as DWORD, &brush, 0, std::ptr::null());
    SelectObject(hdc, pen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
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
    DeleteObject(fill as HGDIOBJ);

    // hover hint to the left of the square
    if app.upd_hover {
        let text = match app.upd_state {
            update::CHECKING => "Buscando actualizaciones...".to_string(),
            _ => update::LAST.lock().map(|l| l.1.clone()).unwrap_or_default(),
        };
        SelectObject(hdc, app.hfont_small as HGDIOBJ);
        SetTextColor(hdc, COL_MUTED);
        let mut hint = RECT { left: 0, top: rc.top, right: rc.left - app.px(8), bottom: rc.bottom };
        DrawTextW(hdc, wide(&text).as_ptr(), -1, &mut hint, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

unsafe fn try_launch(hwnd: HWND) {
    let app = app_ptr(hwnd);
    if app.is_null() || (*app).launching {
        return;
    }
    (*app).launching = true;

    // mods that can break the coop or are missing what they need: told before starting, the player decides
    let report = mods::scan(&launch::game_dir());
    if !report.is_clean() {
        let text = format!("{}\n\n\u{bf}Jugar igual?", report.text());
        let answer = MessageBoxW(hwnd, wide(&text).as_ptr(), wide("CoopSA - revision de mods").as_ptr(),
            MB_YESNO | MB_ICONWARNING);
        if answer != IDYES {
            (*app).launching = false;
            return;
        }
    }

    match launch::launch_game() {
        Ok(()) => {
            DestroyWindow(hwnd);
        }
        Err(text) => {
            (*app).launching = false;
            MessageBoxW(hwnd, wide(&text).as_ptr(), wide("CoopSA").as_ptr(), MB_OK | MB_ICONERROR);
        }
    }
}

unsafe fn paint(hwnd: HWND) {
    let app = app_ptr(hwnd);
    if app.is_null() {
        return;
    }
    let mut ps: PAINTSTRUCT = std::mem::zeroed();
    let hdc = BeginPaint(hwnd, &mut ps);
    let mut cr = RECT { left: 0, top: 0, right: 0, bottom: 0 };
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
                hdc, (cr.right - app.px(LOGO_W)) / 2, app.px(10),
                app.px(LOGO_W), app.px(LOGO_H), source, 0, 0,
                LOGO_SOURCE_W, LOGO_SOURCE_H, SRCCOPY,
            );
            SetStretchBltMode(hdc, old_mode);
            SelectObject(source, previous);
            DeleteDC(source);
        }
    } else {
        SetTextColor(hdc, COL_TEXT);
        let mut title = RECT { left: 0, top: app.px(25), right: cr.right, bottom: app.px(145) };
        DrawTextW(hdc, wide("CoopSA").as_ptr(), -1, &mut title, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // big yellow button
    let btn = (*app).button_rect(&cr);
    let color = if (*app).pressed {
        COL_YELLOW_DOWN
    } else if (*app).hover {
        COL_YELLOW_HOT
    } else {
        COL_YELLOW
    };
    let hold = CreateSolidBrush(color);
    let open = SelectObject(hdc, GetStockObject(NULL_PEN));
    let old_brush = SelectObject(hdc, hold as HGDIOBJ);
    RoundRect(hdc, btn.left, btn.top, btn.right, btn.bottom, app.px(14), app.px(14));
    SelectObject(hdc, old_brush);
    SelectObject(hdc, open);
    DeleteObject(hold as HGDIOBJ);
    SelectObject(hdc, (*app).hfont_btn as HGDIOBJ);
    SetTextColor(hdc, COL_BTN_TEXT);
    let mut rb = btn;
    DrawTextW(hdc, wide("JUGAR GTA SAN ANDREAS").as_ptr(), -1, &mut rb, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(hdc, app.hfont_small as HGDIOBJ);
    SetTextColor(hdc, COL_MUTED);
    let footer = RECT { left: app.px(24), top: cr.bottom - app.px(28), right: cr.right - app.px(24), bottom: cr.bottom - app.px(8) };
    let mut credit = footer;
    DrawTextW(hdc, wide("COOPSA 2026").as_ptr(), -1, &mut credit, DT_SINGLELINE | DT_NOPREFIX);
    let mut version = footer;
    DrawTextW(hdc, wide(VERSION).as_ptr(), -1, &mut version, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);

    paint_update(hdc, app, &cr);

    SelectObject(hdc, old_font);
    EndPaint(hwnd, &ps);
}

unsafe extern "system" fn wndproc(hwnd: HWND, msg: UINT, wp: WPARAM, lp: LPARAM) -> LRESULT {
    match msg {
        WM_CREATE => {
            let app = Box::into_raw(App::new());
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, app as LONG_PTR);
            // size and center the window around the wanted client area
            let mut rc = RECT {
                left: 0,
                top: 0,
                right: CLIENT_W * (*app).dpi / 96,
                bottom: CLIENT_H * (*app).dpi / 96,
            };
            let style = GetWindowLongW(hwnd, GWL_STYLE) as u32;
            AdjustWindowRect(&mut rc, style, FALSE);
            let w = rc.right - rc.left;
            let h = rc.bottom - rc.top;
            let x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
            let y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
            SetWindowPos(hwnd, 0 as HWND, x, y, w, h, SWP_NOZORDER);
            update::start_check(hwnd, WM_UPDATE_STATE);
            // look again while open: a friend's launcher shows a new package without reopening
            SetTimer(hwnd, UPDATE_TIMER, 45_000, std::ptr::null());
            0
        }
        WM_TIMER if wp == UPDATE_TIMER => {
            let app = app_ptr(hwnd);
            if !app.is_null() && !(*app).launching && (*app).upd_state != update::CHECKING {
                update::start_check(hwnd, WM_UPDATE_STATE);
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
        WM_DESTROY => {
            let app = app_ptr(hwnd);
            if !app.is_null() {
                if (*app).logo != 0 {
                    DeleteObject((*app).logo);
                }
                DeleteObject((*app).hfont_sub as HGDIOBJ);
                DeleteObject((*app).hfont_btn as HGDIOBJ);
                DeleteObject((*app).hfont_small as HGDIOBJ);
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
            let vk = wp as u32;
            if vk == VK_RETURN || vk == VK_SPACE {
                try_launch(hwnd);
            }
            0
        }
        WM_MOUSEMOVE => {
            let pt = point_from_lparam(lp);
            let over = pt_in_btn(hwnd, pt);
            let over_upd = pt_in_update(hwnd, pt);
            let app = app_ptr(hwnd);
            if !app.is_null() {
                if (*app).hover != over || (*app).upd_hover != over_upd {
                    (*app).hover = over;
                    (*app).upd_hover = over_upd;
                    InvalidateRect(hwnd, std::ptr::null(), FALSE);
                }
                if over || over_upd {
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
            if !app.is_null() && ((*app).hover || (*app).upd_hover) {
                (*app).hover = false;
                (*app).upd_hover = false;
                InvalidateRect(hwnd, std::ptr::null(), FALSE);
            }
            0
        }
        WM_LBUTTONDOWN => {
            let pt = point_from_lparam(lp);
            if pt_in_btn(hwnd, pt) {
                let app = app_ptr(hwnd);
                if !app.is_null() {
                    (*app).pressed = true;
                    InvalidateRect(hwnd, std::ptr::null(), FALSE);
                }
                SetCapture(hwnd);
            } else if pt_in_update(hwnd, pt) {
                let app = app_ptr(hwnd);
                if !app.is_null() {
                    (*app).upd_pressed = true;
                    InvalidateRect(hwnd, std::ptr::null(), FALSE);
                }
                SetCapture(hwnd);
            }
            0
        }
        WM_LBUTTONUP => {
            let pt = point_from_lparam(lp);
            let app = app_ptr(hwnd);
            if !app.is_null() && (*app).pressed {
                (*app).pressed = false;
                if pt_in_btn(hwnd, pt) {
                    try_launch(hwnd);
                }
                InvalidateRect(hwnd, std::ptr::null(), FALSE);
            } else if !app.is_null() && (*app).upd_pressed {
                (*app).upd_pressed = false;
                if pt_in_update(hwnd, pt) {
                    try_update(hwnd);
                }
                InvalidateRect(hwnd, std::ptr::null(), FALSE);
            }
            ReleaseCapture();
            0
        }
        WM_SETCURSOR => {
            if (lp & 0xFFFF) as isize == HTCLIENT {
                let mut pt = POINT { x: 0, y: 0 };
                GetCursorPos(&mut pt);
                ScreenToClient(hwnd, &mut pt);
                let hand = pt_in_btn(hwnd, pt) || pt_in_update(hwnd, pt);
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
            wide("CoopSA").as_ptr(),
            WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
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
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}
