/*
 * Half-Life Weapon Mod
 * Copyright (c) 2012 - 2014 AGHL.RU Dev Team
 * 
 * http://aghl.ru/forum/ - Russian Half-Life and Adrenaline Gamer Community
 *
 *
 *    This program is free software; you can redistribute it and/or modify it
 *    under the terms of the GNU General Public License as published by the
 *    Free Software Foundation; either version 2 of the License, or (at
 *    your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful, but
 *    WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software Foundation,
 *    Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *    In addition, as a special exception, the author gives permission to
 *    link the code of this program with the Half-Life Game Engine ("HL
 *    Engine") and Modified Game Libraries ("MODs") developed by Valve,
 *    L.L.C ("Valve").  You must obey the GNU General Public License in all
 *    respects for all of the code used other than the HL Engine and MODs
 *    from Valve.  If you modify this file, you may extend this exception
 *    to your version of the file, but you are not obligated to do so.  If
 *    you do not wish to do so, delete this exception statement from your
 *    version.
 *
 */

#include "wpnmod_config.h"
#include "wpnmod_entity.h"
#include "wpnmod_hooks.h"
#include "wpnmod_parse.h"


CConfig g_Config;

//int g_iWeaponsCount = 0;
int g_iAmmoBoxIndex = 0;

const char* gWeaponReference = "weapon_crowbar";
const char* gAmmoBoxReference = "ammo_rpgclip";

WeaponData WeaponInfoArray[MAX_WEAPONS];
AmmoBoxData AmmoBoxInfoArray[MAX_WEAPONS];

cvar_t* cvar_sv_cheats = NULL;
cvar_t* cvar_mp_weaponstay = NULL;

CConfig::CConfig()
{
	m_bInited = false;
	m_bCrowbarHooked = false;
	m_bAmmoBoxHooked = false;

	m_pCurrentSlots = NULL;
	m_iMaxWeaponSlots = 5;
	m_iMaxWeaponPositions = 5;

	m_bWpnBoxModels = false;
	m_iWpnBoxLifeTime = 120;
	m_iWpnBoxRenderColor = NULL;

	m_GameMod = SUBMOD_UNKNOWN;
	m_pEquipEnt = NULL;
};

void CConfig::InitGameMod(void)
{
	m_bInited = true;
	m_GameMod = CheckSubMod(MF_GetModname());

	pvData_Init();
	Vtable_Init();

	if (m_GameMod == SUBMOD_GEARBOX)
	{
		// More slots in OP4.
		m_iMaxWeaponSlots = 7;
	}
	else if (m_GameMod == SUBMOD_AGHLRU)
	{
		// More positions in Bugfixed and improved HL release.
		m_iMaxWeaponPositions = 10;
	}

	m_pCurrentSlots = new int* [m_iMaxWeaponSlots];

	for (int i = 0; i < m_iMaxWeaponSlots; ++i)
	{
		memset((m_pCurrentSlots[i] = new int [m_iMaxWeaponPositions]), 0, sizeof(int) * m_iMaxWeaponPositions);
	}

	cvar_sv_cheats = CVAR_GET_POINTER("sv_cheats");
	cvar_mp_weaponstay = CVAR_GET_POINTER("mp_weaponstay");

	cvar_t version = 
	{
		"hl_wpnmod_version",
		(char *)Plugin_info.version,
		FCVAR_SERVER
	};

	REG_SVR_COMMAND("wpnmod", CConfig::ServerCommand);
	CVAR_REGISTER(&version);
}

