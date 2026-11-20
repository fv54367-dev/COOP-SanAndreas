# Tanda 2026-11-09 paquetes: canales automaticos por CI

Objetivo: que GitHub compile y publique cada rama como canal, que exista "todo junto" (main + PR abiertos) y un solo actualizador para elegir version.

Archivos reservados: `.github/workflows/ci.yml`, `.github/scripts/*`, `dist/ACTUALIZAR_CANALES.bat`, esta tanda. Limpieza del claxon ya confirmado: `ESTADO.md`, `TESTEAR_HOY.md`, `INTERRUPTORES.md`, `dist/ACTUALIZAR_PR_CLAXON.bat`.

Restricciones: no toca C++, protocolo ni `dist/ACTUALIZAR.bat` (sigue funcionando hasta validar esto). Las ramas no suben `COOPANDREAS_BUILD` a mano: el CI lo pone en cada canal, y si una rama lo cambia, choca en "todo junto".

Casos borde:
- Sin PR abiertos: se borra `canal-todo`, y el actualizador muestra solo lo que exista.
- PR que choca en codigo: queda afuera de "todo junto" y el titulo del canal lo nombra. Docs y `dist/` no cuentan como choque.
- PR cerrado: se borra su canal. Si no se fusiono, "todo junto" se rearma sin el.
- GTA o server abiertos: el actualizador no instala. Copia a medias: vuelve a lo anterior.
- Canal publicandose: falta un archivo o no coincide el hash, y no se instala nada.
- Interruptor: dejar de usar `ACTUALIZAR_CANALES.bat`; `ACTUALIZAR.bat` y `dist/` siguen igual.

Aceptacion: el CI publica `canal-<rama>` y `canal-todo` con etiqueta del commit; el actualizador instala en una carpeta de prueba, con hash, respaldo y archivo de version. Falta probarlo en juego desde un paquete del CI.
