// The server side of the launcher: what was typed (name, address, port, remembered in CoopSA.ini next to the
// exe), the address it means (IPv4, IPv4:port or a connection code like the in-game one, shared/ConnectionCode.h),
// the status query the server answers on its game port without an ENet connection (server CNetwork LauncherQuery)
// the same query broadcast to the local network (discover, the SERVIDORES screen) and opening SERVIDOR\server.exe.
use crate::win32::*;
use std::net::{Ipv4Addr, SocketAddrV4, UdpSocket};
use std::os::windows::process::CommandExt;
use std::process::Command;
use std::time::Duration;

pub const DEFAULT_PORT: u16 = 6767; // shared/config.h DEFAULT_PORT
pub const MAX_NAME: usize = 24; // shared/config.h MAX_NICKNAME_LENGTH
const CREATE_NEW_CONSOLE: u32 = 0x0000_0010;

#[derive(Clone)]
pub struct Info {
    pub version: String,
    pub build: String,
    pub name: String,
    pub players: u32,
    pub max: u32,
}

// ConnectionCode::Decode: 12 base-32 digits = [version 1:4][ip:32][port:16][0:8], dashes and "SA-" ignored
fn decode_code(code: &str) -> Option<SocketAddrV4> {
    const ALPHABET: &[u8] = b"0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    let mut c = code.trim();
    if c.len() > 3 && c[..3].eq_ignore_ascii_case("SA-") {
        c = &c[3..];
    }
    let (mut v, mut n) = (0u64, 0);
    for ch in c.chars() {
        if ch == '-' || ch == ' ' {
            continue;
        }
        let ch = match ch.to_ascii_uppercase() {
            'O' => '0',
            'I' | 'L' => '1',
            x => x,
        };
        let d = ALPHABET.iter().position(|&a| a as char == ch)? as u64;
        if n >= 12 {
            return None;
        }
        v = (v << 5) | d;
        n += 1;
    }
    if n != 12 || (v >> 56) != 1 || (v & 255) != 0 {
        return None;
    }
    let port = ((v >> 8) & 0xFFFF) as u16;
    let ip = ((v >> 24) & 0xFFFF_FFFF) as u32;
    if ip == 0 || port == 0 {
        return None;
    }
    Some(SocketAddrV4::new(Ipv4Addr::from(ip), port))
}

// what the game gets after -connect: "ip:port" or the code itself (the client decodes it, CNetwork::ParseAddress)
pub fn resolve(address: &str, port: &str) -> Option<(SocketAddrV4, String)> {
    let address = address.trim();
    if let Some(addr) = decode_code(address) {
        return Some((addr, address.to_string()));
    }
    let (host, port) = match address.split_once(':') {
        Some((h, p)) => (h, p.trim()),
        None => (address, port.trim()),
    };
    let port: u16 = if port.is_empty() { DEFAULT_PORT } else { port.parse().ok()? };
    let ip: Ipv4Addr = host.trim().parse().ok()?;
    if port == 0 {
        return None;
    }
    let addr = SocketAddrV4::new(ip, port);
    Some((addr, addr.to_string()))
}

// two tries of `wait_ms` each (UDP may drop one); None = nobody answered (no server, or an older one)
pub fn query(addr: SocketAddrV4, wait_ms: u64) -> Option<Info> {
    let socket = UdpSocket::bind("0.0.0.0:0").ok()?;
    socket.set_read_timeout(Some(Duration::from_millis(wait_ms))).ok()?;
    let mut buf = [0u8; 512];
    for _ in 0..2 {
        if socket.send_to(b"COOPSA?1", addr).is_err() {
            return None;
        }
        if let Ok((n, from)) = socket.recv_from(&mut buf) {
            if from.ip() != std::net::IpAddr::V4(*addr.ip()) {
                continue;
            }
            return parse_reply(&buf[..n]);
        }
    }
    None
}

fn parse_reply(data: &[u8]) -> Option<Info> {
    let text = String::from_utf8_lossy(data).into_owned();
    let mut lines = text.split('\n');
    if lines.next() != Some("COOPSA!1") {
        return None;
    }
    let mut next = || lines.next().unwrap_or("").to_string();
    let version = next();
    let build = next();
    let name = next();
    let players = next().parse().unwrap_or(0);
    let max = next().parse().unwrap_or(0);
    Some(Info { version, build, name, players, max })
}

#[derive(Clone)]
pub struct Found {
    pub addr: SocketAddrV4,
    pub info: Info,
    pub ping_ms: u32,
}

