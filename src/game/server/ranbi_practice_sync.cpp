#include "ranbi_practice_sync.h"

#include <base/dbg.h>
#include <base/io.h>
#include <base/str.h>

#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>

static int SyncInt(const char *pString, const char *pKey, int Default = 0)
{
	const char *pFound = str_find(pString, pKey);
	if(pFound == nullptr)
		return Default;
	return str_toint(pFound + str_length(pKey));
}

static vec2 ParseSyncPos(const char *pValue)
{
	vec2 Pos(0.0f, 0.0f);
	if(pValue == nullptr)
		return Pos;
	const char *pComma = str_find(pValue, ",");
	if(pComma == nullptr)
		return Pos;
	char aX[64];
	const int Len = (int)(pComma - pValue);
	if(Len > 0 && Len < (int)sizeof(aX))
	{
		mem_copy(aX, pValue, Len);
		aX[Len] = '\0';
		Pos.x = str_tofloat(aX);
		Pos.y = str_tofloat(pComma + 1);
	}
	return Pos;
}

static void ParseSyncState(const char *pString, SRanbiPracticeCharState &State)
{
	if(pString == nullptr || pString[0] == '\0' || SyncInt(pString, "valid=") == 0)
		return;
	State.m_Valid = true;
	State.m_ActiveWeapon = SyncInt(pString, "active=");
	State.m_GotMask = SyncInt(pString, "got=");
	State.m_Jumps = SyncInt(pString, "jumps=");
	State.m_JumpedTotal = SyncInt(pString, "jumptotal=");
	State.m_Solo = SyncInt(pString, "solo=") != 0;
	State.m_EndlessJump = SyncInt(pString, "infjump=") != 0;
	State.m_EndlessHook = SyncInt(pString, "endless=") != 0;
	State.m_DeepFrozen = SyncInt(pString, "deep=") != 0;
	State.m_LiveFrozen = SyncInt(pString, "live=") != 0;
	State.m_Invincible = SyncInt(pString, "invincible=") != 0;
	State.m_CollisionDisabled = SyncInt(pString, "collision=") != 0;
	State.m_Super = SyncInt(pString, "super=") != 0;
	State.m_Jetpack = SyncInt(pString, "jetpack=") != 0;
	State.m_HitDisabled = SyncInt(pString, "hitdis=");
	const char *pAmmo = str_find(pString, "ammo=");
	if(pAmmo != nullptr)
	{
		const char *pValue = pAmmo + str_length("ammo=");
		for(int i = 0; i < NUM_WEAPONS && pValue != nullptr; i++)
		{
			State.m_Ammo[i] = str_toint(pValue);
			pValue = str_find(pValue, ":");
			if(pValue != nullptr)
				pValue++;
		}
	}
}

bool RanbiPracticeSyncLoad(CRanbiPracticeSyncData &Data, const char *pPath, IStorage *pStorage)
{
	Data = CRanbiPracticeSyncData();
	if(pPath == nullptr || pPath[0] == '\0' || pStorage == nullptr)
	{
		dbg_msg("ranbi_practice_sync", "sync file path is empty; retrying");
		return false;
	}
	dbg_msg("ranbi_practice_sync", "opening sync file '%s'", pPath);
	IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_ABSOLUTE);
	if(!File)
	{
		dbg_msg("ranbi_practice_sync", "warning: could not open sync file '%s'", pPath);
		return false;
	}
	CLineReader Reader;
	Reader.OpenFile(File);
	while(const char *pLine = Reader.Get())
	{
		if(pLine[0] == '\0' || pLine[0] == '#')
			continue;
		if(str_startswith(pLine, "has_dummy="))
			Data.m_HasDummy = str_toint(pLine + str_length("has_dummy=")) != 0;
		else if(str_startswith(pLine, "body_pos="))
			Data.m_Body.m_Pos = ParseSyncPos(pLine + str_length("body_pos="));
		else if(str_startswith(pLine, "dummy_pos="))
			Data.m_Dummy.m_Pos = ParseSyncPos(pLine + str_length("dummy_pos="));
		else if(str_startswith(pLine, "body_state="))
			ParseSyncState(pLine + str_length("body_state="), Data.m_Body);
		else if(str_startswith(pLine, "dummy_state="))
			ParseSyncState(pLine + str_length("dummy_state="), Data.m_Dummy);
	}
	Data.m_Loaded = Data.m_Body.m_Valid || Data.m_Dummy.m_Valid;
	dbg_msg("ranbi_practice_sync", "loaded sync file '%s' valid=%d body=%d dummy=%d body_pos=(%.3f,%.3f) dummy_pos=(%.3f,%.3f)",
		pPath, Data.m_Loaded ? 1 : 0, Data.m_Body.m_Valid ? 1 : 0, Data.m_Dummy.m_Valid ? 1 : 0,
		Data.m_Body.m_Pos.x, Data.m_Body.m_Pos.y, Data.m_Dummy.m_Pos.x, Data.m_Dummy.m_Pos.y);
	return Data.m_Loaded;
}

