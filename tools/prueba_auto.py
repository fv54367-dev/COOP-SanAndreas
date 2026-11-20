#!/usr/bin/env python3
"""Pruebas automaticas en juego (coop) contra el sa_bridge de cada GTA de prueba.

Habla por TCP 127.0.0.1:47311+N (una linea JSON por pedido/respuesta) con las instancias que ya estan
abiertas (por defecto NO lanza nada) y usa el export `sa_debug_json` de CoopAndreasSA.dll.
Escenarios: conexion, jugadores, npc, animaciones, capturas. Exit 0 si todo PASS (SKIP no cuenta como fallo).
Solo libreria estandar.
"""
import argparse
import base64
import datetime
import glob
import json
import math
import os
import socket
import struct
import subprocess
import sys
import time
import zlib

ROOT = r"E:\juegos\gtasacoopnuevo"
BASE_PORT = 47311
MODULE = "CoopAndreasSA.dll"
EXPORT = "sa_debug_json"

# CControllerState field indices (sa-bridge pad.rs / PAD_FIELD_NAMES)
PAD_STICK_X, PAD_STICK_Y, PAD_CROSS = 0, 1, 16


class BridgeError(Exception):
    pass


class Bridge:
    """One GTA instance: TCP JSON-lines client."""

    def __init__(self, slot, port, host="127.0.0.1", timeout=15.0):
        self.slot, self.port, self.host, self.timeout = slot, port, host, timeout
        self.sock = None
        self.buf = b""

    def connect(self):
        self.close()
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self.sock.settimeout(self.timeout)
        self.buf = b""

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = None

    def query(self, req, timeout=None):
        if self.sock is None:
            self.connect()
        try:
            self.sock.settimeout(timeout or self.timeout)
            self.sock.sendall((json.dumps(req) + "\n").encode())
            while b"\n" not in self.buf:
                chunk = self.sock.recv(1 << 20)
                if not chunk:
                    raise BridgeError("conexion cerrada")
                self.buf += chunk
        except (OSError, BridgeError) as e:
            self.close()
            raise BridgeError(f"instancia {self.slot}: {e}")
        line, self.buf = self.buf.split(b"\n", 1)
        resp = json.loads(line)
        if not resp.get("ok"):
            raise BridgeError(f"instancia {self.slot}: {resp.get('error')}")
        return resp.get("data")

    def debug(self):
        return self.query({"cmd": "plugin_query", "module": MODULE, "export": EXPORT})

    def position(self):
        return self.query({"cmd": "player_state"})["position"]


def discover(base_port, max_slots=8, host="127.0.0.1"):
    found = []
    for n in range(max_slots):
        b = Bridge(n, base_port + n, host, timeout=10.0)
        try:
            b.connect()
            b.query({"cmd": "status"})
            found.append(b)
        except (OSError, BridgeError):
            b.close()
    return found


def dist(a, b):
    return math.sqrt(sum((a[k] - b[k]) ** 2 for k in "xyz"))


def stats(v):
    if not v:
        return {"n": 0, "media": None, "p95": None, "max": None}
    s = sorted(v)
    p95 = s[min(len(s) - 1, int(math.ceil(0.95 * len(s))) - 1)]
    return {"n": len(s), "media": round(sum(s) / len(s), 3), "p95": round(p95, 3), "max": round(s[-1], 3)}


def find_remote(dbg, net_id):
    for r in dbg.get("remotes", []):
        if r.get("net_id") == net_id:
            return r
    return None


def crash_logs(gta_root):
    out = {}
    for f in glob.glob(os.path.join(gta_root, "GTA_JUGADOR*", "CoopAndreas_crashes", "*.log")):
        out[f] = os.path.getmtime(f)
    return out


class Result:
    def __init__(self, name, status, summary, data=None):
        self.name, self.status, self.summary, self.data = name, status, summary, data or {}


# open spot in the middle of the Grove Street cul-de-sac: room to run in circles and NPCs both games share
GROVE_SPOTS = [(2486.0, -1666.0, 13.6), (2489.0, -1666.0, 13.6), (2486.0, -1669.0, 13.6), (2489.0, -1669.0, 13.6)]


def preparar(ctx):
    """Puts every CJ on foot next to each other once, so a test never walks into a wall or compares far players."""
    if ctx.get("preparado"):
        return
    ctx["preparado"] = True
    for i, b in enumerate(ctx["bridges"][:len(GROVE_SPOTS)]):
        x, y, z = GROVE_SPOTS[i]
        try:
            b.query({"cmd": "input_clear"})
            b.query({"cmd": "teleport", "x": x, "y": y, "z": z})
        except BridgeError as e:
            print("preparar:", e)
    time.sleep(5)  # streaming + the first position packets


