// ranbi: task-19 "weapon aim lines" feature, faithful weapon physics preview.
// Replicates the actual client prediction weapon projectiles instead of drawing
// approximations:
//   - laser / DDRace shotgun: CLaser with real bounce/energy rules (laser.cpp);
//   - vanilla shotgun: the five CProjectile spread shots of FireWeapon();
//   - grenade: CProjectile::GetPos/Tick trajectory with the current tune values
//     (no artificial bounce; FireWeapon grenades explode on first hit).
// Read-only rendering for the local player only; no gameplay/network changes.

#include "ranbi_aim_lines.h"

#include <base/math.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/gameworld.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/mapitems.h>

#include <algorithm>

namespace
{
	constexpr int MAX_AIM_LINES = 512;
	constexpr int MAX_LASER_SEGMENTS = 64; // defensive cap; CLaser uses m_LaserBounceNum
	constexpr int MAX_PROJECTILE_TICKS = 200; // defensive cap; grenade default = 2s * 50 = 100
	constexpr float HIT_MARKER_HALF_SIZE = 4.0f;
	// character.cpp:310: ProjStartPos = m_Pos + Direction * m_ProximityRadius * 0.75f
	constexpr float PROJECTILE_START_OFFSET = 0.75f;

	// vanilla shotgun spread, character.cpp:420
	const float SHOTGUN_SPREAD[] = {-0.185f, -0.070f, 0.0f, 0.070f, 0.185f};

	int ClampTuneZone(int Zone)
	{
		return std::clamp(Zone, 0, (int)TuneZone::NUM - 1);
	}

	int SpawnTuneZone(const CGameWorld &World, CCollision *pCollision, vec2 Pos)
	{
		// same formula as CLaser/CProjectile constructors and FireWeapon tune lookups
		return World.m_WorldConfig.m_UseTuneZones ? ClampTuneZone(pCollision->IsTune(pCollision->GetMapIndex(Pos))) : 0;
	}

	// port of CEntity::GameLayerClipped (entity.cpp:39-43)
	bool IsLayerClipped(const CCollision *pCollision, vec2 Pos)
	{
		return round_to_int(Pos.x) / 32 < -200 || round_to_int(Pos.x) / 32 > pCollision->GetWidth() + 200 ||
		       round_to_int(Pos.y) / 32 < -200 || round_to_int(Pos.y) / 32 > pCollision->GetHeight() + 200;
	}

	struct SAimDrawBuffer
	{
		IGraphics::CLineItem *m_pLines;
		int m_Max;
		int m_Num = 0;

		void Line(vec2 From, vec2 To)
		{
			if(m_Num >= m_Max)
				return;
			m_pLines[m_Num++] = IGraphics::CLineItem(From, To);
		}
	};

	void AddHitMarker(SAimDrawBuffer &Buf, vec2 Pos)
	{
		Buf.Line(Pos - vec2(HIT_MARKER_HALF_SIZE, 0.0f), Pos + vec2(HIT_MARKER_HALF_SIZE, 0.0f));
		Buf.Line(Pos - vec2(0.0f, HIT_MARKER_HALF_SIZE), Pos + vec2(0.0f, HIT_MARKER_HALF_SIZE));
	}

