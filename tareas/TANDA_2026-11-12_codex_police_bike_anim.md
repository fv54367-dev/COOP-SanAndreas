# Animación de disparos en la moto policial

Archivos: CNetworkPed.cpp, CNetworkPedManager.cpp, ESTADO.md, TESTEAR_HOY.md y esta tanda.
Reutilizar PED_ANIM_SET, solo overlays de drive-by del policía en COPBIKE. Sin protocolo nuevo; npc-anim-mirror apagado conserva el comportamiento anterior.
Casos borde: 2–4 observadores, salida de la moto, cambio de autoridad, muerte y streaming. Conservar animación base de asiento y autoridad de daño. La animación sigue siendo visible en modo individual, igual que el NPC.
Aceptación: el perseguido y el observador ven el disparo, y al parar vuelve la pose nativa de la moto. Resumen de hasta 10 líneas.

Cliente compilado. Pendiente prueba en juego.
