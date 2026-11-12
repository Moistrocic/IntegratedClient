// ranbi: weapon aim lines (task-41).
// Reuses the vanilla post-fire visuals instead of drawing custom styled lines:
//   - laser / DDRace shotgun: CItems::RenderLaser (same outline/inner colors, ticks
//     and LASERTYPE as a real fired CLaser);
//   - vanilla shotgun / grenade: CItems::RenderProjectileTrailPreview (same projectile
//     sprite rotation + SmokeTrail/BulletTrail as the original RenderProjectile).
// The trajectory itself is still the faithful CLaser/CProjectile simulation from
// task-19/22 (current input direction from task-37); re-simulated on every render
// frame with only defensive segment/step caps. No custom colors/widths/hit style.

#include "ranbi_aim_lines.h"

#include <base/math.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/components/items.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/gameworld.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/mapitems.h>

#include <algorithm>

namespace
{
	constexpr int MAX_AIM_SEGMENTS = 512;
	constexpr int MAX_LASER_SEGMENTS = 64; // defensive cap; CLaser uses m_LaserBounceNum
	constexpr int MAX_PROJECTILE_TICKS = 200; // defensive cap; grenade default = 2s * 50 = 100
	constexpr int PROJECTILE_SAMPLE_STEP = 8; // trail sample rate (render frames still every frame)
	constexpr int MAX_PROJECTILE_TRAIL_SAMPLES = 16; // cap per projectile path (perf)
	// character.cpp:310: ProjStartPos = m_Pos + Direction * m_ProximityRadius * 0.75f
	constexpr float PROJECTILE_START_OFFSET = 0.75f;

	// vanilla shotgun spread, character.cpp:420
	const float SHOTGUN_SPREAD[] = {-0.185f, -0.070f, 0.0f, 0.070f, 0.185f};

	struct SAimSegment
	{
		vec2 m_From;
		vec2 m_To;
		int m_Path;
	};

	struct SAimDrawBuffer
	{
		SAimSegment *m_pSegments;
		int m_Max;
		int m_Num = 0;
		int m_Path = 0;

		int NextPath()
		{
			return m_Path++;
		}

		void Line(vec2 From, vec2 To)
		{
			if(m_Num >= m_Max)
				return;
			m_pSegments[m_Num++] = SAimSegment{From, To, m_Path};
		}
	};

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

	// Port of CLaser (laser.cpp:15-169). Only the geometry is needed: no sounds,
	// no damage/velocity side effects, no bounce-delay timing (it does not change
	// the path). Character hits use the predicted world's IntersectCharacter.
	void SimulateLaser(SAimDrawBuffer &Buf, CGameWorld &World, CCollision *pCollision, CCharacter *pOwner, int OwnerId,
		int WeaponType, const CTuningParams *pTuning, vec2 StartPos, vec2 Direction, float StartEnergy)
	{
		Buf.NextPath();
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
				return;
			}
		}
	}

	// Port of CProjectile::GetPos/Tick (projectile.cpp:48-168) for the local
	// prediction preview. FireWeapon grenades have m_Bouncing = 0, so the trajectory
	// stops on the first collision (explosion); lifetime ends also stop it.
	void SimulateProjectile(SAimDrawBuffer &Buf, CGameWorld &World, CCollision *pCollision, CCharacter *pOwner, int OwnerId,
		const CTuningParams *pSpawnTuning, vec2 StartPos, vec2 Direction, float SpeedFactor,
		int LifeSpan, float Curvature, float Speed, int TickSpeed)
	{
		(void)pSpawnTuning;
		(void)World;
		Buf.NextPath();
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
				return;
			}

			Buf.Line(Last, CurPos);
			Last = CurPos;
			PrevT = CurT;
		}
	}
} // namespace