# ----------------------------------------------------------------------------------------------------------------
# scenarios
# ----------------------------------------------------------------------------------------------------------------
def esc_conexion(ctx):
    br = ctx["bridges"]
    deadline = time.time() + ctx["args"].timeout_conexion
    last = "sin datos"
    dbgs = []
    ok_link = False
    while time.time() < deadline:
        try:
            dbgs = [b.debug() for b in br]
        except BridgeError as e:
            last = str(e)
            time.sleep(1)
            continue
        conn = [d for d in dbgs if d.get("connected")]
        if len(br) >= 2 and len(conn) == len(dbgs):
            ids = [d["net_id"] for d in dbgs]
            ok_link = all(find_remote(d, o) is not None for d in dbgs for o in ids if o != d["net_id"])
            last = f"conectadas {len(conn)}/{len(dbgs)}, enlazadas={ok_link}"
            if ok_link:
                break
        else:
            last = f"instancias={len(br)} conectadas={len(conn)}"
        time.sleep(1)
    ctx["ids"] = [d.get("net_id") for d in dbgs]
    versions = sorted({d.get("version") for d in dbgs})
    new_crash = [f for f, m in crash_logs(ctx["args"].gta_root).items()
                 if f not in ctx["crashes0"] or m > ctx["crashes0"][f]]
    problems = []
    if len(br) < 2:
        problems.append(f"solo {len(br)} instancia(s)")
    if not ok_link:
        problems.append(f"no se ven entre si ({last})")
    if len(versions) > 1:
        problems.append(f"versiones distintas {versions}")
    if new_crash:
        problems.append(f"{len(new_crash)} crash log nuevo(s)")
    data = {"net_ids": ctx["ids"], "versiones": versions, "crashes_nuevos": new_crash}
    if problems:
        return Result("conexion", "FAIL", "; ".join(problems), data)
    return Result("conexion", "PASS", f"{len(br)} instancias conectadas y enlazadas, version {versions[0]}, sin crashes nuevos", data)


def esc_jugadores(ctx):
    a_args = ctx["args"]
    br = ctx["bridges"]
    if len(br) < 2:
        return Result("jugadores", "FAIL", "hacen falta 2 instancias")
    preparar(ctx)
    a, b = br[0], br[1]
    id_a = a.debug()["net_id"]
    secs = a_args.segundos
    start = a.position()
    # run forward while steering: a circle of a few metres, so it stays in the open spot
    steps = [{"ms": int(secs * 1000), "fields": [[PAD_STICK_Y, -128], [PAD_STICK_X, 70], [PAD_CROSS, 255]]}]
    a.query({"cmd": "input", "steps": steps, "append": False})
    travelled, last_real = 0.0, start
    errs, jumps, samples = [], 0, []
    prev = None
    t_end = time.time() + secs
    nxt = time.time()
    while time.time() < t_end:
        pa = a.position()
        travelled += dist(last_real, pa)
        last_real = pa
        r = find_remote(b.debug(), id_a)
        if r and r.get("has_ped", True):
            pc = r["position"]
            e = dist(pa, pc)
            errs.append(e)
            if prev is not None and dist(prev, pc) > 2.0:
                jumps += 1
            prev = pc
            samples.append({"real": pa, "copia": pc, "err": round(e, 3)})
        nxt += 0.1
        time.sleep(max(0, nxt - time.time()))
    a.query({"cmd": "input_clear"})
    st = stats(errs)
    st["saltos_mayor_2m"] = jumps
    st["recorrido_m"] = round(travelled, 1)
    data = {"stats": st, "muestras": samples[::5]}
    if not errs:
        return Result("jugadores", "FAIL", "la copia del jugador 0 no aparece en la instancia 1", data)
    if travelled < 5.0:
        return Result("jugadores", "FAIL", f"prueba no valida: el CJ 0 solo recorrio {travelled:.1f} m (control bloqueado?)", data)
    summary = f"recorrio {st['recorrido_m']} m, error real-vs-copia media {st['media']} p95 {st['p95']} max {st['max']} m, saltos>2m {jumps} (n={st['n']})"
    return Result("jugadores", "PASS" if st["p95"] <= a_args.umbral_jugadores else "FAIL",
                  summary + f", umbral p95 {a_args.umbral_jugadores}", data)


