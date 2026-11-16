# Instalación

## Requisitos

- **GTA: San Andreas v1.0 US** (`gta_sa.exe` de la versión 1.0). La versión de Steam o de Rockstar Launcher (`gta-sa.exe`) hay que pasarla a 1.0 con un downgrader.
- Windows 10 u 11.

## Instalar

1. Bajá `ACTUALIZAR.bat` del [release](../../releases) "Principal" y ponelo en la carpeta del juego, junto a `gta_sa.exe`.
2. Hacele doble clic y elegí la versión (Enter = la principal). Instala todo el mod y deja un respaldo de lo que reemplaza en `_backup_mod\`.
3. Abrí el juego con **`CoopSA.exe`**. Abriendo `gta_sa.exe` directo, el juego arranca normal, sin el mod.

Para actualizar, el botón de arriba a la derecha de `CoopSA.exe` avisa cuando hay una versión nueva y la instala.

## Dónde queda cada cosa

Dentro de la carpeta del juego:

| Archivo o carpeta | Qué es |
|---|---|
| `CoopSA.exe` | El launcher: abre el juego con el mod. |
| `CoopAndreasSA.dll` | El mod. |
| `eax.dll` | El cargador: arranca el mod solo cuando el juego se abre desde `CoopSA.exe`. El `eax.dll` original del juego queda como `eax_orig.dll`. |
| `CoopAndreas\` | Los scripts del modo cooperativo (`main.scm`, `script.img`). No reemplazan los del juego. |
| `SERVIDOR\server.exe` | El servidor, para el que hostea la partida. Su configuración es `SERVIDOR\server-config.ini`. |
| `ACTUALIZAR.bat` | El actualizador. |
| `MODO_SCRIPTS_ORIGINALES.bat` | Pasa el mod a usar los scripts originales del juego. |
| `CoopAndreas_crashes\` | Reportes de crashes, por si hay que mandarlos. |

## Jugar

- **El que hostea** abre `SERVIDOR\server.exe` y después el juego con `CoopSA.exe`.
- **Los demás** abren el juego con `CoopSA.exe` y se conectan con la IP y el puerto del que hostea.

## Mods

`CoopSA.exe` revisa los mods instalados antes de abrir el juego y avisa cuáles rompen el modo cooperativo. Los mods que reemplazan `main.scm` o `script.img` no son compatibles.

## Compilar

Lo compila GitHub Actions en cada push (`.github/workflows/ci.yml`) y lo publica como release. Para compilar en la PC: [xmake](https://xmake.io), Visual Studio 2022 con C++ (x86) y Rust; después `xmake build client server proxy launcher`.
