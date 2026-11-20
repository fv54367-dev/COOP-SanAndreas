# Crash al cargar un modelo de vehículo

Objetivo: corregir el agotamiento de VehicleStruct observado el 12/11/2026 a las 01:03.
Archivos: client/src/CPatch.cpp, ESTADO.md, TESTEAR_HOY.md y esta tanda.
Conservar CRLF del C++, presupuesto de streaming y protocolo. Sin tocar SCM.
Casos borde: la capacidad cubre los 212 modelos estándar aunque haya muchos modelos en uso simultáneo con 3–4 jugadores. Modo individual, desconexión y cambio de interior siguen usando el constructor y el ciclo de vida nativos.
No se introduce un sistema ni interruptor. Se corrige el tamaño de un pool que el mod ya modifica.
Aceptar si compila, la llamada original conserva ABI y el cliente muestra capacidad de 256; validar en juego con streaming largo y 2–4 clientes.
Resumen de hasta 10 líneas.

## Evidencia
El dump registra escritura a 0x30 desde 0x4C8F24. CVehicleModelInfo::SetClump llamó a CPool::New y guardó null en el campo +0x5C; PreprocessHierarchy intentó guardar una posición dummy. El registro de modelos apunta a Turismo (451). VehicleStruct 127/127 según log y 127 flags ocupados en Memory64List; Vehicle 4/255. El ctor nativo 0x5B8340 recibe un entero de 32 bits, reserva size × 0x314 y usa el mismo tamaño para sus flags. El SDK local confirma estructura 0x314, puntero global 0xB4E680 y 15 dummy positions. La copia local de gta-reversed no incluye ModelInfo; comprobado contra el exe y el SDK.
Referencia adicional: https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Models/VehicleModelInfo.cpp (SetClump y PreprocessHierarchy). Client x86 release compilado. Pendiente prueba de juego y revisión del diff.
