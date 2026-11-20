#!/usr/bin/env python3
"""Resumen de arranque de sesion + control de la documentacion (lo corre el hook SessionStart de Claude Code).

Imprime en pocas lineas: rama, protocolo, ultimo tag probada-*, estado de git, resultados de juego,
PR abiertos y avisos si la
documentacion volvio a crecer (ESTADO.md / TESTEAR_HOY.md), si quedo "SIN INSTALAR" o si hay links rotos.
Uso: python -I tools/resumen_sesion.py [--sin-pr]
"""
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIMITES = {"ESTADO.md": ("palabras", 2500), "TESTEAR_HOY.md": ("lineas", 250), "AGENTS.md": ("lineas", 160)}
DOCS_CON_LINKS = ["AGENTS.md", "ESTADO.md", "TESTEAR_HOY.md", "INTERRUPTORES.md", "WORKFLOW.md",
                  "PROMPT_NUEVA_SESION.md", "docs/INDICE.md"]


def run(*cmd):
    try:
        return subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, timeout=15).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ""


def leer(rel):
    try:
        with open(os.path.join(REPO, rel), encoding="utf-8") as f:
            return f.read()
    except OSError:
        return None


def avisos_docs():
    out = []
    for rel, (unidad, maximo) in LIMITES.items():
        texto = leer(rel)
        if texto is None:
            continue
        n = len(texto.split()) if unidad == "palabras" else texto.count("\n") + 1
        if n > maximo:
            out.append(f"{rel}: {n} {unidad} (max {maximo}): compactar")
    for rel in ("ESTADO.md", "TESTEAR_HOY.md"):
        texto = leer(rel) or ""
        if "SIN INSTALAR" in texto:
            out.append(f"{rel}: dice 'SIN INSTALAR' (verificar el build instalado)")
    for rel in DOCS_CON_LINKS:
        texto = leer(rel)
        if texto is None:
            continue
        base = os.path.dirname(os.path.join(REPO, rel))
        rutas = re.findall(r"\]\(([^)#\s]+)\)", texto) + re.findall(r"`((?:tareas|docs|tools)/[^`*<>\s]+\.(?:md|py|txt))`", texto)
        for ruta in sorted(set(rutas)):
            if "://" in ruta:
                continue
            candidatos = [os.path.join(base, ruta), os.path.join(REPO, ruta)]
            if not any(os.path.exists(c) for c in candidatos):
                out.append(f"{rel}: link roto {ruta}")
    return out


def main():
    config = leer("shared/config.h") or ""
    m = re.search(r'COOPSA_VERSION\s+"([^"]+)"', config)
    tags = run("git", "tag", "-l", "probada-*", "--sort=-creatordate").splitlines()
    rama = run("git", "branch", "--show-current") or run("git", "rev-parse", "--short", "HEAD")
    estado_git = run("git", "status", "--short")
    print(f"Rama {rama or '?'} | protocolo {m.group(1) if m else '?'} | ultimo tag {tags[0] if tags else '-'} | "
          f"git: {'limpio' if not estado_git else str(len(estado_git.splitlines())) + ' cambios sin commitear'}")
    print("Ultimos commits:")
    print(run("git", "log", "--oneline", "-5"))
    if "--sin-pr" not in sys.argv:
        prs = run("gh", "pr", "list", "--repo", "fv54367-dev/GTASAHISTORIAONLINE", "--state", "open")
        print("PR abiertos: " + (prs if prs else "ninguno"))
    pruebas = leer("TESTEAR_HOY.md")
    if pruebas is not None:
        marcas = re.findall(r"(?m)^- \[([ xNI])\] ", pruebas)
        print(f"TESTEAR_HOY.md: {marcas.count(' ')} por probar | {marcas.count('x')} confirmadas | "
              f"{marcas.count('N')} fallan | {marcas.count('I')} a medias")
    avisos = avisos_docs()
    if avisos:
        print("AVISOS de documentacion:")
        for a in avisos[:10]:
            print("  - " + a)
    return 0


if __name__ == "__main__":
    sys.exit(main())
