#pragma once
// Adapted from Karimov-Akbar/sa-coop NatTraversal.cpp (MIT).
#include <string>
#include <cstdint>
#include <cstring>
#include <cctype>
namespace ConnectionCode {
static const char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

static int DecodeChar(char c) {
    c = (char)toupper((unsigned char)c);
    if (c == 'O') c = '0';
    if (c == 'I' || c == 'L') c = '1';
    const char* p = strchr(kAlphabet, c);
    return p && c ? (int)(p - kAlphabet) : -1;
}

inline std::string Encode(uint32_t ip, uint16_t port) {
    // 60 bits: [version:4][ip:32][port:16][key:8]
    const uint8_t* b = (const uint8_t*)&ip;
    uint64_t v = 1;  // version 1
    for (int i = 0; i < 4; ++i) v = (v << 8) | b[i];
    v = (v << 16) | port;
    v = (v << 8) | 0;
    std::string out;
    for (int i = 11; i >= 0; --i) out += kAlphabet[(v >> (i * 5)) & 31];
    return out.substr(0, 4) + "-" + out.substr(4, 4) + "-" + out.substr(8, 4);
}

inline bool Decode(const std::string& code, uint32_t& ip, uint16_t& port) {
    uint64_t v = 0;
    int n = 0;
    if (code.size() > 32) return false;
    std::string c = code;
    if (c.size() > 3 && (c.rfind("SA-", 0) == 0 || c.rfind("sa-", 0) == 0)) c = c.substr(3);
    for (char ch : c) {
        if (ch == '-' || ch == ' ') continue;
        int d = DecodeChar(ch);
        if (d < 0) return false;
        if (n >= 12) return false;
        v = (v << 5) | (uint64_t)d;
        ++n;
    }
    if (n != 12) return false;
    if ((v >> 56) != 1) return false;  // version
    if ((v & 255) != 0) return false;
    port = (uint16_t)((v >> 8) & 0xFFFF);
    uint32_t ipHost = (uint32_t)((v >> 24) & 0xFFFFFFFF);
    uint8_t b[4] = {(uint8_t)(ipHost >> 24), (uint8_t)(ipHost >> 16), (uint8_t)(ipHost >> 8), (uint8_t)ipHost};
    memcpy(&ip, b, 4);
    return ip != 0 && port != 0;
}

}
