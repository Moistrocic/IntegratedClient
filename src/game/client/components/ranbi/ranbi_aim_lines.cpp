// ranbi: weapon aim lines, faithful weapon physics preview (task-19) with
// configurable per-weapon colors/thickness and a distinct "hit player" style
// (task-24). Rendering uses textured quads instead of 1px LinesDraw, and the
// start position/direction are interpolated with the render intra-tick to avoid
// the 50 Hz step. Read-only rendering for the local player only.

#include "ranbi_aim_lines.h"

#include <base/color.h>
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
	constexpr int MAX_AIM_SEGMENTS = 512;
	constexpr int MAX_LASER_SEGMENTS = 64; // defensive cap; CLaser uses m_LaserBounceNum
	constexpr int MAX_PROJECTILE_TICKS = 200; // defensive cap; grenade default = 2s * 50 = 100
	constexpr float HIT_MARKER_HALF_SIZE = 4.0f;
	// character.cpp:310: ProjStartPos = m_Pos + Direction * m_ProximityRadius * 0.75f
	constexpr float PROJECTILE_START_OFFSET = 0.75f;

	// vanilla shotgun spread, character.cpp:420
	const float SHOTGUN_SPREAD[] = {-0.185f, -0.070f, 0.0f, 0.070f, 0.185f};

	struct SAimSegment
	{
		vec2 m_From;
		vec2 m_To;
		bool m_HitPlayer;
	};

	struct SAimDrawBuffer
	{
		SAimSegment *m_pSegments;
		int m_Max;
		int m_Num = 0;

		void Line(vec2 From, vec2 To, bool HitPlayer = false)
		{
			if(m_Num >= m_Max)
				return;
			m_pSegments[m_Num++] = SAimSegment{From, To, HitPlayer};
		}
	};

	void AddHitMarker(SAimDrawBuffer &Buf, vec2 Pos, bool HitPlayer)
	{
		Buf.Line(Pos - vec2(HIT_MARKER_HALF_SIZE, 0.0f), Pos + vec2(HIT_MARKER_HALF_SIZE, 0.0f), HitPlayer);
		Buf.Line(Pos - vec2(0.0f, HIT_MARKER_HALF_SIZE), Pos + vec2(0.0f, HIT_MARKER_HALF_SIZE), HitPlayer);
	}

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
				Buf.Line(Last, At, true);
				AddHitMarker(Buf, At, true);
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
				Buf.Line(Last, To, false);
				Buf.Line(To, Pos, false);
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
				Buf.Line(Last, Pos, false);
				AddHitMarker(Buf, Pos, false);
				return;
			}
		}

		AddHitMarker(Buf, Last, false);
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
				const bool HitPlayer = pHit != nullptr;
				// projectile.cpp:107: explosions happen at ColPos; character hits use At.
				const vec2 HitPos = Collide ? ColPos : (HitPlayer ? At : ColPos);
				Buf.Line(Last, HitPos, HitPlayer);
				AddHitMarker(Buf, HitPos, HitPlayer);
				return;
			}

			Buf.Line(Last, CurPos, false);
			Last = CurPos;
			PrevT = CurT;

			if(Tick == Steps)
			{
				AddHitMarker(Buf, Last, false);
				return;
			}
		}
		AddHitMarker(Buf, Last, false);
	}

	// character.cpp:268: Direction = normalize(vec2(LatestInput.m_TargetX, m_TargetY))
	vec2 SmoothAimDirection(const CCharacterCore &PrevCore, const CCharacterCore &CurCore, float IntraTick)
	{
		const vec2 PrevTarget((float)PrevCore.m_Input.m_TargetX, (float)PrevCore.m_Input.m_TargetY);
		const vec2 CurTarget((float)CurCore.m_Input.m_TargetX, (float)CurCore.m_Input.m_TargetY);
		const vec2 Target = mix(PrevTarget, CurTarget, IntraTick);
		if(length(Target) > 0.001f)
			return normalize(Target);

		// fall back to the interpolated body angle (predicted cores are quantized to
		// the game tick; the angle must wrap smoothly across +-pi).
		float PrevAngle = PrevCore.m_Angle / 256.0f * 2.0f * pi;
		float CurAngle = CurCore.m_Angle / 256.0f * 2.0f * pi;
		float Delta = CurAngle - PrevAngle;
		while(Delta > pi)
			Delta -= 2.0f * pi;
		while(Delta < -pi)
			Delta += 2.0f * pi;
		return direction(PrevAngle + Delta * IntraTick);
	}

	ColorRGBA AimLineColor(unsigned int HslaColor)
	{
		return color_cast<ColorRGBA>(ColorHSLA(HslaColor, true));
	}
} // namespace

