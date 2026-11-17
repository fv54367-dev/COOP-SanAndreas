#pragma once

// COOP_VERIFY(expr): precondition check that also exists in release builds.
//
// Debug (NDEBUG unset): exactly like assert(); the game stops at the failure.
// Release: writes a line to CoopAndreas_verify.log next to gta_sa.exe and keeps
// going, so a broken precondition never becomes a silent write into the game's
// heap. The first MaxHits failures of each site are written; one broken check in
// a per-frame path cannot fill the disk.

#include <Windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>

#if defined(NDEBUG)

namespace coop_verify
{
// Per site, per process.
constexpr unsigned MaxHits = 8;

inline void Report(const char* file, int line, const char* expr, unsigned hits)
{
    char path[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) == 0)
    {
        return;
    }

    char* name = strrchr(path, '\\');
    if (name == nullptr)
    {
        return;
    }
    strcpy_s(name + 1, MAX_PATH - static_cast<size_t>(name + 1 - path), "CoopAndreas_verify.log");

    FILE* out = fopen(path, "a");
    if (out == nullptr)
    {
        return;
    }

    char text[640];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "t=%u %s:%d: CHECK FAILED: %s%s\n",
        GetTickCount(), file, line, expr, hits == MaxHits ? " (no more from this site)" : "");
    fputs(text, out);
    fclose(out);
    OutputDebugStringA(text);
}
}

#define COOP_VERIFY(expr)                                                          \
    do                                                                             \
    {                                                                              \
        if (!(expr))                                                               \
        {                                                                          \
            static unsigned coop_verify_hits = 0;                                  \
            if (coop_verify_hits < ::coop_verify::MaxHits)                         \
            {                                                                      \
                ++coop_verify_hits;                                                \
                ::coop_verify::Report(__FILE__, __LINE__, #expr, coop_verify_hits); \
            }                                                                      \
        }                                                                          \
    } while (0)

#else

#define COOP_VERIFY(expr) assert(expr)

#endif
