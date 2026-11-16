#include "stdafx.h"
#include "CHangWatchdog.h"
#include <windows.h>

static constexpr DWORD HANG_TIMEOUT_MS = 15000;
static constexpr DWORD HANG_REPEAT_MS = 60000;
static constexpr int STACK_COPY_DWORDS = 2048;
static constexpr int MAX_WATCH_LOGS = 500;

static volatile uint32_t* const pFrameCounter = reinterpret_cast<volatile uint32_t*>(0xB7CB4C);

static HANDLE hGameThread = nullptr;
static DWORD dwGameThreadId = 0;
static uintptr_t dwDllBase = 0;
static uintptr_t dwDllEnd = 0;
static uintptr_t dwExeEnd = 0;

static uintptr_t dwWatchAddress = 0;
static volatile LONG nWatchLogs = 0;

// copied while the game thread is suspended, formatted after it is resumed
static CONTEXT hangContext;
static DWORD hangStack[STACK_COPY_DWORDS];
static int nHangStackDwords = 0;
static DWORD hangFrames[32];
static int nHangFrames = 0;

static void WriteRaw(HANDLE file, const char* text)
{
    DWORD written;
    WriteFile(file, text, lstrlenA(text), &written, nullptr);
}

static bool IsCodeAddress(uintptr_t address)
{
    return (address >= 0x401000 && address < dwExeEnd) || (address >= dwDllBase && address < dwDllEnd);
}

static void FormatAddress(char* out, uintptr_t address)
{
    if (address >= dwDllBase && address < dwDllEnd)
        wsprintfA(out, "%08X (dll+%X)", (DWORD)address, (DWORD)(address - dwDllBase));
    else
        wsprintfA(out, "%08X", (DWORD)address);
}

// raw copy of [src, src + count) that stops at the first unreadable page
static int SafeCopyDwords(DWORD* dst, uintptr_t src, int count)
{
    int copied = 0;
    __try
    {
        for (; copied < count; copied++)
            dst[copied] = reinterpret_cast<const DWORD*>(src)[copied];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return copied;
}

static void CaptureGameThread()
{
    nHangStackDwords = 0;
    nHangFrames = 0;
    if (SuspendThread(hGameThread) == (DWORD)-1)
        return;

    hangContext = {};
    hangContext.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(hGameThread, &hangContext))
    {
        nHangStackDwords = SafeCopyDwords(hangStack, hangContext.Esp, STACK_COPY_DWORDS);

        // EBP chain: [ebp] = caller's ebp, [ebp + 4] = return address
        uintptr_t ebp = hangContext.Ebp;
        while (nHangFrames < 32 && ebp >= hangContext.Esp && ebp < hangContext.Esp + 0x100000)
        {
            DWORD frame[2];
            if (SafeCopyDwords(frame, ebp, 2) != 2)
                break;
            hangFrames[nHangFrames++] = frame[1];
            if (frame[0] <= ebp)
                break;
            ebp = frame[0];
        }
    }
    ResumeThread(hGameThread);
}

static void WriteHangReport(DWORD stalledMs, uint32_t frame)
{
    CreateDirectoryA("CoopAndreas_crashes", nullptr);

    SYSTEMTIME now;
    GetLocalTime(&now);
    char path[MAX_PATH];
    wsprintfA(path, "CoopAndreas_crashes\\hang_%04d-%02d-%02d_%02d-%02d-%02d.log", now.wYear, now.wMonth, now.wDay,
        now.wHour, now.wMinute, now.wSecond);

    HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;

    char line[256], a[48];
    wsprintfA(line, "Game thread stalled for %u ms (frame counter %u). dll base %08X\r\n", stalledMs, frame, (DWORD)dwDllBase);
    WriteRaw(file, line);

    FormatAddress(a, hangContext.Eip);
    wsprintfA(line, "EIP %s\r\nEAX %08X EBX %08X ECX %08X EDX %08X\r\nESI %08X EDI %08X EBP %08X ESP %08X\r\n", a,
        hangContext.Eax, hangContext.Ebx, hangContext.Ecx, hangContext.Edx, hangContext.Esi, hangContext.Edi,
        hangContext.Ebp, hangContext.Esp);
    WriteRaw(file, line);

    WriteRaw(file, "\r\nEBP chain (return addresses):\r\n");
    for (int i = 0; i < nHangFrames; i++)
    {
        FormatAddress(a, hangFrames[i]);
        wsprintfA(line, "  #%d %s\r\n", i, a);
        WriteRaw(file, line);
    }

    WriteRaw(file, "\r\nCode addresses on the stack (ESP+offset):\r\n");
    int listed = 0;
    for (int i = 0; i < nHangStackDwords && listed < 64; i++)
    {
        if (!IsCodeAddress(hangStack[i]))
            continue;
        FormatAddress(a, hangStack[i]);
        wsprintfA(line, "  +%04X %s\r\n", i * 4, a);
        WriteRaw(file, line);
        listed++;
    }
    CloseHandle(file);
}

static LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* info)
{
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || (info->ContextRecord->Dr6 & 1) == 0 || !dwWatchAddress)
        return EXCEPTION_CONTINUE_SEARCH;

    info->ContextRecord->Dr6 = 0;
    if (InterlockedIncrement(&nWatchLogs) <= MAX_WATCH_LOGS)
    {
        // no heap here: the write may come from inside the allocator
        HANDLE file = CreateFileA("CoopAndreas_watch.log", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD ret = 0, value = 0;
            SafeCopyDwords(&ret, info->ContextRecord->Esp, 1);
            SafeCopyDwords(&value, dwWatchAddress, 1);
            char line[160], a[48], b[48];
            FormatAddress(a, info->ContextRecord->Eip);
            FormatAddress(b, ret);
            wsprintfA(line, "t=%u frame=%u value=%08X after=%s [esp]=%s\r\n", GetTickCount(), *pFrameCounter, value, a, b);
            WriteRaw(file, line);
            CloseHandle(file);
        }
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void ReadWatchAddress()
{
    HANDLE file = CreateFileA("CoopAndreas_watch.txt", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    char text[32] = {};
    DWORD read = 0;
    ReadFile(file, text, sizeof(text) - 1, &read, nullptr);
    CloseHandle(file);

    const uintptr_t address = strtoul(text, nullptr, 16);
    if (address >= 0x10000 && (address & 3) == 0)
        dwWatchAddress = address;
}

static void ArmWatch()
{
    if (!dwWatchAddress || SuspendThread(hGameThread) == (DWORD)-1)
        return;

    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(hGameThread, &ctx))
    {
        ctx.Dr0 = dwWatchAddress;
        ctx.Dr6 = 0;
        // L0 enabled, RW0 = 01 (writes), LEN0 = 11 (4 bytes)
        ctx.Dr7 = (ctx.Dr7 & ~0x000F0003u) | 0x1u | (0x1u << 16) | (0x3u << 18);
        SetThreadContext(hGameThread, &ctx);
    }
    ResumeThread(hGameThread);

    HANDLE file = CreateFileA("CoopAndreas_watch.log", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        char line[96];
        wsprintfA(line, "watch armed on %08X, dll base %08X\r\n", (DWORD)dwWatchAddress, (DWORD)dwDllBase);
        WriteRaw(file, line);
        CloseHandle(file);
    }
}

static DWORD WINAPI WatchdogThread(LPVOID)
{
    ArmWatch();

    uint32_t lastFrame = *pFrameCounter;
    DWORD lastChange = GetTickCount();
    DWORD lastReport = 0;
    for (;;)
    {
        Sleep(1000);
        const uint32_t frame = *pFrameCounter;
        const DWORD now = GetTickCount();
        if (frame != lastFrame)
        {
            lastFrame = frame;
            lastChange = now;
            lastReport = 0;
            continue;
        }

        const DWORD stalled = now - lastChange;
        if (stalled >= HANG_TIMEOUT_MS && (!lastReport || now - lastReport >= HANG_REPEAT_MS))
        {
            lastReport = now;
            CaptureGameThread();
            WriteHangReport(stalled, frame);
        }
    }
}

void CHangWatchdog::Start()
{
    if (hGameThread)
        return;

    dwGameThreadId = GetCurrentThreadId();
    hGameThread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
        FALSE, dwGameThreadId);
    if (!hGameThread)
        return;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(0x400000);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(0x400000 + dos->e_lfanew);
    dwExeEnd = 0x400000 + nt->OptionalHeader.SizeOfImage;

    HMODULE dll = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&CHangWatchdog::Start), &dll))
    {
        const auto* dllDos = reinterpret_cast<const IMAGE_DOS_HEADER*>(dll);
        const auto* dllNt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<uintptr_t>(dll) + dllDos->e_lfanew);
        dwDllBase = reinterpret_cast<uintptr_t>(dll);
        dwDllEnd = dwDllBase + dllNt->OptionalHeader.SizeOfImage;
    }

    ReadWatchAddress();
    if (dwWatchAddress)
        AddVectoredExceptionHandler(1, WatchHandler);

    CloseHandle(CreateThread(nullptr, 0, WatchdogThread, nullptr, 0, nullptr));
}