def esc_npc(ctx):
    a_args = ctx["args"]
    br = ctx["bridges"]
    if len(br) < 2:
        return Result("npc", "FAIL", "hacen falta 2 instancias")
    preparar(ctx)
    errs, missing, total, mine_seen, bad = [], 0, 0, set(), set()
    t_end = time.time() + a_args.segundos
    while time.time() < t_end:
        dbgs = [b.debug() for b in br[:2]]
        locals_ = [d.get("local_position") for d in dbgs]
        for i, d in enumerate(dbgs):
            for bp in d.get("bad_positions", []):
                bad.add((i, bp.get("ped_id"), bp.get("model"), bp.get("mine")))
        for owner, other in ((0, 1), (1, 0)):
            copies = {p["ped_id"]: p for p in dbgs[other].get("ped_list", []) if not p.get("mine")}
            for p in dbgs[owner].get("ped_list", []):
                if not p.get("mine") or not p.get("has_ped", True):
                    continue
                # a ped far from the other CJ is legitimately not streamed there
                if any(lp and dist(p["position"], lp) > 50.0 for lp in locals_):
                    continue
                mine_seen.add(p["ped_id"])
                c = copies.get(p["ped_id"])
                total += 1
                if c is None or not c.get("has_ped", True):
                    missing += 1
                else:
                    errs.append(dist(p["position"], c["position"]))
        time.sleep(0.5)
    st = stats(errs)
    st["copias_faltantes"] = missing
    st["muestras_cruzadas"] = total
    st["peds_propios_distintos"] = len(mine_seen)
    st["posiciones_rotas"] = len(bad)
    data = {"stats": st, "posiciones_rotas": [{"instancia": i, "ped_id": pid, "modelo": m, "propio": mine}
                                             for i, pid, m, mine in sorted(bad, key=str)]}
    if bad:
        return Result("npc", "FAIL", f"{len(bad)} NPC con posicion NaN/inf (ver datos.json)", data)
    if total == 0:
        return Result("npc", "SKIP", "no hay NPCs propios a menos de 50 m de los dos CJ", data)
    frac = missing / total
    ok = (st["p95"] is not None and st["p95"] <= a_args.umbral_npc) and frac <= a_args.max_faltantes
    summary = (f"error medio {st['media']} p95 {st['p95']} max {st['max']} m, copias faltantes {missing}/{total} "
               f"({frac:.0%}), peds distintos {len(mine_seen)}")
    return Result("npc", "PASS" if ok else "FAIL", summary, data)


def anim_matches(anims, block):
    block = block.lower()
    return [x for x in anims if str(x.get("block", "")).lower() == block]


def esc_animaciones(ctx):
    br = ctx["bridges"]
    if len(br) < 2:
        return Result("animaciones", "FAIL", "hacen falta 2 instancias")
    preparar(ctx)
    a, b = br[0], br[1]
    block, name = "DANCING", "DNCE_M_A"
    id_a = a.debug()["net_id"]
    a.query({"cmd": "run_script", "commands": [{"op": 0x04ED, "args": [{"str": block}]}]})
    loaded = False
    for _ in range(50):
        res = a.query({"cmd": "run_script", "commands": [{"op": 0x04EE, "args": [{"str": block}]}]})
        if res and res.get("results") and res["results"][-1].get("cond"):
            loaded = True
            break
        time.sleep(0.1)
    if not loaded:
        return Result("animaciones", "FAIL", f"el bloque {block} no cargo en la instancia 0 en 5 s")
    # 01F5 GET_PLAYER_CHAR slot -> var 0; 0605 TASK_PLAY_ANIM handle anim file blend loop lockX lockY lockF time
    a.query({"cmd": "run_script", "commands": [
        {"op": 0x01F5, "args": [{"int": 0}, {"var": 0}]},
        {"op": 0x0605, "args": [{"var": 0}, {"str": name}, {"str": block}, {"float": 4.0}, {"int": 1},
                                {"int": 0}, {"int": 0}, {"int": 0}, {"int": -1}]}]})
    t0 = time.time()
    found = []
    while time.time() - t0 < ctx["args"].espera_anim:
        time.sleep(0.25)
        r = find_remote(b.debug(), id_a)
        if r:
            found = anim_matches(r.get("anims", []), block)
            if found:
                break
    lat = round(time.time() - t0, 2)
    local = anim_matches(a.debug().get("local_anims", []), block)
    # stop it so the next scenarios see a normal CJ
    a.query({"cmd": "run_script", "commands": [{"op": 0x0687, "args": [{"var": 0}]}, {"op": 0x04EF, "args": [{"str": block}]}]})
    data = {"local": local, "copia": found, "latencia_s": lat}
    if not local:
        return Result("animaciones", "FAIL", "la animacion no se reprodujo en el CJ local (prueba no valida)", data)
    if not found:
        return Result("animaciones", "FAIL", f"la copia en la instancia 1 no tiene {block} tras {lat} s", data)
    return Result("animaciones", "PASS", f"{block}/{name} del CJ 0 aparece en la copia de la instancia 1 en {lat} s", data)