void CConfig::LoadBlackList(void)
{
	WPNMOD_LOG("-------- Mapchange to %s --------\n", STRING(gpGlobals->mapname));

	if (ParseSection(GetConfigFile(), "[block]", (void*)OnParseBlockedItems, -1) && (int)m_pBlockedItemsList.size())
	{
		WPNMOD_LOG("Blocked items:\n");

		for (int i = 0; i < (int)m_pBlockedItemsList.size(); i++)
		{
			WPNMOD_LOG(" \"%s\"\n", m_pBlockedItemsList[i]->classname);

			for (int iId = 1; iId < MAX_WEAPONS; iId++)
			{
				if (WEAPON_GET_NAME(iId) && !_strcmpi(WEAPON_GET_NAME(iId), m_pBlockedItemsList[i]->classname))
				{
					//printf2("WOW: %d %s\n", iId, WEAPON_GET_NAME(iId));
					WEAPON_RESET_ITEMINFO(iId);
					//--g_iWeaponsCount;
					//memset(&WeaponInfoArray[iId], 0, sizeof(WeaponData));
				}
			}
		}
	}
}

void CConfig::ServerActivate(void)
{
	ParseBSP();
	ParseSpawnPoints();

	// Parse default equipments and ammo.
	ParseSection(GetConfigFile(), "[ammo]", (void*)OnParseStartAmmos, ':');
	ParseSection(GetConfigFile(), "[equipment]", (void*)OnParseStartEquipments	, ':');

	// Remove blocked items from map.
	for (int i = 0; i < (int)m_pBlockedItemsList.size(); i++)
	{
		edict_t *pFind = FIND_ENTITY_BY_CLASSNAME(NULL, m_pBlockedItemsList[i]->classname);

		while (!FNullEnt(pFind))
		{
			UTIL_RemoveEntity(pFind);
			pFind = FIND_ENTITY_BY_CLASSNAME(pFind, m_pBlockedItemsList[i]->classname);
		}
	}

	g_Memory.EnableShieldHitboxTracing();

	if (ParseSection(GetConfigFile(), "[weaponbox]", (void*)OnParseWeaponbox, ':'))
	{
		g_Memory.EnableWeaponboxModels();
	}

	SetHookVirtual(&g_PlayerSpawn_Hook);
	SetHookVirtual(&g_PlayerPostThink_Hook);
}

void CConfig::ServerDeactivate(void)
{
	m_bCrowbarHooked = false;
	m_bAmmoBoxHooked = false;

	//g_iWeaponsCount = 0;
	g_iAmmoBoxIndex = 0;

	memset(WeaponInfoArray, 0, sizeof(WeaponInfoArray));
	memset(AmmoBoxInfoArray, 0, sizeof(AmmoBoxInfoArray));

	for (int i = 0; i < (int)m_pDecalList.size(); i++)
	{
		delete m_pDecalList[i];
	}

	m_pDecalList.clear();

	for (int i = 0; i < (int)m_pStartAmmoList.size(); i++)
	{
		delete m_pStartAmmoList[i];
	}

	m_pStartAmmoList.clear();

	for (int i = 0; i < (int)m_pBlockedItemsList.size(); i++)
	{
		UnsetHookVirtual(m_pBlockedItemsList[i]);
		delete m_pBlockedItemsList[i];
	}

	m_pBlockedItemsList.clear();

	for (int i = 0; i < m_iMaxWeaponSlots; ++i)
	{
		memset(m_pCurrentSlots[i], 0, sizeof(int) * m_iMaxWeaponPositions);
	}

	for (int i = 0; i < CrowbarHook_End; i++)
	{
		UnsetHookVirtual(&g_CrowbarHooks[i]);
	}

	if (g_fh_funcPackWeapon.done)
	{
		UnsetHook(&g_fh_funcPackWeapon);
	}

	m_pEquipEnt = NULL;
	g_fh_funcPackWeapon.address = NULL;

	UnsetHookVirtual(&g_RpgAddAmmo_Hook);
	UnsetHookVirtual(&g_PlayerSpawn_Hook);
	UnsetHookVirtual(&g_PlayerPostThink_Hook);

	m_iWpnBoxLifeTime = 120;
}

void CConfig::ServerShutDown(void)
{
	if (m_pCurrentSlots)
	{
		for (int i = 0; i < m_iMaxWeaponSlots; ++i)
		{
			delete[] m_pCurrentSlots[i];
		}

		delete[] m_pCurrentSlots;
	}
}