void CRanbiAimLines::OnRender()
{
	// task-30/task-41: re-simulated on *every render frame* (current PredIntraGameTick
	// position smoothing, task-37 current input direction). No tick cache/throttle/fps
	// cap; the segment/step caps are only complexity limits.
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

	CGameWorld *pWorld = &GameClient()->m_PredictedWorld;
	CCharacter *pLocalChar = nullptr;
	int OwnerId = LocalId;
	int PracticeId = -1;
	bool UsePracticeCharacter = false;
	// ranbi: while BestClient Fast Practice is active, the controlled practice ghost
	// is the shooter: weapon/position/input/tune zone and the collision/hit world must
	// come from the practice simulation, not from the frozen server body.
	if(GameClient()->m_FastPractice.Active())
	{
		PracticeId = GameClient()->m_FastPractice.ControlledPracticeId();
		if(PracticeId >= 0 && PracticeId < MAX_CLIENTS)
		{
			CGameWorld &PracticeWorld = GameClient()->m_FastPractice.PracticeWorld();
			CCharacter *pPracticeChar = PracticeWorld.GetCharacterById(PracticeId);
			if(pPracticeChar != nullptr)
			{
				pWorld = &PracticeWorld;
				pLocalChar = pPracticeChar;
				OwnerId = PracticeId;
				UsePracticeCharacter = true;
			}
		}
	}
	CGameWorld &World = *pWorld;
	if(pLocalChar == nullptr)
		pLocalChar = World.GetCharacterById(LocalId);

	const bool UsePublishedCores = UsePracticeCharacter && PracticeId >= 0 && PracticeId < MAX_CLIENTS;
	const CCharacterCore &CurCore = UsePublishedCores ? GameClient()->m_aClients[PracticeId].m_Predicted : LocalClient.m_Predicted;
	const CCharacterCore &PrevCore = UsePublishedCores ? GameClient()->m_aClients[PracticeId].m_PrevPredicted : LocalClient.m_PrevPredicted;
	const float IntraTick = Client()->PredIntraGameTick(g_Config.m_ClDummy);
	const int Weapon = pLocalChar != nullptr ? pLocalChar->Core()->m_ActiveWeapon : CurCore.m_ActiveWeapon;
	if(Weapon != WEAPON_LASER && Weapon != WEAPON_SHOTGUN && Weapon != WEAPON_GRENADE)
		return;

	// task-37: FireWeapon aims with the *current* input target (character.cpp:268).
	vec2 Direction;
	if(CurCore.m_Input.m_TargetX != 0 || CurCore.m_Input.m_TargetY != 0)
		Direction = normalize(vec2((float)CurCore.m_Input.m_TargetX, (float)CurCore.m_Input.m_TargetY));
	else
		Direction = direction(CurCore.m_Angle / 256.0f * 2.0f * pi);
	if(length(Direction) < 0.001f)
		return;

	vec2 StartPos;
	if(UsePracticeCharacter)
		StartPos = mix(PrevCore.m_Pos, CurCore.m_Pos, IntraTick);
	else if(length(LocalClient.m_RenderPos) > 0.001f)
		StartPos = LocalClient.m_RenderPos; // already interpolated for the render frame
	else
		StartPos = CurCore.m_Pos;

	// character.cpp:413-467: CProjectile shots (vanilla shotgun, grenade) start at
	// ProjStartPos, but CLaser shots (laser, DDRace shotgun) start at the character
	// center (m_Pos). Keep both so the preview starts exactly where the real shot does.
	const float ProximityRadius = pLocalChar != nullptr ? pLocalChar->GetProximityRadius() : CCharacterCore::PhysicalSize();
	const vec2 ProjectileStartPos = StartPos + Direction * ProximityRadius * PROJECTILE_START_OFFSET;
	const vec2 LaserStartPos = StartPos;

	CCollision *pCollision = World.Collision();
	SAimSegment aSegments[MAX_AIM_SEGMENTS];
	SAimDrawBuffer Buf{aSegments, MAX_AIM_SEGMENTS};
	bool RenderAsLaser = false;
	int LaserType = LASERTYPE_RIFLE;

	if(Weapon == WEAPON_LASER)
	{
		// character.cpp:469-475: laser is a CLaser in every mode
		const int FireTuneZone = pLocalChar != nullptr ? ClampTuneZone(pLocalChar->GetOverriddenTuneZone()) : SpawnTuneZone(World, pCollision, LaserStartPos);
		const int LaserTuneZone = SpawnTuneZone(World, pCollision, LaserStartPos);
		const CTuningParams *pFireTuning = World.GetTuning(FireTuneZone);
		const CTuningParams *pTuning = World.GetTuning(LaserTuneZone);
		RenderAsLaser = true;
		LaserType = LASERTYPE_RIFLE;
		SimulateLaser(Buf, World, pCollision, pLocalChar, OwnerId, WEAPON_LASER, pTuning, LaserStartPos, Direction, (float)pFireTuning->m_LaserReach);
	}
	else if(Weapon == WEAPON_SHOTGUN)
	{
		if(World.m_WorldConfig.m_IsVanilla)
		{
			// character.cpp:413-437: vanilla shotgun = 5 CProjectiles with spread
			const float Angle = angle(Direction);
			const float SpeedHz = (float)Client()->GameTickSpeed();
			const CTuningParams *pGlobalTuning = World.GlobalTuning();
			const int ProjTuneZone = SpawnTuneZone(World, pCollision, ProjectileStartPos);
			const CTuningParams *pTuning = World.GetTuning(ProjTuneZone);
			const int LifeSpan = (int)(SpeedHz * (float)pGlobalTuning->m_ShotgunLifetime);
			for(int i = 0; i < 5; i++)
			{
				const float V = 1.0f - absolute(i - 2) / 2.0f;
				const float SpeedFactor = mix((float)pGlobalTuning->m_ShotgunSpeeddiff, 1.0f, V);
				SimulateProjectile(Buf, World, pCollision, pLocalChar, OwnerId, pTuning, ProjectileStartPos,
					direction(Angle + SHOTGUN_SPREAD[i]), SpeedFactor, LifeSpan,
					(float)pTuning->m_ShotgunCurvature, (float)pTuning->m_ShotgunSpeed, Client()->GameTickSpeed());
			}
		}
		else if(World.m_WorldConfig.m_IsDDRace)
		{
			// character.cpp:438-443: DDRace shotgun = one CLaser (no 5 spread shots)
			const int FireTuneZone = pLocalChar != nullptr ? ClampTuneZone(pLocalChar->GetOverriddenTuneZone()) : SpawnTuneZone(World, pCollision, LaserStartPos);
			const int LaserTuneZone = SpawnTuneZone(World, pCollision, LaserStartPos);
			const CTuningParams *pFireTuning = World.GetTuning(FireTuneZone);
			const CTuningParams *pTuning = World.GetTuning(LaserTuneZone);
			RenderAsLaser = true;
			LaserType = LASERTYPE_SHOTGUN;
			SimulateLaser(Buf, World, pCollision, pLocalChar, OwnerId, WEAPON_SHOTGUN, pTuning, LaserStartPos, Direction, (float)pFireTuning->m_LaserReach);
		}
		else
		{
			return;
		}
	}
	else // WEAPON_GRENADE
	{
		// character.cpp:449-467: one explosive CProjectile; FireWeapon defaults m_Bouncing = 0
		const int FireTuneZone = pLocalChar != nullptr ? ClampTuneZone(pLocalChar->GetOverriddenTuneZone()) : SpawnTuneZone(World, pCollision, ProjectileStartPos);
		const int ProjTuneZone = SpawnTuneZone(World, pCollision, ProjectileStartPos);
		const CTuningParams *pFireTuning = World.GetTuning(FireTuneZone);
		const CTuningParams *pTuning = World.GetTuning(ProjTuneZone);
		const int LifeSpan = (int)((float)Client()->GameTickSpeed() * (float)pFireTuning->m_GrenadeLifetime);
		SimulateProjectile(Buf, World, pCollision, pLocalChar, OwnerId, pTuning, ProjectileStartPos, Direction, 1.0f, LifeSpan,
			(float)pTuning->m_GrenadeCurvature, (float)pTuning->m_GrenadeSpeed, Client()->GameTickSpeed());
	}

	if(Buf.m_Num == 0)
		return;

	// Use the same world mapping as the items renderer so the vanilla visuals line up
	// exactly with the normal in-game laser/projectile rendering (camera zoom aware).
	const vec2 CameraPos = GameClient()->m_Camera.m_Center;
	const CScreenRect SavedScreen = Graphics()->GetScreen();
	const CScreenRect WorldScreen = Graphics()->MapScreenToWorld(
		CameraPos.x, CameraPos.y, 100.0f, 100.0f, 100.0f, 0, 0,
		Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom);
	Graphics()->MapScreen(WorldScreen);

	if(RenderAsLaser)
	{
		// Same colors/ticks/types as CItems::RenderLaser(CLaserData*, bool):
		// RIFLE uses the rifle outline/inner colors, SHOTGUN the shotgun ones; a
		// just-fired laser body has TicksBody = 0 (full outline/inner width), the head
		// animation uses the current render tick like the fired laser.
		const unsigned int ColorOut = LaserType == LASERTYPE_SHOTGUN ? g_Config.m_ClLaserShotgunOutlineColor : g_Config.m_ClLaserRifleOutlineColor;
		const unsigned int ColorIn = LaserType == LASERTYPE_SHOTGUN ? g_Config.m_ClLaserShotgunInnerColor : g_Config.m_ClLaserRifleInnerColor;
		const ColorRGBA OuterColor = color_cast<ColorRGBA>(ColorHSLA(ColorOut).WithAlpha(1.0f));
		const ColorRGBA InnerColor = color_cast<ColorRGBA>(ColorHSLA(ColorIn).WithAlpha(1.0f));
		const float TicksHead = (float)Client()->GameTick(g_Config.m_ClDummy) + Client()->PredIntraGameTick(g_Config.m_ClDummy);
		// The real fired beam is rendered with the laser's age, so its width has already
		// started fading (RenderWeaponLaser/CItems::RenderLaser width = 1 - age/bounceDelay).
		// A preview with TicksBody = 0 is therefore about twice as wide as the real beam.
		// Use the same trick as the settings laser preview (TicksBody = 4 for a thin,
		// aligned beam) but derive it from the tuning so the requested 0.5 width factor
		// holds in every tune zone.
		constexpr float PREVIEW_WIDTH_FACTOR = 0.5f;
		const int PreviewTuneZone = (Client()->State() == IClient::STATE_ONLINE && GameClient()->m_GameWorld.m_WorldConfig.m_UseTuneZones) ? Collision()->IsTune(Collision()->GetMapIndex(Buf.m_pSegments[0].m_From)) : 0;
		const float PreviewBounceDelay = GameClient()->GetTuning(PreviewTuneZone)->m_LaserBounceDelay;
		const float PreviewTicksBody = (1.0f - PREVIEW_WIDTH_FACTOR) * PreviewBounceDelay * Client()->GameTickSpeed() / 1000.0f;
		for(int i = 0; i < Buf.m_Num; i++)
		{
			if(Buf.m_pSegments[i].m_To == Buf.m_pSegments[i].m_From)
				continue;
			// task-58: body-only preview (no BestClient laser glow/head VFX per segment);
			// the vanilla outline/inner colors and geometric beam are unchanged.
			GameClient()->m_Items.RenderLaserPreview(Buf.m_pSegments[i].m_From, Buf.m_pSegments[i].m_To, OuterColor, InnerColor, PreviewTicksBody, LaserType);
		}
	}
	else
	{
		// Same projectile visuals as CItems::RenderProjectile: sprite rotation + SmokeTrail
		// (grenade) / BulletTrail (shotgun). Sample the simulated trajectory instead of
		// spawning a full particle per segment (keeps the per-frame cost bounded).
		int Index = 0;
		while(Index < Buf.m_Num)
		{
			const int Path = Buf.m_pSegments[Index].m_Path;
			int End = Index;
			while(End + 1 < Buf.m_Num && Buf.m_pSegments[End + 1].m_Path == Path)
				End++;
			const int NumSegments = End - Index + 1;
			int TrailSamples = 0;
			for(int i = 0; i < NumSegments; i++)
			{
				const bool Last = i == NumSegments - 1;
				if(!Last && (i % PROJECTILE_SAMPLE_STEP != 0 || TrailSamples >= MAX_PROJECTILE_TRAIL_SAMPLES))
					continue;
				TrailSamples++;
				const SAimSegment &Seg = Buf.m_pSegments[Index + i];
				const vec2 Vel = Seg.m_To - Seg.m_From;
				GameClient()->m_Items.RenderProjectileTrailPreview(Seg.m_To, Vel, Weapon, 1.0f, Last, Weapon == WEAPON_GRENADE);
			}
			Index = End + 1;
		}
	}

	Graphics()->MapScreen(SavedScreen);
}