	// Port of CLaser (laser.cpp:15-169). Only the geometry is needed: no sounds,
	// no damage/velocity side effects, no bounce-delay timing (it does not change
	// the path). Character hits use the predicted world's IntersectCharacter.
	void SimulateLaser(SAimDrawBuffer &Buf, CGameWorld &World, CCollision *pCollision, CCharacter *pOwner, int OwnerId,
		int WeaponType, const CTuningParams *pTuning, vec2 StartPos, vec2 Direction, float StartEnergy)
	{
		// laser.cpp:21-22
		float Energy = StartEnergy;
		if(World.m_WorldConfig.m_IsFNG && Energy < 10.0f)
			Energy = 800.0f;

		int Bounces = 0;
		bool ZeroEnergyBounceInLastTick = false;
		vec2 Pos = StartPos;
		vec2 Dir = Direction;
		vec2 Last = StartPos;

		for(int Segment = 0; Segment < MAX_LASER_SEGMENTS && Energy >= 0.0f; Segment++)
		{
			// laser.cpp:108-111
			vec2 To = Pos + Dir * Energy;
			vec2 ColTile;
			const int Res = pCollision->IntersectLineTeleWeapon(Pos, To, &ColTile, &To);

			// laser.cpp:33-50: hit characters along the segment (the same world
			// IntersectCharacter call is used in CLaser::HitCharacter). The owner
			// exclusion follows the DontHitSelf rule from laser.cpp:39.
			const bool DontHitSelf = (World.m_WorldConfig.m_OldLaser || !World.m_WorldConfig.m_IsDDRace) || (Bounces == 0);
			vec2 At = To;
			CCharacter *pHit = World.IntersectCharacter(Pos, To, 0.0f, At, DontHitSelf ? pOwner : nullptr, OwnerId);
			if(pOwner && ((WeaponType == WEAPON_LASER && pOwner->LaserHitDisabled()) || (WeaponType == WEAPON_SHOTGUN && pOwner->ShotgunHitDisabled())))
				pHit = nullptr;
			if(pHit != nullptr)
			{
				Buf.Line(Last, At);
				AddHitMarker(Buf, At);
				return;
			}

			if(Res != 0)
			{
				// laser.cpp:113-158: reflect with MovePoint and subtract bounce cost
				vec2 TempPos = To;
				vec2 TempDir = Dir * 4.0f;
				int f = 0;
				if(Res == -1)
				{
					f = pCollision->GetTile(round_to_int(ColTile.x), round_to_int(ColTile.y));
					pCollision->SetCollisionAt(round_to_int(ColTile.x), round_to_int(ColTile.y), TILE_SOLID);
				}
				pCollision->MovePoint(&TempPos, &TempDir, 1.0f, nullptr);
				if(Res == -1)
					pCollision->SetCollisionAt(round_to_int(ColTile.x), round_to_int(ColTile.y), f);

				const vec2 From = Pos;
				Pos = TempPos;
				Dir = normalize(TempDir);
				// laser.cpp:135-136: draw to the collision point first, then continue from
				// the reflected MovePoint position.
				Buf.Line(Last, To);
				Buf.Line(To, Pos);
				Last = Pos;

				const float Distance = distance(From, Pos);
				// laser.cpp:138-156: energy/bounce limits
				if(Distance == 0.0f && ZeroEnergyBounceInLastTick)
					Energy = -1.0f;
				else
					Energy -= Distance + (float)pTuning->m_LaserBounceCost;
				ZeroEnergyBounceInLastTick = Distance == 0.0f;
				Bounces++;
				if(Bounces > (int)(float)pTuning->m_LaserBounceNum)
					Energy = -1.0f;
			}
			else
			{
				// laser.cpp:160-168: no wall hit, stops at the end of the reach
				Pos = To;
				Buf.Line(Last, Pos);
				AddHitMarker(Buf, Pos);
				return;
			}
		}

		AddHitMarker(Buf, Last);
	}

	// Port of CProjectile::GetPos/Tick (projectile.cpp:48-168) for the local
	// prediction preview. FireWeapon grenades have m_Bouncing = 0, so the trajectory
	// stops on the first collision (explosion); lifetime ends also stop it.
	void SimulateProjectile(SAimDrawBuffer &Buf, CGameWorld &World, CCollision *pCollision, CCharacter *pOwner, int OwnerId,
		const CTuningParams *pSpawnTuning, vec2 StartPos, vec2 Direction, float SpeedFactor,
		int LifeSpan, float Curvature, float Speed, int TickSpeed)
	{
		(void)pSpawnTuning;
		const vec2 Dir = Direction * SpeedFactor; // CProjectile m_Direction
		vec2 Last = StartPos;
		float PrevT = 0.0f;

		const int Steps = std::min(LifeSpan, MAX_PROJECTILE_TICKS);
		for(int Tick = 1; Tick <= Steps; Tick++)
		{
			const float CurT = (float)Tick / (float)TickSpeed;
			const vec2 PrevPos = CalcPos(StartPos, Dir, Curvature, Speed, PrevT);
			const vec2 CurPos = CalcPos(StartPos, Dir, Curvature, Speed, CurT);

			vec2 ColPos = CurPos;
			vec2 BeforePos = CurPos;
			const int Collide = pCollision->IntersectLine(PrevPos, CurPos, &ColPos, &BeforePos);

			// projectile.cpp:83-107: character hit (freeze/size 6 for regular projectiles)
			vec2 At = ColPos;
			CCharacter *pHit = World.IntersectCharacter(PrevPos, ColPos, 6.0f, At, pOwner, OwnerId);
			if(Collide || pHit != nullptr || IsLayerClipped(pCollision, CurPos))
			{
				// projectile.cpp:107: explosions happen at ColPos; character hits use At.
				const vec2 HitPos = Collide ? ColPos : (pHit != nullptr ? At : ColPos);
				Buf.Line(Last, HitPos);
				AddHitMarker(Buf, HitPos);
				return;
			}

			Buf.Line(Last, CurPos);
			Last = CurPos;
			PrevT = CurT;

			if(Tick == Steps)
			{
				AddHitMarker(Buf, Last);
				return;
			}
		}
		AddHitMarker(Buf, Last);
	}

