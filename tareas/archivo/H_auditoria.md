# Tarea H — Auditoría (SOLO LECTURA)

Repo: `E:\juegos\gtasacoopnuevo\coopandreas-server` (mod coop GTA SA: cliente DLL `client/src`, server `server/src`). NO modifiques ningún archivo del repo y NO compiles (hay otra tarea compilando). Lo único que podés escribir es `E:\juegos\gtasacoopnuevo\AUDITORIA_NOCHE.md`.

Revisá estos commits (usá `git show <hash>`; los archivos grandes leelos por partes):
- `66a571a` (población por zonas y policía: hooks de memoria)
- `6f43591` (animaciones con nombre en interiores, traspaso de interiores)
- `94ab474` (host que se va, guardado, consola del server con un hilo)
- `ad63345` (blips de misión)
- `107f04a` y `dd5ca29` (misión coop con dueño: server y cliente)
- `3dc670f` (opcode 1D2A)

Buscá SOLO bugs reales y concretos:
- crashes: punteros nulos o colgados (entidades del juego borradas mientras se guarda su puntero, `CNetworkPlayer*` que se borra al desconectarse y queda referenciado), índices fuera de rango, strings sin `\0`;
- concurrencia: el hilo de consola del server tocando estructuras del loop de red sin mutex;
- lógica: con el interruptor apagado el comportamiento NO es igual al de antes; el host deja de ver o mandar algo que antes veía (cambios `m_bIsHost` → `IsScriptAuthority`); estados que nunca se limpian (misión coop que queda "en curso" para siempre);
- protocolo: un paquete que el server reenvía sin validar que quien lo manda tenga permiso.

Para cada hallazgo: severidad (CRASH / BUG / MENOR), archivo:línea, escenario concreto que lo dispara y arreglo propuesto en 1-3 líneas. No reportes estilo ni sugerencias generales. Si no estás seguro, marcalo como "dudoso". Máximo 25 hallazgos, ordenados por severidad.

Respondé con un resumen de ≤8 líneas (cantidad por severidad).
