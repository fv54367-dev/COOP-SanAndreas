#pragma once
#include <windows.h>
#include <string>
#include <cstdarg>
#include <cstdio>
#include "CMissionMirror.h"
inline std::string SceneFormat(const char* fmt, ...) {
 char b[2048]; va_list args; va_start(args,fmt); vsnprintf(b,sizeof b,fmt,args); va_end(args); return b;
}
inline std::string WideToUtf8(const std::wstring& s) {
 int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,nullptr,0,nullptr,nullptr);
 std::string r(n,0); if(n) WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,r.data(),n,nullptr,nullptr); return r;
}
#define LOGD(...) ((void)0)
#define LOGI(...) CMissionMirror::LogEventf(__VA_ARGS__)
#define LOGW(...) CMissionMirror::LogEventf(__VA_ARGS__)
#define LOGE(...) CMissionMirror::LogEventf(__VA_ARGS__)

inline std::string ToLower(std::string s) { for(auto& c:s) c=char(tolower((unsigned char)c)); return s; }

inline uint64_t NowMs() { return GetTickCount64(); }