// the IPv4 addresses of this PC (every adapter: LAN, Wi-Fi, Radmin/ZeroTier/Hamachi...)
fn local_ips() -> Vec<Ipv4Addr> {
    use std::net::ToSocketAddrs;
    let host = std::env::var("COMPUTERNAME").unwrap_or_else(|_| "localhost".to_string());
    let mut ips: Vec<Ipv4Addr> = (host.as_str(), 0)
        .to_socket_addrs()
        .map(|list| list.filter_map(|a| match a.ip() { std::net::IpAddr::V4(ip) => Some(ip), _ => None }).collect())
        .unwrap_or_default();
    ips.retain(|ip| !ip.is_loopback() && !ip.is_unspecified() && !ip.is_link_local());
    ips.dedup();
    ips
}

// COOPSA?1 to the whole network: the limited broadcast, the broadcast of each adapter (the mask is unknown, so /24, /16
// and /8) and this PC itself, on the default port and the next three (the local test servers) plus SERVIDOR's own.
// Two rounds (UDP may drop one); a server is listed once, the fastest answer gives the ping.
pub fn discover(wait_ms: u64) -> Vec<Found> {
    let Ok(socket) = UdpSocket::bind("0.0.0.0:0") else { return Vec::new() };
    let _ = socket.set_broadcast(true);
    let own = local_ips();
    let mut targets = vec![Ipv4Addr::BROADCAST, Ipv4Addr::LOCALHOST];
    for ip in &own {
        let o = ip.octets();
        targets.push(Ipv4Addr::new(o[0], o[1], o[2], 255));
        targets.push(Ipv4Addr::new(o[0], o[1], 255, 255));
        targets.push(Ipv4Addr::new(o[0], 255, 255, 255));
    }
    targets.sort();
    targets.dedup();
    let mut ports: Vec<u16> = (DEFAULT_PORT..DEFAULT_PORT + 4).collect();
    let own_port = local_port();
    if !ports.contains(&own_port) {
        ports.push(own_port);
    }

    let start = std::time::Instant::now();
    let mut found: Vec<Found> = Vec::new();
    let mut buf = [0u8; 512];
    for round in 0..2u64 {
        let sent = std::time::Instant::now();
        for ip in &targets {
            for port in &ports {
                let _ = socket.send_to(b"COOPSA?1", SocketAddrV4::new(*ip, *port));
            }
        }
        let until = start + Duration::from_millis(wait_ms * (round + 1) / 2);
        loop {
            let now = std::time::Instant::now();
            if now >= until {
                break;
            }
            let _ = socket.set_read_timeout(Some((until - now).max(Duration::from_millis(1))));
            let Ok((n, from)) = socket.recv_from(&mut buf) else { continue };
            let std::net::SocketAddr::V4(from) = from else { continue };
            let Some(info) = parse_reply(&buf[..n]) else { continue };
            let ping_ms = sent.elapsed().as_millis() as u32;
            // this PC answers on 127.0.0.1 and on each of its addresses: keep it once, as 127.0.0.1
            let addr = if own.contains(from.ip()) { SocketAddrV4::new(Ipv4Addr::LOCALHOST, from.port()) } else { from };
            match found.iter_mut().find(|f| f.addr == addr) {
                Some(f) => {
                    f.ping_ms = f.ping_ms.min(ping_ms);
                    f.info = info;
                }
                None => found.push(Found { addr, info, ping_ms }),
            }
        }
    }
    found.sort_by(|a, b| a.ping_ms.cmp(&b.ping_ms).then(a.info.name.cmp(&b.info.name)));
    found
}

fn ini_path() -> String {
    format!("{}\\CoopSA.ini", crate::launch::game_dir())
}

pub fn ini_read(file: &str, section: &str, key: &str) -> String {
    let mut buf = [0u16; 256];
    let n = unsafe {
        GetPrivateProfileStringW(
            wide(section).as_ptr(),
            wide(key).as_ptr(),
            wide("").as_ptr(),
            buf.as_mut_ptr(),
            buf.len() as DWORD,
            wide(file).as_ptr(),
        )
    } as usize;
    String::from_utf16_lossy(&buf[..n]).trim().to_string()
}

// name, address, port: the launcher's own file first; the first time, the nickname/ip/port the game saved
pub fn load_fields() -> (String, String, String) {
    let file = ini_path();
    let mut name = ini_read(&file, "launcher", "nombre");
    let mut address = ini_read(&file, "launcher", "direccion");
    let mut port = ini_read(&file, "launcher", "puerto");
    if name.is_empty() && address.is_empty() {
        let game = format!("{}\\coopandreas.ini", crate::saves::user_dir());
        name = ini_read(&game, "config", "nickname");
        address = ini_read(&game, "config", "ip");
        port = ini_read(&game, "config", "port");
    }
    if address.is_empty() {
        address = "127.0.0.1".to_string();
    }
    if port.is_empty() {
        port = DEFAULT_PORT.to_string();
    }
    (name, address, port)
}