void CConfig::SetConfigFile(void)
{
	build_pathname_r(m_cfgpath, sizeof(m_cfgpath) - 1, "%s/weaponmod/weaponmod-%s.ini", LOCALINFO((char*)"amxx_configsdir"), STRING(gpGlobals->mapname));
	
	if (!FileExists(m_cfgpath))
	{
		build_pathname_r(m_cfgpath, sizeof(m_cfgpath) - 1, "%s/weaponmod/weaponmod.ini", LOCALINFO((char*)"amxx_configsdir"));
	}
}

SUBMOD CConfig::CheckSubMod(const char* game)
{
	if (!stricmp(game, "ag"))
	{
		WPNMOD_LOG_ONLY("Adrenaline Gamer detected.\n");
		return SUBMOD_AG;
	}
	else if (!stricmp(game, "gearbox"))
	{
		WPNMOD_LOG_ONLY("Opposing Force detected.\n");
		return SUBMOD_GEARBOX;
	}
	else if (!stricmp(game, "valve"))
	{
		if (CVAR_GET_POINTER("sv_ag_version"))
		{
			WPNMOD_LOG_ONLY("Adrenaline Gamer Mini detected.\n");
			return SUBMOD_MINIAG;
		}
		else if (CVAR_GET_POINTER("aghl.ru"))
		{
			WPNMOD_LOG_ONLY("Bugfixed and improved HL release detected.\n");
			return SUBMOD_AGHLRU;
		}

		WPNMOD_LOG_ONLY("Half-Life assumed.\n");
		return SUBMOD_VALVE;
	}

	WPNMOD_LOG("Warning: game mod unrecognized (\"%s\").\n", MF_GetModname());
	return SUBMOD_UNKNOWN;
}

void CConfig::AutoSlotDetection(int iWeaponID, int iSlot, int iPosition)
{
	if (iSlot >= m_iMaxWeaponSlots || iSlot < 0)
	{
		iSlot = m_iMaxWeaponSlots - 1;
	}

	if (iPosition >= m_iMaxWeaponPositions || iPosition < 0)
	{
		iPosition = m_iMaxWeaponPositions - 1;
	}

	if (!m_pCurrentSlots[iSlot][iPosition])
	{
		m_pCurrentSlots[iSlot][iPosition] = iWeaponID;

		WEAPON_SET_SLOT(iWeaponID, iSlot);
		WEAPON_SET_SLOT_POSITION(iWeaponID, iPosition);
	}
	else
	{
		bool bFound = false;

		for (int k, i = 0; i < m_iMaxWeaponSlots && !bFound; i++)
		{
			for (k = 0; k < m_iMaxWeaponPositions; k++)
			{
				if (!m_pCurrentSlots[i][k])
				{
					m_pCurrentSlots[i][k] = iWeaponID;

					WEAPON_SET_SLOT(iWeaponID, i);
					WEAPON_SET_SLOT_POSITION(iWeaponID, k);

					WPNMOD_LOG("Warning: \"%s\" is moved to slot %d-%d.\n", WEAPON_GET_NAME(iWeaponID), i + 1, k + 1);

					bFound = true;
					break;
				}
			}
		}

		if (!bFound)
		{
			WEAPON_SET_SLOT_POSITION(iWeaponID, MAX_WEAPONS);
			WPNMOD_LOG("Warning: No free slot for \"%s\" in HUD!\n", WEAPON_GET_NAME(iWeaponID));
		}
	}
}

void CConfig::ManageEquipment(void)
{
	const char* equip_classname = "game_player_equip";
	edict_t* pFind = FIND_ENTITY_BY_CLASSNAME(NULL, equip_classname);

	while (!FNullEnt(pFind))
	{
		UTIL_RemoveEntity(pFind);
		pFind = FIND_ENTITY_BY_CLASSNAME(pFind, equip_classname);
	}

	pFind = CREATE_NAMED_ENTITY(MAKE_STRING(equip_classname));

	if (IsValidPev(pFind))
	{
		MDLL_Spawn(pFind);

		pFind = CREATE_NAMED_ENTITY(MAKE_STRING(equip_classname));

		if (IsValidPev(pFind))
		{
			m_pEquipEnt = pFind;
			m_pEquipEnt->v.classname = MAKE_STRING("weaponmod_equipment");
				
			MDLL_Spawn(m_pEquipEnt);
		}
	}
}

