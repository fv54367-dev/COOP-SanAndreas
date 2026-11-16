#pragma once

#if defined(_WIN32)
#include <windows.h>
#endif

// text of the server-written chat lines (SERVER_CHAT) is UTF-16 on the wire, the server has plain `char*`
inline void ServerTextToWide(const char* text, wchar_t* out, size_t outCount)
{
    if (outCount == 0)
        return;

#if defined(_WIN32)
    // the console (and the rest of the server) works in the ANSI codepage
    if (MultiByteToWideChar(CP_ACP, 0, text, -1, out, (int)outCount) == 0)
        out[0] = L'\0';
#else
    if (mbstowcs(out, text, outCount) == (size_t)-1)
        out[0] = L'\0';
#endif

    out[outCount - 1] = L'\0';
}
