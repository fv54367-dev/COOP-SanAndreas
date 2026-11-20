#include "stdafx.h"
#include "CHomeInvasion.h"
#include "CommandHooks.h"
#include "CMissionGuestCounts.h"
#include "CMissionGuestActions.h"
#include "CMissionPhotos.h"
#include "CMissionSceneWait.h"
#include "CCustomCommandMgr.h"
#include "Commands/CCommandAddChatMessage.h"

uint16_t nCommand = 0x0;
CRunningScript* pScript = nullptr;
static const uint8_t* commandIP = nullptr;

// 0 = run the original handler, 1 = a custom command that was run, 2 = hold: leave without running (see the hook)
int ProcessCustomCommand()
{
    // a cutscene or scripted scene waits for the players of the mission; the script yields and asks again next frame
    if (CMissionSceneWait::ShouldHold(pScript, nCommand & 0x7FFF))
        return 2;
    commandIP = pScript->m_pCurrentIP;
    CMissionSceneWait::OnCommandStart(pScript, nCommand & 0x7FFF);
    CHomeInvasion::OnCommandStart(pScript, nCommand & 0x7FFF);
    CMissionGuestActions::OnCommandStart(pScript, nCommand & 0x7FFF);
    CMissionGuestCounts::OnCommandStart(pScript, nCommand & 0x7FFF);
    CMissionPhotos::OnCommandStart(pScript, nCommand & 0x7FFF);
	// if is in custom command range
	uint16_t commandNormalised = (nCommand & 0x7FFF);
	if (commandNormalised >= CCustomCommandMgr::MIN_CUSTOM_COMMAND && commandNormalised <= CCustomCommandMgr::MAX_CUSTOM_COMMAND)
	{
		pScript->m_pCurrentIP += 2;
		pScript->m_bNotFlag = (nCommand & 0x8000) != 0;
		CCustomCommandMgr::ProcessCommand(commandNormalised, pScript); // process it
		return 1;
	}
	return 0;
}


void __declspec(naked) CRunningScript__Process_Hook() 
{
	__asm
	{
		mov ax, [ecx]

		mov nCommand, ax
		mov pScript, esi

		push eax
		push ecx
		push esi

		call ProcessCustomCommand

		cmp al, 2
		je hold_command
		test al, al
		jz process_orig_opcode 

		pop esi
		pop ecx
		pop eax
		push 0x469FB0
		ret

	hold_command:
		// the exit of CRunningScript::Process (0x469FFB: pop esi / xor al, al / pop ebx / ret): the instruction pointer was
		// not moved, so the same command is read again on the next frame
		pop esi
		pop ecx
		pop eax
		push 0x469FFB
		ret

	process_orig_opcode:
		pop esi
		pop ecx
		pop eax
		push 0x469FBF
		ret
	}
}

//uintptr_t CRunningScript__ProcessOneCommand_Ret = 0x469EBF;
//uintptr_t CRunningScript__ProcessOneCommand_Exit = 0x469EF8;
//void __declspec(naked) CRunningScript__ProcessOneCommand_Hook()
//{
//	__asm
//	{
//		xor eax, eax
//		mov ax, [edx]
//
//		mov nCommand, ax
//		mov pScript, ecx
//
//		pushad
//		pushfd
//	}
//
//	if (ProcessCustomCommand(nCommand, pScript))
//	{
//		__asm
//		{
//			popfd
//			popad
//
//			jmp CRunningScript__ProcessOneCommand_Exit
//		}
//	}
//
//	__asm
//	{
//		popfd
//		popad
//
//		jmp CRunningScript__ProcessOneCommand_Ret
//	}
//}

const uint8_t* CommandHooks::GetCurrentCommandIP()
{
    return commandIP;
}

void CommandHooks::InjectHooks()
{
	patch::RedirectJump(0x469FBA, CRunningScript__Process_Hook);
	///patch::RedirectJump(0x469EBA, CRunningScript__ProcessOneCommand_Hook);
}

CRunningScript* CommandHooks::GetCurrentScript()
{
	return pScript;
}

uint16_t CommandHooks::GetCurrentCommand()
{
	return nCommand;
}