bool RanbiPracticeSyncApply(CGameContext *pGameContext, int ClientId, const SRanbiPracticeCharState &State)
{
	if(pGameContext == nullptr || !State.m_Valid || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	CPlayer *pPlayer = pGameContext->m_apPlayers[ClientId];
	if(pPlayer == nullptr)
		return false;
	CCharacter *pBefore = pGameContext->GetPlayerChar(ClientId);
	const int DDRaceTeamBefore = pGameContext->GetDDRaceTeam(ClientId);
	const int GameTeamBefore = pPlayer->GetTeam();
	dbg_msg("ranbi_practice_sync", "apply-before client=%d char=%p game_team=%d ddr_team=%d pos=(%.3f,%.3f) valid=%d",
		ClientId, (void *)pBefore, GameTeamBefore, DDRaceTeamBefore, State.m_Pos.x, State.m_Pos.y, State.m_Valid ? 1 : 0);

	if(DDRaceTeamBefore != 1)
	{
		// DDNet /team 1 is the DDRace team. DoTeamChange only changes the game team, so use
		// the same CGameTeams::SetCharacterTeam path as the /team command.
		char aError[512] = "";
		if(pGameContext->m_pController == nullptr ||
			!pGameContext->m_pController->Teams().SetCharacterTeam(ClientId, 1, aError, sizeof(aError)))
		{
			pGameContext->m_aRanbiPracticeSyncTeamFailed[ClientId] = true;
			dbg_msg("ranbi_practice_sync", "warning: client=%d could not join DDRace team 1 (was %d): '%s'; stopping team retry",
				ClientId, DDRaceTeamBefore, aError);
			return false;
		}
		pGameContext->m_pController->Teams().SetPractice(1, true);
		pGameContext->m_apPlayers[ClientId]->m_LastDDRaceTeamChange = pGameContext->Server()->Tick();
		dbg_msg("ranbi_practice_sync", "client=%d SetCharacterTeam DDRace team 1 ok (was %d); waiting next tick before apply", ClientId, DDRaceTeamBefore);
		return false;
	}

	// Already DDRace team 1: never call SetTeam/DoTeamChange again (both can force a
	// respawn every tick). Only apply position/weapons/status.
	dbg_msg("ranbi_practice_sync", "client=%d skip team change (already DDRace team 1)", ClientId);
	if(pGameContext->m_pController != nullptr)
		pGameContext->m_pController->Teams().SetPractice(pGameContext->GetDDRaceTeam(ClientId), true);
	CCharacter *pCharacter = pGameContext->GetPlayerChar(ClientId);
	if(pCharacter == nullptr)
	{
		dbg_msg("ranbi_practice_sync", "client=%d has no character yet; will retry on spawn", ClientId);
		return false;
	}

	CCharacterCore Core = pCharacter->GetCore();
	Core.m_ActiveWeapon = State.m_ActiveWeapon;
	for(int i = 0; i < NUM_WEAPONS; i++)
	{
		Core.m_aWeapons[i].m_Got = (State.m_GotMask & (1 << i)) != 0;
		Core.m_aWeapons[i].m_Ammo = State.m_Ammo[i];
	}
	Core.m_Jumps = State.m_Jumps;
	Core.m_JumpedTotal = State.m_JumpedTotal;
	Core.m_Solo = State.m_Solo;
	Core.m_EndlessHook = State.m_EndlessHook;
	Core.m_EndlessJump = State.m_EndlessJump;
	Core.m_DeepFrozen = State.m_DeepFrozen;
	Core.m_LiveFrozen = State.m_LiveFrozen;
	Core.m_Invincible = State.m_Invincible;
	Core.m_CollisionDisabled = State.m_CollisionDisabled;
	Core.m_Super = State.m_Super;
	Core.m_Jetpack = State.m_Jetpack;
	Core.m_HammerHitDisabled = (State.m_HitDisabled & 1) != 0;
	Core.m_GrenadeHitDisabled = (State.m_HitDisabled & 2) != 0;
	Core.m_LaserHitDisabled = (State.m_HitDisabled & 4) != 0;
	Core.m_ShotgunHitDisabled = (State.m_HitDisabled & 8) != 0;
	Core.m_HookHitDisabled = (State.m_HitDisabled & 16) != 0;
	pCharacter->SetCore(Core);
	pCharacter->SetPosition(State.m_Pos);
	pCharacter->SetJumps(State.m_Jumps);
	const CCharacterCore *pApplied = pCharacter->Core();
	vec2 AppliedPos = pCharacter->m_Pos; // NOLINT: verify position after setter
	int AppliedGot = 0;
	for(int i = 0; i < NUM_WEAPONS; i++)
	{
		if(pApplied->m_aWeapons[i].m_Got)
			AppliedGot |= 1 << i;
	}
	dbg_msg("ranbi_practice_sync", "applied client=%d char=%p team=%d pos=(%.3f,%.3f) got=%d active=%d jumps=%d solo=%d endless=%d deep=%d live=%d inv=%d coll=%d super=%d jet=%d",
		ClientId, (void *)pCharacter, pPlayer->GetTeam(), AppliedPos.x, AppliedPos.y, AppliedGot,
		pApplied->m_ActiveWeapon, pApplied->m_Jumps, pApplied->m_Solo ? 1 : 0,
		pApplied->m_EndlessHook ? 1 : 0, pApplied->m_DeepFrozen ? 1 : 0,
		pApplied->m_LiveFrozen ? 1 : 0, pApplied->m_Invincible ? 1 : 0,
		pApplied->m_CollisionDisabled ? 1 : 0, pApplied->m_Super ? 1 : 0,
		pApplied->m_Jetpack ? 1 : 0);
	return true;
}

void CGameContext::RanbiPracticeSyncTick()
{
	if(!m_RanbiPracticeSyncLoaded)
	{
		const bool PathEmpty = g_Config.m_SvRanbiPracticeSyncFile[0] == '\0';
		if(PathEmpty && !m_RanbiPracticeSyncPathLogged)
		{
			m_RanbiPracticeSyncPathLogged = true;
			dbg_msg("ranbi_practice_sync", "warning: sv_ranbi_practice_sync_file is empty; will retry");
		}
		if(Server()->Tick() >= m_RanbiPracticeSyncNextRetryTick)
		{
			m_RanbiPracticeSyncLoaded = RanbiPracticeSyncLoad(m_RanbiPracticeSyncData, g_Config.m_SvRanbiPracticeSyncFile, Storage());
			if(!m_RanbiPracticeSyncLoaded)
				m_RanbiPracticeSyncNextRetryTick = Server()->Tick() + 50;
		}
	}
	if(!m_RanbiPracticeSyncLoaded || !m_RanbiPracticeSyncData.m_Loaded)
		return;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CPlayer *pPlayer = m_apPlayers[ClientId];
		if(pPlayer == nullptr)
			continue;
		CCharacter *pCharacter = GetPlayerChar(ClientId);
		if(pCharacter == nullptr || !pCharacter->IsAlive())
			continue;
		if(m_aRanbiPracticeSyncTeamFailed[ClientId])
			continue;
		if(!m_aRanbiPracticeSyncAssigned[ClientId])
		{
			if(m_RanbiPracticeSyncNextRole >= 2)
				continue;
			if(m_RanbiPracticeSyncNextRole == 1 && !m_RanbiPracticeSyncData.m_HasDummy)
				continue;
			m_aRanbiPracticeSyncAssigned[ClientId] = true;
			m_aRanbiPracticeSyncRole[ClientId] = m_RanbiPracticeSyncNextRole++;
			dbg_msg("ranbi_practice_sync", "client=%d assigned role=%d", ClientId, m_aRanbiPracticeSyncRole[ClientId]);
		}
		if(m_apRanbiPracticeSyncChar[ClientId] == pCharacter)
			continue;
		const int Role = m_aRanbiPracticeSyncRole[ClientId];
		const bool Applied = RanbiPracticeSyncApply(this, ClientId, Role == 1 ? m_RanbiPracticeSyncData.m_Dummy : m_RanbiPracticeSyncData.m_Body);
		m_apRanbiPracticeSyncChar[ClientId] = Applied ? GetPlayerChar(ClientId) : nullptr;
	}
}
