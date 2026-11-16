#pragma once
#include <cstdint>

// Debug export for automated tests (sa-mcp plugin_query): writes a JSON snapshot of the local net state into buf.
// Returns the length it needs; nothing is written when cap is too small.
extern "C" __declspec(dllexport) uint32_t sa_debug_json(char* buf, uint32_t cap);
