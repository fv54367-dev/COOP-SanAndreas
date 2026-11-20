// Minimal hand-written Win32 FFI: everything the launcher needs, no external crates.
#![allow(non_snake_case, non_camel_case_types, dead_code)]

use std::ffi::c_void;

pub type HWND = isize;
pub type HINSTANCE = isize;
pub type HMODULE = isize;
pub type HBRUSH = isize;
pub type HICON = isize;
pub type HCURSOR = isize;
pub type HDC = isize;
pub type HFONT = isize;
pub type HGDIOBJ = isize;
pub type HMENU = isize;
pub type HANDLE = isize;
pub type LRESULT = isize;
pub type WPARAM = usize;
pub type LPARAM = isize;
pub type ATOM = u16;
pub type BOOL = i32;
pub type UINT = u32;
pub type DWORD = u32;
pub type WORD = u16;
pub type COLORREF = u32;
pub type LONG_PTR = isize;

pub type WNDPROC = Option<unsafe extern "system" fn(HWND, UINT, WPARAM, LPARAM) -> LRESULT>;

#[repr(C)]
pub struct WNDCLASSW {
    pub style: UINT,
    pub lpfnWndProc: WNDPROC,
    pub cbClsExtra: i32,
    pub cbWndExtra: i32,
    pub hInstance: HINSTANCE,
    pub hIcon: HICON,
    pub hCursor: HCURSOR,
    pub hbrBackground: HBRUSH,
    pub lpszMenuName: *const u16,
    pub lpszClassName: *const u16,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct POINT {
    pub x: i32,
    pub y: i32,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct RECT {
    pub left: i32,
    pub top: i32,
    pub right: i32,
    pub bottom: i32,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct SIZE {
    pub cx: i32,
    pub cy: i32,
}

#[repr(C)]
pub struct MSG {
    pub hwnd: HWND,
    pub message: UINT,
    pub wParam: WPARAM,
    pub lParam: LPARAM,
    pub time: DWORD,
    pub pt: POINT,
}

#[repr(C)]
pub struct PAINTSTRUCT {
    pub hdc: HDC,
    pub fErase: BOOL,
    pub rcPaint: RECT,
    pub fRestore: BOOL,
    pub fUpdate: BOOL,
    pub rgbReserved: [u8; 32],
}

#[repr(C)]
pub struct TRACKMOUSEEVENT {
    pub cbSize: DWORD,
    pub dwFlags: DWORD,
    pub hwndTrack: HWND,
    pub dwHoverTime: DWORD,
}

#[repr(C)]
pub struct STARTUPINFOW {
    pub cb: DWORD,
    pub lpReserved: *mut u16,
    pub lpDesktop: *mut u16,
    pub lpTitle: *mut u16,
    pub dwX: DWORD,
    pub dwY: DWORD,
    pub dwXSize: DWORD,
    pub dwYSize: DWORD,
    pub dwXCountChars: DWORD,
    pub dwYCountChars: DWORD,
    pub dwFillAttribute: DWORD,
    pub dwFlags: DWORD,
    pub wShowWindow: WORD,
    pub cbReserved2: WORD,
    pub lpReserved2: *mut u8,
    pub hStdInput: HANDLE,
    pub hStdOutput: HANDLE,
    pub hStdError: HANDLE,
}

#[repr(C)]
pub struct PROCESS_INFORMATION {
    pub hProcess: HANDLE,
    pub hThread: HANDLE,
    pub dwProcessId: DWORD,
    pub dwThreadId: DWORD,
}

#[repr(C)]
#[derive(Default)]
pub struct SYSTEMTIME {
    pub wYear: WORD,
    pub wMonth: WORD,
    pub wDayOfWeek: WORD,
    pub wDay: WORD,
    pub wHour: WORD,
    pub wMinute: WORD,
    pub wSecond: WORD,
    pub wMilliseconds: WORD,
}

// constants
pub const FALSE: BOOL = 0;
pub const TRUE: BOOL = 1;
pub const CW_USEDEFAULT: i32 = 0x80000000u32 as i32;
pub const CS_HREDRAW: u32 = 0x0002;
pub const CS_VREDRAW: u32 = 0x0001;
pub const WS_CAPTION: u32 = 0x00C00000;
pub const WS_SYSMENU: u32 = 0x00080000;
pub const WS_MINIMIZEBOX: u32 = 0x00020000;
pub const WS_CLIPCHILDREN: u32 = 0x02000000;
pub const WM_CREATE: u32 = 0x0001;
pub const WM_DESTROY: u32 = 0x0002;
pub const WM_CLOSE: u32 = 0x0010;
pub const WM_SIZE: u32 = 0x0005;
pub const WM_SETCURSOR: u32 = 0x0020;
pub const WM_PAINT: u32 = 0x000F;
pub const WM_ERASEBKGND: u32 = 0x0014;
pub const WM_KEYDOWN: u32 = 0x0100;
pub const WM_MOUSEMOVE: u32 = 0x0200;
pub const WM_LBUTTONDOWN: u32 = 0x0201;
pub const WM_LBUTTONUP: u32 = 0x0202;
pub const WM_MOUSELEAVE: u32 = 0x02A2;
pub const SW_SHOW: i32 = 5;
pub const IDC_ARROW: usize = 32512;
pub const IDC_HAND: usize = 32649;
pub const VK_RETURN: u32 = 0x0D;
pub const VK_SPACE: u32 = 0x20;
pub const VK_ESCAPE: u32 = 0x1B;
pub const MB_OK: u32 = 0x0;
pub const MB_ICONERROR: u32 = 0x10;
pub const MB_YESNO: u32 = 0x4;
pub const MB_ICONWARNING: u32 = 0x30;
pub const IDYES: i32 = 6;
pub const HTCLIENT: isize = 1;
pub const TME_LEAVE: u32 = 0x2;
pub const DT_CENTER: u32 = 0x1;
pub const DT_RIGHT: u32 = 0x2;
pub const DT_SINGLELINE: u32 = 0x20;
pub const DT_VCENTER: u32 = 0x4;
pub const DT_NOCLIP: u32 = 0x100;
pub const DT_NOPREFIX: u32 = 0x800;
pub const TRANSPARENT: i32 = 1;
pub const NULL_PEN: i32 = 8;
pub const IMAGE_BITMAP: UINT = 0;
pub const IMAGE_ICON: UINT = 1;
pub const ICON_SMALL: WPARAM = 0;
pub const LR_SHARED: UINT = 0x8000;
pub const WM_SETICON: UINT = 0x0080;
pub const SM_CXSMICON: i32 = 49;
pub const SM_CYSMICON: i32 = 50;
pub const LR_CREATEDIBSECTION: UINT = 0x2000;
pub const HALFTONE: i32 = 4;
pub const SRCCOPY: DWORD = 0x00CC0020;
pub const DWMWA_USE_IMMERSIVE_DARK_MODE: DWORD = 20;
pub const DWMWA_CAPTION_COLOR: DWORD = 35;
pub const DWMWA_TEXT_COLOR: DWORD = 36;
pub const FW_BOLD: i32 = 700;
pub const FW_NORMAL: i32 = 400;
pub const DEFAULT_CHARSET: u32 = 1;
pub const CLEARTYPE_QUALITY: u32 = 5;
pub const FF_SWISS: u32 = 0x20;
pub const SM_CXSCREEN: i32 = 0;
pub const SM_CYSCREEN: i32 = 1;
pub const GWL_STYLE: i32 = -16;
pub const GWLP_HINSTANCE: i32 = -6;
pub const GWLP_USERDATA: i32 = -21;
pub const SWP_NOZORDER: u32 = 0x4;
pub const LOGPIXELSX: u32 = 88;
pub const INVALID_FILE_ATTRIBUTES: DWORD = u32::MAX;
pub const ERROR_FILE_NOT_FOUND: u32 = 2;
pub const ERROR_PATH_NOT_FOUND: u32 = 3;
pub const ERROR_SHARING_VIOLATION: u32 = 32;
pub const ERROR_LOCK_VIOLATION: u32 = 33;
pub const ERROR_ACCESS_DENIED: u32 = 5;
pub const ERROR_BAD_EXE_FORMAT: u32 = 193;
pub const ERROR_ELEVATION_REQUIRED: u32 = 740;
pub const WM_APP: u32 = 0x8000;
pub const WM_TIMER: u32 = 0x0113;
pub const NULL_BRUSH: i32 = 5;
// edit boxes (launcher fields)
pub const WS_CHILD: u32 = 0x4000_0000;
pub const WS_VISIBLE: u32 = 0x1000_0000;
pub const WS_TABSTOP: u32 = 0x0001_0000;
pub const ES_AUTOHSCROLL: u32 = 0x0080;
pub const ES_NUMBER: u32 = 0x2000;
pub const WM_SETFONT: u32 = 0x0030;
pub const WM_COMMAND: u32 = 0x0111;
pub const WM_CTLCOLOREDIT: u32 = 0x0133;
pub const EM_LIMITTEXT: u32 = 0x00C5;
pub const EN_CHANGE: u32 = 0x0300;
pub const IDOK: usize = 1;
pub const MB_ICONINFORMATION: u32 = 0x40;
pub const DT_END_ELLIPSIS: u32 = 0x8000;
pub const DT_PATH_ELLIPSIS: u32 = 0x4000;
pub const SW_HIDE: i32 = 0;
pub const CSIDL_PERSONAL: i32 = 5;
pub const PS_GEOMETRIC: DWORD = 0x0001_0000;
pub const BS_SOLID: UINT = 0;

#[repr(C)]
pub struct LOGBRUSH {
    pub lbStyle: UINT,
    pub lbColor: COLORREF,
    pub lbHatch: usize,
}

#[link(name = "user32")]
extern "system" {
    pub fn RegisterClassW(lpWndClass: *const WNDCLASSW) -> ATOM;
    pub fn CreateWindowExW(
        dwExStyle: DWORD,
        lpClassName: *const u16,
        lpWindowName: *const u16,
        dwStyle: u32,
        x: i32,
        y: i32,
        nWidth: i32,
        nHeight: i32,
        hWndParent: HWND,
        hMenu: HMENU,
        hInstance: HINSTANCE,
        lpParam: *const c_void,
    ) -> HWND;
    pub fn SendMessageW(hwnd: HWND, message: UINT, wparam: WPARAM, lparam: LPARAM) -> LRESULT;
    pub fn ShowWindow(hWnd: HWND, nCmdShow: i32) -> BOOL;
    pub fn UpdateWindow(hWnd: HWND) -> BOOL;
    pub fn GetMessageW(lpMsg: *mut MSG, hWnd: HWND, wMsgFilterMin: UINT, wMsgFilterMax: UINT) -> i32;
    pub fn TranslateMessage(lpMsg: *const MSG) -> BOOL;
    pub fn DispatchMessageW(lpMsg: *const MSG) -> LRESULT;
    pub fn PostQuitMessage(nExitCode: i32);
    pub fn DefWindowProcW(hWnd: HWND, Msg: UINT, wParam: WPARAM, lParam: LPARAM) -> LRESULT;
    pub fn DestroyWindow(hWnd: HWND) -> BOOL;
    pub fn MessageBoxW(hWnd: HWND, lpText: *const u16, lpCaption: *const u16, uType: u32) -> i32;
    pub fn InvalidateRect(hWnd: HWND, lpRect: *const RECT, bErase: BOOL) -> BOOL;
    pub fn GetClientRect(hWnd: HWND, lpRect: *mut RECT) -> BOOL;
    pub fn BeginPaint(hWnd: HWND, lpPaint: *mut PAINTSTRUCT) -> HDC;
    pub fn EndPaint(hWnd: HWND, lpPaint: *const PAINTSTRUCT) -> BOOL;
    pub fn LoadCursorW(hInstance: HINSTANCE, lpCursorName: *const u16) -> HCURSOR;
    pub fn LoadImageW(instance: HINSTANCE, name: *const u16, image_type: UINT, cx: i32, cy: i32, flags: UINT) -> HANDLE;
    pub fn LoadIconW(hInstance: HINSTANCE, lpIconName: *const u16) -> HICON;
    pub fn SetCursor(hCursor: HCURSOR) -> HCURSOR;
    pub fn SetCapture(hWnd: HWND) -> HWND;
    pub fn ReleaseCapture() -> BOOL;
    pub fn TrackMouseEvent(lpEventTrack: *mut TRACKMOUSEEVENT) -> BOOL;
    pub fn PtInRect(lprc: *const RECT, pt: POINT) -> BOOL;
    pub fn GetCursorPos(lpPoint: *mut POINT) -> BOOL;
    pub fn ScreenToClient(hWnd: HWND, lpPoint: *mut POINT) -> BOOL;
    pub fn GetSystemMetrics(nIndex: i32) -> i32;
    pub fn GetWindowLongW(hWnd: HWND, nIndex: i32) -> i32;
    pub fn GetWindowLongPtrW(hWnd: HWND, nIndex: i32) -> LONG_PTR;
    pub fn SetWindowLongPtrW(hWnd: HWND, nIndex: i32, dwNewLong: LONG_PTR) -> LONG_PTR;
    pub fn SetWindowPos(
        hWnd: HWND,
        hWndInsertAfter: HWND,
        x: i32,
        y: i32,
        cx: i32,
        cy: i32,
        uFlags: UINT,
    ) -> BOOL;
    pub fn AdjustWindowRect(lpRect: *mut RECT, dwStyle: u32, bMenu: BOOL) -> BOOL;
    pub fn GetDC(hWnd: HWND) -> HDC;
    pub fn ReleaseDC(hWnd: HWND, hDC: HDC) -> i32;
    pub fn PostMessageW(hWnd: HWND, Msg: UINT, wParam: WPARAM, lParam: LPARAM) -> BOOL;
    pub fn SetTimer(hWnd: HWND, nIDEvent: usize, uElapse: UINT, lpTimerFunc: *const core::ffi::c_void) -> usize;
    pub fn GetWindowTextW(hWnd: HWND, lpString: *mut u16, nMaxCount: i32) -> i32;
    pub fn SetWindowTextW(hWnd: HWND, lpString: *const u16) -> BOOL;
    pub fn SetFocus(hWnd: HWND) -> HWND;
    pub fn IsDialogMessageW(hDlg: HWND, lpMsg: *mut MSG) -> BOOL;
}

#[link(name = "shell32")]
extern "system" {
    pub fn SHGetFolderPathW(hwnd: HWND, csidl: i32, hToken: HANDLE, dwFlags: DWORD, pszPath: *mut u16) -> i32;
    pub fn SHBrowseForFolderW(lpbi: *mut BROWSEINFOW) -> *mut c_void;
    pub fn SHGetPathFromIDListW(pidl: *const c_void, pszPath: *mut u16) -> BOOL;
}

#[link(name = "ole32")]
extern "system" {
    pub fn CoInitializeEx(reserved: *mut c_void, coinit: DWORD) -> i32;
    pub fn CoTaskMemFree(pv: *mut c_void);
}

pub const COINIT_APARTMENTTHREADED: DWORD = 2;
pub const BIF_RETURNONLYFSDIRS: UINT = 0x1;
pub const BIF_NEWDIALOGSTYLE: UINT = 0x40;
pub const BFFM_INITIALIZED: UINT = 1;
pub const BFFM_SETSELECTIONW: UINT = 0x400 + 103;

pub type BFFCALLBACK = Option<unsafe extern "system" fn(HWND, UINT, LPARAM, LPARAM) -> i32>;

#[repr(C)]
pub struct BROWSEINFOW {
    pub hwndOwner: HWND,
    pub pidlRoot: *const c_void,
    pub pszDisplayName: *mut u16,
    pub lpszTitle: *const u16,
    pub ulFlags: UINT,
    pub lpfn: BFFCALLBACK,
    pub lParam: LPARAM,
    pub iImage: i32,
}

#[link(name = "dwmapi")]
extern "system" {
    pub fn DwmSetWindowAttribute(hwnd: HWND, attribute: DWORD, value: *const c_void, size: DWORD) -> i32;
}

#[link(name = "gdi32")]
extern "system" {
    pub fn CreateCompatibleDC(hdc: HDC) -> HDC;
    pub fn DeleteDC(hdc: HDC) -> BOOL;
    pub fn SetStretchBltMode(hdc: HDC, mode: i32) -> i32;
    pub fn StretchBlt(
        dest: HDC, x: i32, y: i32, width: i32, height: i32,
        source: HDC, source_x: i32, source_y: i32,
        source_width: i32, source_height: i32, operation: DWORD,
    ) -> BOOL;
    pub fn CreateSolidBrush(color: COLORREF) -> HBRUSH;
    pub fn CreateFontW(
        cHeight: i32,
        cWidth: i32,
        cEscapement: i32,
        cOrientation: i32,
        cWeight: i32,
        bItalic: DWORD,
        bUnderline: DWORD,
        bStrikeOut: DWORD,
        iCharSet: DWORD,
        iOutPrecision: DWORD,
        iClipPrecision: DWORD,
        iQuality: DWORD,
        iPitchAndFamily: DWORD,
        pszFaceName: *const u16,
    ) -> HFONT;
    pub fn DeleteObject(ho: HGDIOBJ) -> BOOL;
    pub fn SelectObject(hdc: HDC, hgdiobj: HGDIOBJ) -> HGDIOBJ;
    pub fn SetBkMode(hdc: HDC, mode: i32) -> i32;
    pub fn SetTextColor(hdc: HDC, color: COLORREF) -> COLORREF;
    pub fn SetBkColor(hdc: HDC, color: COLORREF) -> COLORREF;
    pub fn FillRect(hdc: HDC, lprc: *const RECT, hbr: HBRUSH) -> i32;
    pub fn DrawTextW(hdc: HDC, lpchText: *const u16, cchText: i32, lprc: *mut RECT, format: UINT) -> i32;
    pub fn GetTextExtentPoint32W(hdc: HDC, lpString: *const u16, c: i32, lpsz: *mut SIZE) -> BOOL;
    pub fn RoundRect(hdc: HDC, left: i32, top: i32, right: i32, bottom: i32, w: i32, h: i32) -> BOOL;
    pub fn GetStockObject(i: i32) -> HGDIOBJ;
    pub fn GetDeviceCaps(hdc: HDC, index: u32) -> i32;
    pub fn ExtCreatePen(style: DWORD, width: DWORD, brush: *const LOGBRUSH, count: DWORD, dashes: *const DWORD) -> HGDIOBJ;
    pub fn Ellipse(hdc: HDC, left: i32, top: i32, right: i32, bottom: i32) -> BOOL;
    pub fn MoveToEx(hdc: HDC, x: i32, y: i32, previous: *mut POINT) -> BOOL;
    pub fn LineTo(hdc: HDC, x: i32, y: i32) -> BOOL;
}

#[link(name = "kernel32")]
extern "system" {
    pub fn GetModuleHandleW(lpModuleName: *const u16) -> HMODULE;
    pub fn GetModuleFileNameW(hModule: HMODULE, lpFilename: *mut u16, nSize: DWORD) -> DWORD;
    pub fn CreateProcessW(
        lpApplicationName: *const u16,
        lpCommandLine: *mut u16,
        lpProcessAttributes: *mut c_void,
        lpThreadAttributes: *mut c_void,
        bInheritHandles: BOOL,
        dwCreationFlags: DWORD,
        lpEnvironment: *const c_void,
        lpCurrentDirectory: *const u16,
        lpStartupInfo: *mut STARTUPINFOW,
        lpProcessInformation: *mut PROCESS_INFORMATION,
    ) -> BOOL;
    pub fn CloseHandle(hObject: HANDLE) -> BOOL;
    pub fn GetLastError() -> DWORD;
    pub fn GetFileAttributesW(lpFileName: *const u16) -> DWORD;
    pub fn CopyFileW(lpExistingFileName: *const u16, lpNewFileName: *const u16, bFailIfExists: BOOL) -> BOOL;
    pub fn GetLocalTime(lpSystemTime: *mut SYSTEMTIME);
    pub fn GetPrivateProfileStringW(
        lpAppName: *const u16,
        lpKeyName: *const u16,
        lpDefault: *const u16,
        lpReturnedString: *mut u16,
        nSize: DWORD,
        lpFileName: *const u16,
    ) -> DWORD;
    pub fn WritePrivateProfileStringW(
        lpAppName: *const u16,
        lpKeyName: *const u16,
        lpString: *const u16,
        lpFileName: *const u16,
    ) -> BOOL;
}

// UTF-8 -> NUL-terminated UTF-16
pub fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}
