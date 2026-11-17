#pragma once
#include <cstdint>

class CRunningScript;

class CMissionRulesWorld
{
public:
    static bool Badlands(uint16_t opcode);
    static bool Countryside(CRunningScript* script, uint16_t opcode);
};
