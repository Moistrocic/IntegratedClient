// ranbi: task-12 "weapon aim lines" feature.
// Predicts local-only aim lines for laser / shotgun / grenade using the base
// collision API. Default off (rc_weapon_aim_lines); rendering only.

#include "ranbi_aim_lines.h"

#include <base/math.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/gameclient.h>

namespace
{
	// DDNet default tuning values (tuning.h): laser_reach / grenade_speed /
	// grenade_curvature. Not read from the tuning object to keep the preview
	// approximation simple and independent from the current map tune zones.
	constexpr float AIM_START_OFFSET = 14.0f;
	constexpr float LASER_REACH = 800.0f;
	constexpr float SHOTGUN_REACH = 800.0f;
	constexpr float GRENADE_SPEED = 1000.0f;
	// pos.y = start.y + dir.y * speed * t + curvature / 10000 * (speed * t)^2
	// => gravity acceleration = 2 * curvature / 10000 * speed^2.
	constexpr float GRENADE_CURVATURE = 7.0f;
	constexpr float GRENADE_GRAVITY = 2.0f * GRENADE_CURVATURE / 10000.0f * GRENADE_SPEED * GRENADE_SPEED;
	constexpr float GRENADE_LIFETIME = 2.0f;
	constexpr float GRENADE_STEP = 1.0f / 50.0f;
	constexpr int GRENADE_MAX_STEPS = (int)(GRENADE_LIFETIME / GRENADE_STEP);
	constexpr float GRENADE_BOUNCE_DAMPING = 0.5f;
	constexpr float GRENADE_MIN_SPEED = 40.0f;
	constexpr float HIT_MARKER_HALF_SIZE = 4.0f;
	constexpr int MAX_AIM_LINES = 128;
	// Vanilla shotgun spread angles (character.cpp aSpreading[]). DDRace shotgun
	// fires a laser-like projectile; the five lines are a visual approximation.
	const float SHOTGUN_SPREAD[] = {-0.185f, -0.070f, 0.0f, 0.070f, 0.185f};
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

	const int Weapon = LocalClient.m_Predicted.m_ActiveWeapon;
	if(Weapon != WEAPON_LASER && Weapon != WEAPON_SHOTGUN && Weapon != WEAPON_GRENADE)
		return;

	vec2 AimDir;
	const auto &LocalCharacterInfo = GameClient()->m_Snap.m_aCharacters[LocalId];
	if(LocalCharacterInfo.m_HasExtendedDisplayInfo && (LocalCharacterInfo.m_ExtendedData.m_TargetX != 0 || LocalCharacterInfo.m_ExtendedData.m_TargetY != 0))
		AimDir = normalize(vec2((float)LocalCharacterInfo.m_ExtendedData.m_TargetX, (float)LocalCharacterInfo.m_ExtendedData.m_TargetY));
	else
		AimDir = direction(LocalClient.m_Predicted.m_Angle / 256.0f * 2.0f * pi);
	if(length(AimDir) < 0.001f)
		return;

	const vec2 Start = LocalClient.m_Predicted.m_Pos + AimDir * AIM_START_OFFSET;

