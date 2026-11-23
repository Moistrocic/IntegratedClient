#ifndef GAME_SERVER_RANBI_PRACTICE_SYNC_H
#define GAME_SERVER_RANBI_PRACTICE_SYNC_H

#include <base/vmath.h>

#include <generated/protocol.h>

class CCharacter;
class CGameContext;
class IStorage;

struct SRanbiPracticeCharState
{
	bool m_Valid = false;
	vec2 m_Pos = vec2(0.0f, 0.0f);
	int m_ActiveWeapon = 0;
	int m_GotMask = 0;
	int m_Ammo[NUM_WEAPONS] = {};
	int m_Jumps = 0;
	int m_JumpedTotal = 0;
	bool m_Solo = false;
	bool m_EndlessHook = false;
	bool m_EndlessJump = false;
	bool m_DeepFrozen = false;
	bool m_LiveFrozen = false;
	bool m_Invincible = false;
	bool m_CollisionDisabled = false;
	bool m_Super = false;
	bool m_Jetpack = false;
	int m_HitDisabled = 0;
};

struct CRanbiPracticeSyncData
{
	bool m_Loaded = false;
	bool m_HasDummy = false;
	SRanbiPracticeCharState m_Body;
	SRanbiPracticeCharState m_Dummy;
};

bool RanbiPracticeSyncLoad(CRanbiPracticeSyncData &Data, const char *pPath, IStorage *pStorage);
bool RanbiPracticeSyncApply(CGameContext *pGameContext, int ClientId, const SRanbiPracticeCharState &State);

#endif
