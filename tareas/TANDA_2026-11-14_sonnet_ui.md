# TANDA 2026-11-14 sonnet UI (rama tanda/2026-11-14-ui)

## Que se hizo
1. Oferta de carrera VS: `CVsLobby::DrawHelpBox` (llamada desde el drawingEvent de Main.cpp) dibuja el texto con CFont en una caja negra arriba a la izquierda (x=20, y=20, ancho 200 en 640x448), como la ayuda del juego. La ventana ImGui vieja queda solo si el interruptor esta apagado.
2. Carrera VS: se quita el panel viejo de puesto/vueltas (abajo a la derecha, `CVsRace::DrawHud`); queda la tabla POSICIONES (que baja a ese lugar) y la tabla final de resultados. Mismo interruptor `vs-race-board`.
3. Voz: menu F1 > "Configurar chat de voz": microfono (lista de dispositivos de entrada, guardado por nombre), volumen del microfono, volumen de las voces (0-200%), modo mantener/alternar F4, barra de nivel. Se guarda en `UserFiles\coopandreas.ini`. No hay tecla nueva (se usa F1).

## Interruptores nuevos
- `vs-lobby-helpbox` (true): caja de ayuda arriba a la izquierda; false = ventana vieja.
- `voice-mic-device` (""): microfono por nombre; vacio = predeterminado.
- `voice-mic-volume` / `voice-out-volume` (100): porcentaje; 100 = igual que antes.
- `voice-toggle` (false): false = mantener F4; true = F4 alterna.

## Pruebas
- [ ] Carrera VS: el que ofrece ve la caja negra arriba a la izquierda con "Y: VS / N: correr solo"
- [ ] Carrera VS: el invitado ve "X te invita a una carrera VS" arriba a la izquierda y Y/N funciona
- [ ] Carrera VS: la lista de jugadores esperando se ve bien con 3 y 4 jugadores y no se sale de la caja
- [ ] Durante la carrera ya no aparece el cartel viejo abajo a la derecha con puesto y vueltas
- [ ] La tabla POSICIONES se ve abajo a la derecha y al terminar aparece la tabla de resultados
- [ ] F1 > Configurar chat de voz: aparece la lista de microfonos y cambiar de uno funciona la proxima vez que hablas
- [ ] Subir/bajar el volumen del microfono se nota en el otro jugador; la barra de nivel se mueve al hablar
- [ ] Subir/bajar el volumen de las voces se nota al escuchar al otro
- [ ] Modo "F4 enciende y apaga": F4 una vez habla, otra vez corta
- [ ] Cerrar y abrir el juego: la configuracion de voz se mantiene

## Sin hacer / dudas
- Client compila (build ok, `xmake build client`); server/testbot no se tocaron. Nada probado en juego. El tamano/posicion de la caja es una aproximacion de `CHud::DrawHelpText` (no esta reversado en gta-reversed), puede requerir ajuste fino.
- No se uso `CHud::SetHelpMessage` (lo usa el sistema de esperas de CMissionHints y se pisarian).
- No se elige dispositivo de salida; no hay cambio de protocolo.