	IGraphics::CLineItem aLines[MAX_AIM_LINES];
	int NumLines = 0;
	auto AddLine = [&](vec2 From, vec2 To) {
		if(NumLines >= MAX_AIM_LINES)
			return;
		aLines[NumLines++] = IGraphics::CLineItem(From, To);
	};
	auto AddHitMarker = [&](vec2 Pos) {
		AddLine(Pos - vec2(HIT_MARKER_HALF_SIZE, 0.0f), Pos + vec2(HIT_MARKER_HALF_SIZE, 0.0f));
		AddLine(Pos - vec2(0.0f, HIT_MARKER_HALF_SIZE), Pos + vec2(0.0f, HIT_MARKER_HALF_SIZE));
	};
	auto TraceLine = [&](vec2 Dir, float Reach) {
		const vec2 Target = Start + Dir * Reach;
		vec2 ColPos = Target;
		vec2 BeforePos = Target;
		vec2 End = Target;
		if(Collision()->IntersectLine(Start, Target, &ColPos, &BeforePos) != 0)
			End = BeforePos;

		// Stop at the closest other player in front of the wall hit (approximate
		// tee hitbox with the physical radius) so player hits are visible too.
		const vec2 Trace = End - Start;
		const float TraceLen = length(Trace);
		if(TraceLen > 0.001f)
		{
			const vec2 TraceDir = Trace / TraceLen;
			float BestT = TraceLen;
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(i == LocalId)
					continue;
				const CGameClient::CClientData &OtherClient = GameClient()->m_aClients[i];
				if(!OtherClient.m_Active)
					continue;
				const vec2 Rel = OtherClient.m_Predicted.m_Pos - Start;
				const float T = Rel.x * TraceDir.x + Rel.y * TraceDir.y;
				if(T <= 0.0f || T >= BestT)
					continue;
				const vec2 Perpendicular = Rel - TraceDir * T;
				if(length(Perpendicular) <= 14.0f)
					BestT = T;
			}
			if(BestT < TraceLen)
				End = Start + TraceDir * BestT;
		}
		return End;
	};

	ColorRGBA Color;
	if(Weapon == WEAPON_LASER)
	{
		Color = ColorRGBA(1.0f, 0.25f, 0.25f, 0.55f);
		const vec2 End = TraceLine(AimDir, LASER_REACH);
		AddLine(Start, End);
		AddHitMarker(End);
	}
	else if(Weapon == WEAPON_SHOTGUN)
	{
		Color = ColorRGBA(1.0f, 0.9f, 0.35f, 0.5f);
		const float AimAngle = angle(AimDir);
		for(const float Spread : SHOTGUN_SPREAD)
		{
			const vec2 End = TraceLine(direction(AimAngle + Spread), SHOTGUN_REACH);
			AddLine(Start, End);
			AddHitMarker(End);
		}
	}
	else
	{
		Color = ColorRGBA(1.0f, 0.55f, 0.1f, 0.6f);
		vec2 Pos = Start;
		vec2 Vel = AimDir * GRENADE_SPEED;
		for(int Step = 0; Step < GRENADE_MAX_STEPS; Step++)
		{
			Vel.y += GRENADE_GRAVITY * GRENADE_STEP;
			const vec2 Next = Pos + Vel * GRENADE_STEP;
			vec2 ColPos = Next;
			vec2 BeforePos = Next;
			if(Collision()->IntersectLine(Pos, Next, &ColPos, &BeforePos) != 0)
			{
				AddLine(Pos, BeforePos);
				Pos = BeforePos;

				// Approximate bounce: probe which side of the collision point is solid.
				const bool SolidX = Collision()->CheckPoint(BeforePos + vec2(-2.0f, 0.0f)) ||
						    Collision()->CheckPoint(BeforePos + vec2(2.0f, 0.0f));
				const bool SolidY = Collision()->CheckPoint(BeforePos + vec2(0.0f, -2.0f)) ||
						    Collision()->CheckPoint(BeforePos + vec2(0.0f, 2.0f));
				if(SolidX)
				{
					Vel.x = -Vel.x * GRENADE_BOUNCE_DAMPING;
					Pos.x = BeforePos.x + (Vel.x > 0.0f ? 1.0f : -1.0f);
				}
				else if(SolidY)
				{
					Vel.y = -Vel.y * GRENADE_BOUNCE_DAMPING;
					Pos.y = BeforePos.y + (Vel.y > 0.0f ? 1.0f : -1.0f);
				}
				else if(absolute(Vel.x) > absolute(Vel.y))
				{
					Vel.x = -Vel.x * GRENADE_BOUNCE_DAMPING;
					Pos.x = BeforePos.x + (Vel.x > 0.0f ? 1.0f : -1.0f);
				}
				else
				{
					Vel.y = -Vel.y * GRENADE_BOUNCE_DAMPING;
					Pos.y = BeforePos.y + (Vel.y > 0.0f ? 1.0f : -1.0f);
				}

				if(length(Vel) < GRENADE_MIN_SPEED)
					break;
			}
			else
			{
				AddLine(Pos, Next);
				Pos = Next;
			}
		}
		AddHitMarker(Pos);
	}

	if(NumLines == 0)
		return;

	// Draw in world coordinates; the mapping is set explicitly like the Ranbi
	// weapon angle renderer, so the lines do not depend on another component's
	// mapping state.
	const vec2 CameraPos = GameClient()->m_Camera.m_Center;
	const CScreenRect SavedScreen = Graphics()->GetScreen();
	const CScreenRect WorldScreen = Graphics()->MapScreenToWorld(CameraPos.x, CameraPos.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->ScreenAspect(), 1.0f);
	Graphics()->MapScreen(WorldScreen);

	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->SetColor(Color);
	Graphics()->LinesDraw(aLines, NumLines);
	Graphics()->LinesEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->MapScreen(SavedScreen);
}
