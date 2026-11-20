# Tanda 2026-11-14 Sonnet: tienda de ropa, trucos, animaciones F9
Rama `tanda/2026-11-14-tienda` (worktree `wt-tienda`). SIN REVISAR / SIN PROBAR / SIN INSTALAR.

## 1. Empleada de la tienda de ropa muerta = fracaso
- Mision: la de las tiendas de ropa con colores de pandilla es SWEET2 (habilita CSCHP/CSSPRT/LACS1). El script de tienda `CLOTH` crea a la empleada con `Char.Create(4, modelo 211/217)`; si la matan con el jugador en el probador el script no termina la animacion y el que se cambia queda trabado.
- Nuevo `client/src/CShopClerkFail.cpp` (llamado desde `CMissionMirror::OnScriptsProcessed`): solo la autoridad del script, con mision en curso y script `CLOTH` (ropa) o `BARB` (peluquero) activo, busca al ped de script con modelo de empleado mas cercano; si muere, `CCoopMission::RequestMissionFail()` (fracasa para todos) + aviso en chat y cartel. No toca comida/Ammu-Nation/tatuajes.
- Interruptor `mission-shop-clerk-fail=true`. No se tocaron reglas de SWEET1B ni INTRO (Big Smoke / Cleaning the Hood): la regla es general por script de tienda, pero esas misiones no usan CLOTH/BARB.
## 2. Trucos
- Causa de "a veces no se ve": `CCheatSync` solo propagaba ~30 trucos de mundo, sin aviso; los demas (vida, armas, dinero, vehiculos, busqueda) y el menu F8 no anunciaban nada, y el aviso nativo se pisa con otros textos.
- Ahora `CCheatSync` envuelve los 92 slots de `CCheat::m_aCheatFunctions` (el juego los llama inline al teclear el codigo y el menu F8 los usa): cada truco aplicado se anuncia en el chat propio ("Vos activo: ...") y por paquete nuevo `CHEAT_ANNOUNCE` (al final de `packet_types.h`, sin subir version; server lo reenvia con el emisor) a los demas. Los trucos recibidos de red no se re-anuncian. Interruptor `cheat-announce=true`.
## 3. Animaciones F9 de a dos
- Causa: el server (`SocialReady`) exigia `m_bSocialControls`, que arranca en false y solo se actualizaba con el paquete de teclas (que solo viaja si cambia el pad). Ahora se actualiza tambien con cada `PLAYER_ONFOOT_UPDATE` (trae `keySnapshot.controlsDisabled`). Ademas el rechazo distingue motivos (5 yo ocupado, 6 el otro ocupado, 7 lejos/otra area) en vez del texto generico "mision en curso". Sin cambio de protocolo (reason ya era un byte). Requiere recompilar server.
- No pude confirmar con partida real si habia otra causa (p. ej. `m_bOnMission` viejo del host); el nuevo texto de motivo lo dira.
## Pruebas simples
- [ ] SWEET2: entrar a la tienda de ropa y matar a la empleada con otro jugador en el probador: la mision fracasa para todos y nadie queda trabado.
- [ ] Barberia en mision: matar al peluquero fracasa; fuera de mision no pasa nada.
- [ ] Teclear HESOYAM / un truco de clima o trafico / usar el menu F8: todos ven "<nombre> activo: <truco>" en el chat y vos "Vos activo: ...".
- [ ] `cheat-announce=false`: sin avisos.
- [ ] F9 de a dos a menos de 4 m, a pie, sin mision: ya no rechaza; si rechaza, el texto dice el motivo.
## No pude
- Compilar: el coordinador pidio parar; el client llego a compilar casi todo (error de m_nPedState corregido a m_ePedState sin recompilar). Server y testbot sin compilar (cambios chicos: players.cpp, scripts.cpp, world.cpp). Probar en juego.
