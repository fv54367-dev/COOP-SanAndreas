// Endpoint codec regression checks (no network or game required).
#include "ConnectionCode.h"
#include <cassert>
#include <cstdio>
int main() {
    uint32_t ip = 0; uint16_t port = 0;
    for (uint32_t i = 1; i < 10000; ++i) {
        auto code = ConnectionCode::Encode(i * 429497u, static_cast<uint16_t>(i));
        assert(ConnectionCode::Decode(code, ip, port));
        assert(ip == i * 429497u && port == i);
        assert(ConnectionCode::Decode("SA-" + code, ip, port));
    }
    assert(!ConnectionCode::Decode("", ip, port));
    assert(!ConnectionCode::Decode(std::string(1000, '1'), ip, port));
    assert(!ConnectionCode::Decode("ZZZZ-ZZZZ-ZZZZ", ip, port));
    assert(!ConnectionCode::Decode(ConnectionCode::Encode(0, 1234), ip, port));
    assert(!ConnectionCode::Decode(ConnectionCode::Encode(1234, 0), ip, port));
    auto code = ConnectionCode::Encode(0x08080808, 65535);
    code.back() = '1';
    assert(!ConnectionCode::Decode(code, ip, port));
    puts("connection code: PASS (9999 roundtrips, prefix, malformed, version, zero endpoint, unsupported key)");
}