	// Character.cpp:268: Direction = normalize(vec2(LatestInput.m_TargetX, m_TargetY))
	bool GetAimDirection(const CGameClient *pGameClient, CCharacter *pLocalChar, const CGameClient::CClientData &LocalClient, int LocalId, vec2 &OutDirection)
	{
		if(pLocalChar != nullptr)
		{
			const CNetObj_PlayerInput *pInput = pLocalChar->LatestInput();
			if(pInput != nullptr && (pInput->m_TargetX != 0 || pInput->m_TargetY != 0))
			{
				OutDirection = normalize(vec2((float)pInput->m_TargetX, (float)pInput->m_TargetY));
				return true;
			}
		}

		if(LocalId >= 0 && LocalId < MAX_CLIENTS)
		{
			const auto &CharacterInfo = pGameClient->m_Snap.m_aCharacters[LocalId];
			if(CharacterInfo.m_HasExtendedDisplayInfo && (CharacterInfo.m_ExtendedData.m_TargetX != 0 || CharacterInfo.m_ExtendedData.m_TargetY != 0))
			{
				OutDirection = normalize(vec2((float)CharacterInfo.m_ExtendedData.m_TargetX, (float)CharacterInfo.m_ExtendedData.m_TargetY));
				return true;
			}
		}

		OutDirection = direction(LocalClient.m_Predicted.m_Angle / 256.0f * 2.0f * pi);
		return true;
	}
} // namespace