def write_png(path, width, height, bgra):
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        row = bgra[y * stride:(y + 1) * stride]
        rgb = bytearray(width * 3)
        rgb[0::3] = row[2::4]
        rgb[1::3] = row[1::4]
        rgb[2::3] = row[0::4]
        raw += rgb

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def esc_capturas(ctx):
    files = []
    for b in ctx["bridges"]:
        d = None
        for _ in range(3):
            try:
                d = b.query({"cmd": "screenshot", "max_width": 960}, timeout=30)
                break
            except BridgeError:
                time.sleep(1)
        if d is None:
            return Result("capturas", "SKIP", f"la instancia {b.slot} no dibuja (ventana sin foco); usar la captura del MCP", {"archivos": files})
        w, h = d["width"], d["height"]
        bgra = base64.b64decode(d["bgra_b64"])
        if len(bgra) < w * h * 4:
            return Result("capturas", "FAIL", f"captura de la instancia {b.slot} incompleta")
        path = os.path.join(ctx["out"], f"captura_instancia{b.slot}.png")
        write_png(path, w, h, bgra)
        files.append(path)
    return Result("capturas", "PASS" if files else "FAIL", f"{len(files)} captura(s) en {ctx['out']}", {"archivos": files})


SCENARIOS = {"conexion": esc_conexion, "jugadores": esc_jugadores, "npc": esc_npc,
             "animaciones": esc_animaciones, "capturas": esc_capturas}


def launch(args):
    bat = os.path.join(args.gta_root, "PROBAR_SOLO.bat")
    print("lanzando", bat)
    subprocess.Popen(["cmd", "/c", "start", "", bat], cwd=args.gta_root)
    deadline = time.time() + args.espera_lanzar
    while time.time() < deadline:
        found = discover(args.puerto_base)
        if len(found) >= 2:
            return found
        time.sleep(5)
    return discover(args.puerto_base)


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--salida", help="carpeta de salida")
    p.add_argument("--escenarios", default=",".join(SCENARIOS))
    p.add_argument("--lanzar", action="store_true", help="correr PROBAR_SOLO.bat y esperar (por defecto no lanza nada)")
    p.add_argument("--gta-root", default=ROOT)
    p.add_argument("--puerto-base", type=int, default=BASE_PORT)
    p.add_argument("--espera-lanzar", type=float, default=240.0)
    p.add_argument("--timeout-conexion", type=float, default=60.0)
    p.add_argument("--segundos", type=float, default=10.0, help="duracion de jugadores/npc")
    p.add_argument("--umbral-jugadores", type=float, default=1.5, help="p95 maximo (m) real vs copia")
    p.add_argument("--umbral-npc", type=float, default=3.0, help="p95 maximo (m) dueno vs copia de NPC")
    p.add_argument("--max-faltantes", type=float, default=0.25, help="fraccion maxima de copias de NPC faltantes")
    p.add_argument("--espera-anim", type=float, default=3.0)
    return p.parse_args(argv)


def main(argv=None):
    sys.stdout.reconfigure(line_buffering=True)
    args = parse_args(argv)
    names = [n.strip() for n in args.escenarios.split(",") if n.strip()]
    for n in names:
        if n not in SCENARIOS:
            print("escenario desconocido:", n, "(hay:", ", ".join(SCENARIOS) + ")")
            return 2
    out = args.salida or os.path.join(args.gta_root, "_pruebas_auto", datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S"))
    os.makedirs(out, exist_ok=True)
    crashes0 = crash_logs(args.gta_root)
    bridges = launch(args) if args.lanzar else discover(args.puerto_base)
    # the second GTA of PROBAR_SOLO starts a few seconds after the first one
    deadline = time.time() + args.timeout_conexion
    while len(bridges) < 2 and time.time() < deadline:
        time.sleep(3)
        for b in bridges:
            b.close()
        bridges = discover(args.puerto_base)
    print(f"instancias encontradas: {len(bridges)}")
    ctx = {"args": args, "bridges": bridges, "out": out, "crashes0": crashes0}
    results = []
    for n in names:
        try:
            r = SCENARIOS[n](ctx)
        except (BridgeError, OSError, KeyError, ValueError) as e:
            r = Result(n, "FAIL", f"error: {type(e).__name__}: {e}")
        results.append(r)
        print(f"{r.status} {r.name}: {r.summary}")
    with open(os.path.join(out, "informe.txt"), "w", encoding="utf-8") as f:
        for r in results:
            f.write(f"{r.status} {r.name}: {r.summary}\n")
    with open(os.path.join(out, "datos.json"), "w", encoding="utf-8") as f:
        json.dump([{"escenario": r.name, "estado": r.status, "resumen": r.summary, "datos": r.data} for r in results],
                  f, indent=1, ensure_ascii=False)
    for b in bridges:
        b.close()
    print("salida:", out)
    return 0 if all(r.status in ("PASS", "SKIP") for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