void CRanbiAimLines::OnRender()
{
	// task-30: this component is re-simulated on *every render frame* with the
	// current PredIntraGameTick() (see SmoothAimDirection below). There is no game
	// tick cache / throttle / fps cap here; only the defensive segment and step caps
	// limit simulation complexity (they are not a frame-rate limit).
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

	// task-24: smooth the start position/direction with the render intra-tick. The
	// predicted cores are published for the local player and (in fast practice) for
	// the controlled practice participant, so both paths avoid 50 Hz stepping.
	const bool UsePublishedCores = UsePracticeCharacter && PracticeId >= 0 && PracticeId < MAX_CLIENTS;
	const CCharacterCore &CurCore = UsePublishedCores ? GameClient()->m_aClients[PracticeId].m_Predicted : LocalClient.m_Predicted;
	const CCharacterCore &PrevCore = UsePublishedCores ? GameClient()->m_aClients[PracticeId].m_PrevPredicted : LocalClient.m_PrevPredicted;
	const float IntraTick = Client()->PredIntraGameTick(g_Config.m_ClDummy);
	const int Weapon = pLocalChar != nullptr ? pLocalChar->Core()->m_ActiveWeapon : CurCore.m_ActiveWeapon;
	if(Weapon != WEAPON_LASER && Weapon != WEAPON_SHOTGUN && Weapon != WEAPON_GRENADE)
		return;

	vec2 Direction = SmoothAimDirection(PrevCore, CurCore, IntraTick);
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

	ColorRGBA WeaponColor;
	float WeaponWidth = 1.0f;
	const ColorRGBA PlayerColor = AimLineColor(g_Config.m_RcWeaponAimPlayerColor);
	const float PlayerWidth = (float)g_Config.m_RcWeaponAimPlayerWidth;

	if(Weapon == WEAPON_LASER)
	{
		// character.cpp:469-475: laser is a CLaser in every mode
		const int FireTuneZone = pLocalChar != nullptr ? ClampTuneZone(pLocalChar->GetOverriddenTuneZone()) : SpawnTuneZone(World, pCollision, LaserStartPos);
		const int LaserTuneZone = SpawnTuneZone(World, pCollision, LaserStartPos);
		const CTuningParams *pFireTuning = World.GetTuning(FireTuneZone);
		const CTuningParams *pTuning = World.GetTuning(LaserTuneZone);
		WeaponColor = AimLineColor(g_Config.m_RcWeaponAimLaserColor);
		WeaponWidth = (float)g_Config.m_RcWeaponAimLaserWidth;
		SimulateLaser(Buf, World, pCollision, pLocalChar, OwnerId, WEAPON_LASER, pTuning, LaserStartPos, Direction, (float)pFireTuning->m_LaserReach);
	}
	else if(Weapon == WEAPON_SHOTGUN)
	{
		WeaponColor = AimLineColor(g_Config.m_RcWeaponAimShotgunColor);
		WeaponWidth = (float)g_Config.m_RcWeaponAimShotgunWidth;
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
			SimulateLaser(Buf, World, pCollision, pLocalChar, OwnerId, WEAPON_SHOTGUN, pTuning, LaserStartPos, Direction, (float)pFireTuning->m_LaserReach);
		}
		else
		{
			// FireWeapon has no branch for other modes -> no trajectory
			return;
		}
	}
	else // WEAPON_GRENADE
	{
		WeaponColor = AimLineColor(g_Config.m_RcWeaponAimGrenadeColor);
		WeaponWidth = (float)g_Config.m_RcWeaponAimGrenadeWidth;
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

	// Draw in world coordinates with the actual camera zoom; width is scaled by the
	// camera zoom like the hook collision line (players.cpp:442-490). Contiguous
	// segments are converted to a shared-vertex miter strip: the same joint offset is
	// used by both adjacent quads, so there is neither a gap at corners (no separate
	// unjoined segments) nor area overlap (alpha does not accumulate/darken at joints).
	const vec2 CameraPos = GameClient()->m_Camera.m_Center;
	const float CameraZoom = GameClient()->m_Camera.m_Zoom;
	const CScreenRect SavedScreen = Graphics()->GetScreen();
	const CScreenRect WorldScreen = Graphics()->MapScreenToWorld(
		CameraPos.x, CameraPos.y, 100.0f, 100.0f, 100.0f, 0, 0,
		Graphics()->ScreenAspect(), CameraZoom);
	Graphics()->MapScreen(WorldScreen);

	Graphics()->TextureClear();
	auto DrawStrip = [&](bool HitStyle, const ColorRGBA &Color, float Width) {
		IGraphics::CFreeformItem aQuads[MAX_AIM_SEGMENTS];
		vec2 aPoints[MAX_AIM_SEGMENTS + 2];
		vec2 aOffsets[MAX_AIM_SEGMENTS + 2];
		int NumQuads = 0;
		const float HalfWidth = Width * CameraZoom * 0.5f;

		int i = 0;
		while(i < Buf.m_Num && NumQuads < MAX_AIM_SEGMENTS)
		{
			if(aSegments[i].m_HitPlayer != HitStyle)
			{
				i++;
				continue;
			}

			// Collect one contiguous chain of same-style segments (a new shotgun
			// projectile or a break in the chain starts a new strip).
			int j = i;
			while(j + 1 < Buf.m_Num && aSegments[j + 1].m_HitPlayer == HitStyle &&
				distance(aSegments[j].m_To, aSegments[j + 1].m_From) < 0.01f)
				j++;

			int NumPoints = 0;
			aPoints[NumPoints++] = aSegments[i].m_From;
			for(int k = i; k <= j; k++)
				aPoints[NumPoints++] = aSegments[k].m_To;

			// Miter offsets: at an interior joint both adjacent quads share this exact
			// offset, which keeps a continuous strip and avoids double blending.
			for(int k = 0; k < NumPoints; k++)
			{
				const vec2 PrevDir = k > 0 ? normalize(aPoints[k] - aPoints[k - 1]) : normalize(aPoints[1] - aPoints[0]);
				const vec2 NextDir = k + 1 < NumPoints ? normalize(aPoints[k + 1] - aPoints[k]) : PrevDir;
				const vec2 PrevNormal(-PrevDir.y, PrevDir.x);
				const vec2 NextNormal(-NextDir.y, NextDir.x);
				vec2 Miter = PrevNormal + NextNormal;
				if(length(Miter) < 0.001f)
					Miter = NextNormal;
				else
					Miter = normalize(Miter);
				const float Denom = std::max(dot(Miter, NextNormal), 0.25f);
				const float Scale = std::min(HalfWidth / Denom, HalfWidth * 4.0f);
				aOffsets[k] = Miter * Scale;
			}

			for(int k = i; k <= j && NumQuads < MAX_AIM_SEGMENTS; k++)
			{
				const int Local = k - i;
				aQuads[NumQuads++] = IGraphics::CFreeformItem(
					aPoints[Local] + aOffsets[Local],
					aPoints[Local + 1] + aOffsets[Local + 1],
					aPoints[Local + 1] - aOffsets[Local + 1],
					aPoints[Local] - aOffsets[Local]);
			}
			i = j + 1;
		}

		if(NumQuads == 0)
			return;
		Graphics()->QuadsBegin();
		Graphics()->SetColor(Color);
		Graphics()->QuadsDrawFreeform(aQuads, NumQuads);
		Graphics()->QuadsEnd();
	};
	DrawStrip(false, WeaponColor, WeaponWidth);
	DrawStrip(true, PlayerColor, PlayerWidth);

	Graphics()->MapScreen(SavedScreen);
}