void CConfig::DecalPushList(const char *name)
{
	DecalList *p = new DecalList;

	p->name = STRING(ALLOC_STRING(name));
	p->index = META_RESULT_ORIG_RET(int);

	m_pDecalList.push_back(p);
}

bool CConfig::IsItemBlocked(const char *name)
{
	for (int i = 0; i < (int)m_pBlockedItemsList.size(); i++)
	{
		if (!stricmp(m_pBlockedItemsList[i]->classname, name))
		{
			return true;
		}
	}

	return false;
}

void CConfig::ServerCommand(void)
{
	const char *cmd = CMD_ARGV(1);

	if (!strcmp(cmd, "credits"))
	{
		printf2("Credits:\n\tAMXX Dev team, 6a6kin, GordonFreeman, Koshak, Lev, noo00oob.\n");
	}
	else  if (!strcmp(cmd, "version")) 
	{
		printf2("%s %s (%s)\n", Plugin_info.name, Plugin_info.version, Plugin_info.url);
		printf2("Author:\n\tKORD_12.7 (AGHL.RU Dev Team)\n");
		printf2("Compiled: %s\n", __DATE__ ", " __TIME__);

	}
	else if (!strcmp(cmd, "edicts"))
	{
		for (int i = 0; i < gpGlobals->maxEntities; i++)
		{
			if (IsValidPev(INDEXENT2(i)))
			{
				printf2("\t*Edict %d (%s)\n", i, STRING(INDEXENT2(i)->v.classname));
			}
		}
	}
	else if (!strcmp(cmd, "items"))
	{
		int i = 0;
		int items = 0;
		int weapons = 0;
		int ammo = 0;

		printf2("\nCurrently loaded weapons:\n");

		for (i = 1; i < /*g_iWeaponsCount*/ MAX_WEAPONS; i++)
		{
			if (WeaponInfoArray[i].iType == Wpn_Custom)
			{
				items++;
				printf2(" [%2d] %-23.22s\n", ++weapons, WEAPON_GET_NAME(i));
			}
		}

		printf2("\nCurrently loaded ammo:\n");

		for (i = 1; i <= g_iAmmoBoxIndex; i++)
		{
			items++;
			printf2(" [%2d] %-23.22s\n", ++ammo, AmmoBoxInfoArray[i].classname.c_str());
		}

		printf2("\nTotal:\n");
		printf2("%4d items (%d weapons, %d ammo).\n\n", items, weapons, ammo);
	}
	else if (!strcmp(cmd, "gpl"))
	{
		printf2("Half-Life Weapon Mod\n");
		printf2("\n");
		printf2(" by the AGHL.RU Dev Team\n");
		printf2("\n");
		printf2("\n");
		printf2("  This program is free software; you can redistribute it and/or modify it\n");
		printf2("  under the terms of the GNU General Public License as published by the\n");
		printf2("  Free Software Foundation; either version 2 of the License, or (at\n");
		printf2("  your option) any later version.\n");
		printf2("\n");
		printf2("  This program is distributed in the hope that it will be useful, but\n");
		printf2("  WITHOUT ANY WARRANTY; without even the implied warranty of\n");
		printf2("  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU\n");
		printf2("  General Public License for more details.\n");
		printf2("\n");
		printf2("  You should have received a copy of the GNU General Public License\n");
		printf2("  along with this program; if not, write to the Free Software Foundation,\n");
		printf2("  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA\n");
		printf2("\n");
		printf2("  In addition, as a special exception, the author gives permission to\n");
		printf2("  link the code of this program with the Half-Life Game Engine (\"HL\n");
		printf2("  Engine\") and Modified Game Libraries (\"MODs\") developed by Valve,\n");
		printf2("  L.L.C (\"Valve\"). You must obey the GNU General Public License in all\n");
		printf2("  respects for all of the code used other than the HL Engine and MODs\n");
		printf2("  from Valve. If you modify this file, you may extend this exception\n");
		printf2("  to your version of the file, but you are not obligated to do so. If\n");
		printf2("  you do not wish to do so, delete this exception statement from your\n");
		printf2("  version.\n");
		printf2("\n");
	}
	else
	{
		// Unknown command
		printf2("\nUsage: wpnmod < command > [ argument ]\n");
		printf2("Commands:\n");
		printf2("   %-10s - %s\n", "version", "displays version information.");
		printf2("   %-10s - %s\n", "credits", "displays credits information.");
		printf2("   %-10s - %s\n", "items", "displays information about registered weapons and ammo.");
		printf2("   %-10s - %s\n", "gpl", "print the license.");
		printf2("\n");
	}
}

