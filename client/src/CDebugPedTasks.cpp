#include "stdafx.h"

// thx Vektor for his ped task debugger
// Task id -> name table, kept out of this file: it is 900 lines of data.
#include "CDebugPedTasks_TaskNames.inc"

void CDebugPedTasks::Draw()
{
	for (auto ped : CPools::ms_pPedPool)//(auto networkPed : CNetworkPedManager::m_pPeds)
	{
		/*if (!networkPed)
			continue;

		CPed* ped = networkPed->m_pPed;*/
		if (!ped)
			continue;

		CVector& posn = ped->GetPosition();
		RwV3d screenCoors; float w, h;
		if (CSprite::CalcScreenCoors({ posn.x, posn.y, posn.z + 1.0f }, &screenCoors, &w, &h, true, true))
		{
			char text[1000];
			char level[60];

			//sprintf(text, "Ped: %s\nPRIMARY TASKS:\n", to_string(networkPed->m_pedId).c_str());
			sprintf(text, "PRIMARY TASKS:\n");

			for (int i = 0; i < 5; i++)
			{
				CTask* task = ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[i];

				sprintf(level, "");
				while (task)
				{
					sprintf(text, "%s%s%s\n", text, level, TaskNames[task->GetTaskType()]);
					sprintf(level, "%s--", level);
					task = task->GetSubTask();
				}
			}

			sprintf(text, "%s\nSECONDARY TASKS:\n", text);

			for (int i = 0; i < 5; i++)
			{
				CTask* task = ped->m_pIntelligence->m_TaskMgr.m_aSecondaryTasks[i];

				sprintf(level, "");
				while (task)
				{
					sprintf(text, "%s%s%s\n", text, level, TaskNames[task->GetTaskType()]);
					sprintf(level, "%s--", level);
					task = task->GetSubTask();
				}
			}

			CDXFont::Draw((int)screenCoors.x, (int)screenCoors.y, text, D3DCOLOR_ARGB(255, 255, 255, 255));
		}
	}
}

