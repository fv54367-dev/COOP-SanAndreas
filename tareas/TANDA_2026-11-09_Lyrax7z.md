# Tanda 2026-11-09 Lyrax7z: claxon de conductores remotos

Objetivo: corregir el camino de audio del claxon recibido, reportado inaudible del invitado al host.
Archivos reservados: client/src/Hooks/VehicleHooks.cpp, client/src/PacketHandlers/vehicles.cpp,
client/src/CConfigLoader.cpp, client/src/CConfigLoader.h, INTERRUPTORES.md,
ESTADO.md, TESTEAR_HOY.md y esta tanda. Sin SCM, protocolo, GTA ni dist.

Casos borde: 3-4 conductores independientes y cambio de conductor; host e invitados en ambos sentidos.
F1 individual conserva el audio de proximidad; interior/distancia usan la atenuacion nativa.
Muerte/desconexion/salida del conductor no deben reaplicar un snapshot ajeno; validar vehiculo y emisor.
Probar toques cortos, mantener/soltar, motos, sirenas y lowriders; sin cambios de teclas/hidraulica.
Interruptor remote-horn-sync=false restaura el camino anterior en el receptor.

Evidencia: gta-reversed Automobile.cpp CAutomobile::PlayCarHorn (0x6A3770) tiene azar y temporizador;
Vehicle.cpp ProcessSirenAndHorn usa el pad y puede reescribir el contador;
AEVehicleAudioEntity.cpp GetHornState (0x4F61E0) usa patrones NPC salvo STATUS_PLAYER.
El servidor reenvia VehicleDriverUpdate.horn a todos excepto al emisor; no filtra por host.
Referencias: https://github.com/gta-reversed/gta-reversed (archivos consultados en .cache/gta-reversed).
La asimetria reportada NO esta reproducida en juego en esta PC; es un arreglo candidato.

Aceptacion: compilar client/server, CRLF y diff --check; cambios limitados al receptor.
Riesgo a revisar: STATUS_PLAYER temporal durante Service puede afectar tambien el audio de motor remoto;
se restaura antes de ProcessControl y se mantiene el contexto local de radio existente.
Revision cruzada y prueba en juego pendientes. Resumen <=10 lineas.

Resultado: implementado candidato; client -r OK (33.187 s), server -r OK (7.515 s).
CRLF en los cuatro C++ y git diff --check OK. SIN REVISAR / SIN PROBAR.
Sin instalacion en GTA ni dist ni cambios de protocolo.

## Publicacion candidata autorizada por Lyrax7z
Ampliacion: shared/config.h (solo COOPANDREAS_BUILD=2026-11-09.1), dist/CoopAndreasSA.dll,
dist/server.exe y documentacion. Instalar DLL/servidor en el GTA indicado por el usuario con respaldo.
Publicar exclusivamente en la rama de Lyrax7z y preparar PR; main y el actualizador estable no cambian.
Sin cambio de protocolo; estado SIN REVISAR / SIN PROBAR en juego.

Publicacion candidata: client OK (37.094 s), server OK (8.953 s); DLL y servidor instalados
en el GTA indicado con respaldo en _backup_mod/antes-claxon-2026-11-09.1-171841.
Hashes build/instalacion/dist iguales. Sin abrir GTA; SIN REVISION CRUZADA / SIN PROBAR EN JUEGO.

## Actualizador de prueba
Alcance adicional: dist/ACTUALIZAR_PR_CLAXON.bat y esta tanda. Descarga fija del commit
b2f582843c10f0e80754de3d2d7664e5e3315235 (build 2026-11-09.1), respaldo y SHA256.
No reemplaza ACTUALIZAR.bat estable ni integra el PR a main.

Actualizador probado de punta a punta en el GTA de Lyrax7z: descarga privada del commit fijado,
SHA256 correcto, respaldo e instalacion OK. Get-FileHash no estaba disponible en Windows PowerShell
desde este entorno; se usa SHA256 de .NET. El juego no fue abierto: claxon SIN PROBAR.

Integracion para prueba: main avanzo a cd4ed98, protocolo 0.4.19-alpha.
Se incorporan sus cambios en esta rama, conservando ambas entradas de documentacion.
Nueva candidata 2026-11-09.2 y actualizador fijado a sus binarios; sin tocar main.

Resultado integracion: client/server/testbot OK (29.657/7.891/2.797 s).
ACTUALIZAR_PR_CLAXON.bat descarga binarios del commit 8d3ad7403a27a1fcb02b5ccbe25c965ba151c1cf
(build 2026-11-09.2, protocolo 0.4.19-alpha); SHA256 y respaldo/instalacion probados OK en GTA local.
Claxon SIN PROBAR EN JUEGO, revision cruzada pendiente; sin modificar main.