pub fn save_fields(name: &str, address: &str, port: &str) {
    let file = wide(&ini_path());
    let section = wide("launcher");
    for (key, value) in [("nombre", name), ("direccion", address), ("puerto", port)] {
        unsafe {
            WritePrivateProfileStringW(section.as_ptr(), wide(key).as_ptr(), wide(value).as_ptr(), file.as_ptr());
        }
    }
}

// the name goes on the game's command line split by spaces (CLaunchManager): no spaces, at most 24 characters
pub fn clean_name(name: &str) -> String {
    name.trim().replace(' ', "_").chars().take(MAX_NAME).collect()
}

fn server_dir() -> String {
    format!("{}\\SERVIDOR", crate::launch::game_dir())
}

// the port of SERVIDOR\server-config.ini ("port = N", no section)
pub fn local_port() -> u16 {
    std::fs::read_to_string(format!("{}\\server-config.ini", server_dir()))
        .ok()
        .and_then(|text| {
            text.lines().find_map(|l| {
                let (k, v) = l.split_once('=')?;
                if k.trim().eq_ignore_ascii_case("port") { v.trim().parse().ok() } else { None }
            })
        })
        .unwrap_or(DEFAULT_PORT)
}

pub fn start_local_server() -> Result<(), String> {
    let dir = server_dir();
    let exe = format!("{}\\server.exe", dir);
    if std::fs::metadata(&exe).is_err() {
        return Err("No est\u{e1} el servidor (SERVIDOR\\server.exe junto al juego).\n\nActualiz\u{e1} con el bot\u{f3}n de arriba a la derecha: el paquete lo trae.".to_string());
    }
    Command::new(&exe)
        .current_dir(&dir)
        .creation_flags(CREATE_NEW_CONSOLE)
        .spawn()
        .map(|_| ())
        .map_err(|e| format!("No se pudo abrir el servidor: {}", e))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn code_and_addresses() {
        // ConnectionCode::Encode(127.0.0.1, 6767)
        let (addr, target) = resolve("2ZR0-000H-MVR0", "").unwrap();
        assert_eq!(addr.to_string(), "127.0.0.1:6767");
        assert_eq!(target, "2ZR0-000H-MVR0");
        assert_eq!(resolve("192.168.0.10", "7000").unwrap().1, "192.168.0.10:7000");
        assert_eq!(resolve("192.168.0.10:7001", "7000").unwrap().1, "192.168.0.10:7001");
        assert_eq!(resolve("10.0.0.1", "").unwrap().1, "10.0.0.1:6767");
        assert!(resolve("hola", "6767").is_none());
        assert_eq!(clean_name("  Juan Perez  "), "Juan_Perez");
    }

    #[test]
    fn query_reads_the_server_answer() {
        // the reply of server CNetwork LauncherQuery
        let fake = UdpSocket::bind("127.0.0.1:0").unwrap();
        let addr = match fake.local_addr().unwrap() {
            std::net::SocketAddr::V4(a) => a,
            _ => unreachable!(),
        };
        std::thread::spawn(move || {
            let mut buf = [0u8; 64];
            let (n, from) = fake.recv_from(&mut buf).unwrap();
            assert_eq!(&buf[..n], b"COOPSA?1");
            fake.send_to(b"COOPSA!1\n0.4.56-alpha\nmain-abc1234\nServidor COOP San Andreas\n2\n8\n", from).unwrap();
        });
        let info = query(addr, 1000).unwrap();
        assert_eq!(info.version, "0.4.56-alpha");
        assert_eq!(info.build, "main-abc1234");
        assert_eq!(info.name, "Servidor COOP San Andreas");
        assert_eq!((info.players, info.max), (2, 8));
        // nobody on that port any more: no answer
        assert!(query(SocketAddrV4::new(Ipv4Addr::LOCALHOST, 9), 200).is_none());
    }

    // a real server open on this PC (PROBAR_SOLO or SERVIDOR\server.exe) shows up once, as 127.0.0.1
    #[test]
    #[ignore]
    fn discover_lists_local_servers() {
        let list = discover(800);
        for f in &list {
            println!("{} {} {}/{} {} ms {}", f.addr, f.info.name, f.info.players, f.info.max, f.ping_ms, f.info.build);
        }
        assert!(!list.is_empty());
    }
}
