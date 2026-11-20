#!/usr/bin/env python3
"""Prueba de prueba_auto.py contra un bridge falso (servidores TCP locales). No necesita el juego."""
import json
import os
import socketserver
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prueba_auto as pa  # noqa: E402


class FakeWorld:
    """Two instances; A walks at 5 m/s while an input is queued, B shows A with `lag` seconds of delay."""

    def __init__(self, lag=0.2, anim_delay=0.4, version=("0.4.48", "0.4.48"), link=True):
        self.lag, self.anim_delay, self.version, self.link = lag, anim_delay, version, link
        self.t0 = time.time()
        self.input_until = [0.0, 0.0]
        self.walk_start = [0.0, 0.0]
        self.anim = [None, None]  # (block, since)

    def pos(self, i, t=None):
        t = time.time() if t is None else t
        x = 10.0 * i
        if self.walk_start[i] and self.walk_start[i] <= t:
            x += 5.0 * (min(t, self.input_until[i]) - self.walk_start[i])
        return {"x": x, "y": 0.0, "z": 9.0}

    def debug(self, i):
        o = 1 - i
        d = {"net_id": i + 1, "version": self.version[i], "connected": True, "authenticated": True, "area": 0,
             "mission": {"id": -1}, "local_anims": [], "remotes": [], "peds": {"mine": 3, "copies": 3}, "vehicles": {"mine": 0, "copies": 0}}
        if self.anim[i]:
            d["local_anims"] = [{"group": 0, "id": 1, "blend": 1.0, "block": self.anim[i][0]}]
        if self.link:
            anims = []
            if self.anim[o] and time.time() - self.anim[o][1] > self.anim_delay:
                anims = [{"group": 0, "id": 1, "blend": 1.0, "block": self.anim[o][0].lower()}]
            d["remotes"] = [{"net_id": o + 1, "name": f"J{o}", "position": self.pos(o, time.time() - self.lag),
                             "net_position": self.pos(o, time.time() - self.lag), "age_ms": 50, "has_ped": True,
                             "anims": anims}]
        d["ped_list"] = []
        for k in range(3):  # peds owned by instance i
            base = {"x": 100.0 + k * 5, "y": 5.0, "z": 9.0}
            d["ped_list"].append({"ped_id": 1000 * (i + 1) + k, "mine": True, "model": 7, "position": base, "has_ped": True})
            # copy of the ped owned by the other instance (drifts 0.5 m)
            d["ped_list"].append({"ped_id": 1000 * (o + 1) + k, "mine": False, "model": 7, "has_ped": True,
                                  "position": {"x": base["x"] + 0.5, "y": 5.0, "z": 9.0}, "net_position": base, "age_ms": 100})
        return d

    def handle(self, i, req):
        cmd = req.get("cmd")
        now = time.time()
        if cmd == "status":
            return {"state": "ingame"}
        if cmd == "player_state":
            return {"position": self.pos(i)}
        if cmd == "plugin_query":
            assert req["module"] == pa.MODULE and req["export"] == pa.EXPORT
            return self.debug(i)
        if cmd == "input":
            ms = sum(s["ms"] for s in req["steps"])
            self.walk_start[i] = now
            self.input_until[i] = now + ms / 1000.0
            return {"queued_ms": ms}
        if cmd == "input_clear":
            self.input_until[i] = min(self.input_until[i], now)
            return {}
        if cmd == "run_script":
            conds = []
            for c in req["commands"]:
                op, args = c["op"], c["args"]
                if op == 0x04EE:
                    conds.append({"op": "04EE", "cond": True})
                elif op == 0x0605:
                    self.anim[i] = (args[2]["str"], now)
                    conds.append({"op": "0605", "cond": True})
                elif op == 0x0687:
                    self.anim[i] = None
                    conds.append({"op": "0687", "cond": True})
                else:
                    conds.append({"op": "%04X" % op, "cond": True})
            return {"results": conds, "vars": {}}
        if cmd == "screenshot":
            w, h = 8, 4
            import base64
            return {"width": w, "height": h, "bgra_b64": base64.b64encode(bytes([10, 20, 30, 255]) * (w * h)).decode()}
        raise ValueError("cmd " + str(cmd))


def start_fake(world):
    servers = []
    for i in range(2):
        class H(socketserver.StreamRequestHandler):
            slot = i

            def handle(self):
                for line in self.rfile:
                    try:
                        resp = {"ok": True, "data": world.handle(self.slot, json.loads(line))}
                    except Exception as e:  # noqa: BLE001
                        resp = {"ok": False, "error": str(e)}
                    self.wfile.write((json.dumps(resp) + "\n").encode())

        srv = socketserver.ThreadingTCPServer(("127.0.0.1", 0), H)
        srv.daemon_threads = True
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        servers.append(srv)
    return servers


def run(world, extra=(), escenarios="conexion,jugadores,npc,animaciones,capturas"):
    servers = start_fake(world)
    out = tempfile.mkdtemp()
    root = tempfile.mkdtemp()
    ports = [s.server_address[1] for s in servers]
    # discover() scans base+N: make the fake ports consecutive by wrapping discover
    orig = pa.discover
    pa.discover = lambda base, max_slots=8, host="127.0.0.1": [pa.Bridge(n, p) for n, p in enumerate(ports)]
    try:
        code = pa.main(["--salida", out, "--gta-root", root, "--segundos", "4", "--timeout-conexion", "5",
                        "--escenarios", escenarios, *extra])
    finally:
        pa.discover = orig
        for s in servers:
            s.shutdown()
    return code, out


class T(unittest.TestCase):
    def test_todo_pass(self):
        code, out = run(FakeWorld())
        informe = open(os.path.join(out, "informe.txt"), encoding="utf-8").read()
        self.assertEqual(code, 0, informe)
        self.assertEqual(informe.count("PASS"), 5, informe)
        self.assertTrue(os.path.getsize(os.path.join(out, "captura_instancia0.png")) > 50)
        datos = json.load(open(os.path.join(out, "datos.json"), encoding="utf-8"))
        self.assertEqual(len(datos), 5)
        self.assertEqual(open(os.path.join(out, "captura_instancia0.png"), "rb").read(8), b"\x89PNG\r\n\x1a\n")

    def test_lag_falla_jugadores(self):
        code, out = run(FakeWorld(lag=1.0), escenarios="jugadores")
        self.assertEqual(code, 1)
        self.assertIn("FAIL jugadores", open(os.path.join(out, "informe.txt"), encoding="utf-8").read())

    def test_version_distinta_falla_conexion(self):
        code, out = run(FakeWorld(version=("0.4.48", "0.4.47")), escenarios="conexion")
        self.assertEqual(code, 1)
        self.assertIn("versiones distintas", open(os.path.join(out, "informe.txt"), encoding="utf-8").read())

    def test_sin_enlace_falla(self):
        code, out = run(FakeWorld(link=False), escenarios="conexion")
        self.assertEqual(code, 1)

    def test_anim_no_llega_falla(self):
        code, out = run(FakeWorld(anim_delay=99), escenarios="animaciones")
        self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
