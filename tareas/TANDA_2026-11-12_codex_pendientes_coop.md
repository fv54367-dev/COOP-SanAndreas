# Pendientes de juego del 12/11

Base actual: main 3c7760f. Rama codex/pendientes-coop, separada de codex/crash-streaming-vehiculo. Protocolo 0.4.45-alpha.

1. **Tutorial de muerte y arresto.** Opción en F1 para omitir los avisos de primera vez. Identificar en el script original el tramo que solo muestra el tutorial. Con opción activa, verificar hospital, comisaría y fallo de la misión si un participante coop muere o es arrestado. Con la opción apagada, probar el tutorial normal.
2. **Policías en moto.** Reproducir con el perseguido y un observador. Revisar CDriveBy y la sincronización de NPC en vehículos para confirmar dónde se pierde la animación. Verificar que ambos ven el disparo y que el observador no aplica daño adicional.
3. **Reclutamiento de Grove Street.** Reproducir el reclutamiento, seguimiento con G y expulsión manteniendo H, primero con host y luego con invitado. Comparar el grupo y las tareas del peatón en ambas instancias. El blip verde solo confirma la interfaz, no que el seguidor tenga la tarea de seguir.
4. **Life's a Beach.** CCoopLowrider.cpp suma los incrementos recibidos directamente a la variable que comparte DANCE con MUSIC1. Falta aislar de dónde proviene el puntaje que sigue subiendo. Comprobar que cada aporte cuenta una vez, que el total queda fijo al cerrar el baile y que el umbral de MUSIC1 es 2500 por participante: 2500/5000/7500/10000. Confirmar qué se muestra y qué cuenta si un invitado abandona antes de comenzar o se desconecta durante el baile. Mantener scripts originales.
5. **Garajes.** CGarageSync (T90) filtra por tipo de puerta y por F1 individual. Ampliar la cobertura visual para todos, incluso al acercarse después desde otra zona. Verificar por separado garajes de misión, casa, pintura y tuning; el estado visual de una puerta no debe ejecutar cobros, guardar autos ni iniciar una escena en otro jugador.

## Avance

- 1. Implementado con opción F1; pendiente de prueba en juego.
- 2. Implementado para los overlays de drive-by del policía en COPBIKE; pendiente de prueba con observador.
- 3. Implementado el traspaso de autoridad al reclutador para el seguimiento G y la expulsión H. Pendiente de prueba en juego con host e invitado.
- 4. Implementado: la puntuación de invitado queda separada del contador DANCE y el resultado original de MUSIC1 usa 2500 por bailarín activo. Pendiente de prueba en juego.
- 5. Implementado el movimiento remoto de puertas de servicios y de misión, con el último estado para conexiones tardías. Pendiente de prueba en juego.
Cliente, servidor y testbot compilados. ENet local: 15 escenarios y 133 chequeos PASS. Falta la prueba en GTA.
