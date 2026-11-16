#pragma once

// Diagnostics for the bugs that leave no crash log.
//  - Hang: a thread watches CTimer::m_FrameCounter (0xB7CB4C, +1 in CTimer::Update every frame, menus included:
//    gta-reversed Timer.cpp). If it stops for 15 s the game thread is suspended, its registers and stack are copied,
//    it is resumed and the report goes to CoopAndreas_crashes\hang_<date>.log (EIP, EBP chain, return addresses of
//    gta_sa.exe and of our dll found on the stack). Nothing is allocated while the game thread is suspended.
//  - Write watch (heap corruption hunting): if CoopAndreas_watch.txt in the game folder holds an address (hex, 4-byte
//    aligned), a hardware breakpoint on writes to those 4 bytes is set on the game thread; every write is logged in
//    CoopAndreas_watch.log with the instruction after it and the return address (first 500 writes).
class CHangWatchdog
{
public:
    // first call from the game thread starts the watchdog (later calls do nothing)
    static void Start();
};