void CRanbiAimLines::OnRender()
{
	if(!g_Config.m_RcWeaponAimLines)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		return;

	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return;
	const CGameClient::CClientData &LocalClient = GameClient()->m_aClients[LocalId];
	if(!LocalClient.m_Active)
		return;

	CGameWorld &World = GameClient()->m_PredictedWorld;
	CCharacter *pLocalChar = World.GetCharacterById(LocalId);
	const int Weapon = pLocalChar != nullptr ? pLocalChar->Core()->m_ActiveWeapon : LocalClient.m_Predicted.m_ActiveWeapon;
	if(Weapon != WEAPON_LASER && Weapon != WEAPON_SHOTGUN && Weapon != WEAPON_GRENADE)
		return;

	vec2 Direction;
	if(!GetAimDirection(GameClient(), pLocalChar, LocalClient, LocalId, Direction) || length(Direction) < 0.001f)
		return;

	// character.cpp:413-467: CProjectile shots (vanilla shotgun, grenade) start at
	// ProjStartPos, but CLaser shots (laser, DDRace shotgun) start at the character
	// center (m_Pos). Keep both so the preview starts exactly where the real shot does.
	vec2 StartPos = LocalClient.m_Predicted.m_Pos;
	if(pLocalChar != nullptr)
		StartPos = pLocalChar->Core()->m_Pos + Direction * pLocalChar->GetProximityRadius() * PROJECTILE_START_OFFSET;
	else
		StartPos = LocalClient.m_Predicted.m_Pos + Direction * CCharacterCore::PhysicalSize() * PROJECTILE_START_OFFSET;
	const vec2 LaserStartPos = pLocalChar != nullptr ? pLocalChar->Core()->m_Pos : LocalClient.m_Predicted.m_Pos;

	CCollision *pCollision = Collision();
	IGraphics::CLineItem aLines[MAX_AIM_LINES];
	SAimDrawBuffer Buf{aLines, MAX_AIM_LINES};
	ColorRGBA Color;

	if(Weapon == WEAPON_LASER)
	{
		// character.cpp:469-475: laser is a CLaser in every mode
		const int FireTuneZone = pLocalChar != nullptr ? ClampTuneZone(pLocalChar->GetOverriddenTuneZone()) : SpawnTuneZone(World, pCollision, StartPos);
		const int LaserTuneZone = SpawnTuneZone(World, pCollision, LaserStartPos);
		const CTuningParams *pFireTuning = World.GetTuning(FireTuneZone);
		const CTuningParams *pTuning = World.GetTuning(LaserTuneZone);
		Color = ColorRGBA(1.0f, 0.25f, 0.25f, 0.6f);
		SimulateLaser(Buf, World, pCollision, pLocalChar, LocalId, WEAPON_LASER, pTuning, LaserStartPos, Direction, (float)pFireTuning->m_LaserReach);
	}
	else if(Weapon == WEAPON_SHOTGUN)
	{
		Color = ColorRGBA(1.0f, 0.9f, 0.35f, 0.55f);
		if(World.m_WorldConfig.m_IsVanilla)
		{
			// character.cpp:413-437: vanilla shotgun = 5 CProjectiles with spread
			const float Angle = angle(Direction);
			const float SpeedHz = (float)Client()->GameTickSpeed();
			const CTuningParams *pGlobalTuning = World.GlobalTuning();
			const int ProjTuneZone = SpawnTuneZone(World, pCollision, StartPos);
			const CTuningParams *pTuning = World.GetTuning(ProjTuneZone);
			const int LifeSpan = (int)(SpeedHz * (float)pGlobalTuning->m_ShotgunLifetime);
			for(int i = 0; i < 5; i++)
			{
				const float V = 1.0f - absolute(i - 2) / 2.0f;
				const float SpeedFactor = mix((float)pGlobalTuning->m_ShotgunSpeeddiff, 1.0f, V);
				SimulateProjectile(Buf, World, pCollision, pLocalChar, LocalId, pTuning, StartPos,
					direction(Angle + SHOTGUN_SPREAD[i]), SpeedFactor, LifeSpan,
					(float)pTuning->m_ShotgunCurvature, (float)pTuning->m_ShotgunSpeed, Client()->GameTickSpeed());
			}
		}
		else if(World.m_WorldConfig.m_IsDDRace)
		{
			// character.cpp:438-443: DDRace shotgun = one CLaser (no 5 spread shots)
			const int FireTuneZone = pLocalChar != nullptr ? ClampTuneZone(pLocalChar->GetOverriddenTuneZone()) : SpawnTuneZone(World, pCollision, StartPos);
			const int LaserTuneZone = SpawnTuneZone(World, pCollision, LaserStartPos);
			const CTuningParams *pFireTuning = World.GetTuning(FireTuneZone);
			const CTuningParams *pTuning = World.GetTuning(LaserTuneZone);
			SimulateLaser(Buf, World, pCollision, pLocalChar, LocalId, WEAPON_SHOTGUN, pTuning, LaserStartPos, Direction, (float)pFireTuning->m_LaserReach);
		}
		else
		{
			// FireWeapon has no branch for other modes -> no trajectory
			return;
		}
	}
	else // WEAPON_GRENADE
	{
		Color = ColorRGBA(1.0f, 0.55f, 0.1f, 0.6f);
		// character.cpp:449-467: one explosive CProjectile; FireWeapon defaults m_Bouncing = 0
		const int FireTuneZone = pLocalChar != nullptr ? ClampTuneZone(pLocalChar->GetOverriddenTuneZone()) : SpawnTuneZone(World, pCollision, StartPos);
		const int ProjTuneZone = SpawnTuneZone(World, pCollision, StartPos);
		const CTuningParams *pFireTuning = World.GetTuning(FireTuneZone);
		const CTuningParams *pTuning = World.GetTuning(ProjTuneZone);
		const int LifeSpan = (int)((float)Client()->GameTickSpeed() * (float)pFireTuning->m_GrenadeLifetime);
		SimulateProjectile(Buf, World, pCollision, pLocalChar, LocalId, pTuning, StartPos, Direction, 1.0f, LifeSpan,
			(float)pTuning->m_GrenadeCurvature, (float)pTuning->m_GrenadeSpeed, Client()->GameTickSpeed());
	}

	if(Buf.m_Num == 0)
		return;

	// Draw in world coordinates; the mapping is set explicitly like the other Ranbi
	// world-space renderers, so the lines do not depend on another component's mapping.
	const vec2 CameraPos = GameClient()->m_Camera.m_Center;
	const CScreenRect SavedScreen = Graphics()->GetScreen();
	const CScreenRect WorldScreen = Graphics()->MapScreenToWorld(CameraPos.x, CameraPos.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->ScreenAspect(), 1.0f);
	Graphics()->MapScreen(WorldScreen);

	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->SetColor(Color);
	Graphics()->LinesDraw(aLines, Buf.m_Num);
	Graphics()->LinesEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->MapScreen(SavedScreen);
}