bool CConfig::ClientCommand(edict_t *pEntity)
{
	static int FF_ClientCommand;

	if (FF_ClientCommand || (FF_ClientCommand = MF_RegisterForward("client_command", ET_STOP, FP_CELL, FP_DONE)))
	{
		if (MF_ExecuteForward(FF_ClientCommand, static_cast<cell>(ENTINDEX(pEntity)) != 0))
		{
			return false;
		}
	}

	static const char* cmd = NULL;

	cmd = CMD_ARGV(0);

	if (!cmd || !MF_IsPlayerIngame(ENTINDEX(pEntity)))
	{
		return false;
	}

	if (!strcmp(cmd, "lastinv"))
	{
		SelectLastItem(pEntity);
		return true;
	}
	else if (strstr(cmd, "weapon_"))
	{
		SelectItem(pEntity, cmd);
		return true;
	}
	else if (!_stricmp(cmd, "give") && cvar_sv_cheats && cvar_sv_cheats->value)
	{
		GiveNamedItem(pEntity, CMD_ARGV(1));
		return true;
	}
	else if (!_stricmp(cmd, "wpnmod"))
	{
		int i = 0;
		int ammo = 0;
		int weapons = 0;

		static char buf[1024];
		size_t len = 0;

		sprintf(buf, "\n%s %s\n", Plugin_info.name, Plugin_info.version);
		CLIENT_PRINT(pEntity, print_console, buf);
		len = sprintf(buf, "Author: \n         KORD_12.7 (AGHL.RU Dev Team)\n");
		len += sprintf(&buf[len], "Credits: \n         AMXX Dev team, Arkshine, 6a6kin, GordonFreeman, Koshak, Lev, noo00oob\n");
		len += sprintf(&buf[len], "Compiled: %s\nURL: http://www.aghl.ru/ - Russian Half-Life and Adrenaline Gamer Community.\n\n", __DATE__ ", " __TIME__);
		CLIENT_PRINT(pEntity, print_console, buf);

		CLIENT_PRINT(pEntity, print_console, "Currently loaded weapons:\n");

		for (i = 1; i </*= g_iWeaponsCount*/ MAX_WEAPONS; i++)
		{
			if (WeaponInfoArray[i].iType == Wpn_Custom)
			{
				sprintf(buf, " [%2d] %-23.22s\n", ++weapons, WEAPON_GET_NAME(i));
				CLIENT_PRINT(pEntity, print_console, buf);
			}
		}

		CLIENT_PRINT(pEntity, print_console, "\nCurrently loaded ammo:\n");

		for (i = 1; i <= g_iAmmoBoxIndex; i++)
		{
			sprintf(buf, " [%2d] %-23.22s\n", ++ammo, AmmoBoxInfoArray[i].classname.c_str());
			CLIENT_PRINT(pEntity, print_console, buf);
		}

		CLIENT_PRINT(pEntity, print_console, "\nTotal:\n");
		sprintf(buf, "%4d items (%d weapons, %d ammo).\n\n", weapons + ammo, weapons, ammo);
		CLIENT_PRINT(pEntity, print_console, buf);

		return true;
	}

	return false;
}
