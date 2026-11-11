// ranbi: ported from RanbiClient 1.0.2 (DDNet 19.8 / TClient 10.8.7).
#include "weapons.h"

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/config.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/gamecore.h>

#include <cmath>
#include <vector>

static void EscapeConfigString(const char *pFrom, char *pTo, size_t ToSize)
{
	size_t Out = 0;
	for(; *pFrom && Out + 2 < ToSize; pFrom++)
	{
		if(*pFrom == '\\' || *pFrom == '"')
			pTo[Out++] = '\\';
		else if(*pFrom == '\n' || *pFrom == '\r')
		{
			// config files are line based, never write line breaks into a quoted string
			pTo[Out++] = ' ';
			continue;
		}
		pTo[Out++] = *pFrom;
	}
	pTo[Out] = '\0';
}

CWeapons::CWeapons()
{
	OnReset();
}

void CWeapons::OnReset()
{
	for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
	{
		m_aRecentWeapon[Dummy] = -1;
		m_aLastActiveWeapon[Dummy] = -1;
	}
}

void CWeapons::OnConsoleInit()
{
	ConfigManager()->RegisterCallback(ConfigSaveCallback, this, ConfigDomain::RANBIWEAPONS);
	Console()->Register("add_weapon_angle", "i[weapon_id] f[angle_start] f[angle_end] i[color] ?s[note]", CFGFLAG_CLIENT, ConAddWeaponAngle, this, "Add a weapon angle config");
	Console()->Register("+switch_recent_weapon", "", CFGFLAG_CLIENT, ConSwitchRecentWeapon, this, "Switch to most recently used weapon");
}

void CWeapons::ConAddWeaponAngle(IConsole::IResult *pResult, void *pUserData)
{
	CWeapons *pThis = (CWeapons *)pUserData;
	SWeaponInfo Weapon;
	Weapon.m_WeaponID = pResult->GetInteger(0);
	Weapon.m_AngleStart = pResult->GetFloat(1);
	Weapon.m_AngleEnd = pResult->GetFloat(2);
	Weapon.m_Color = (unsigned int)pResult->GetInteger(3);
	str_copy(Weapon.m_aNote, pResult->GetString(4));
	pThis->m_vWeapons.push_back(Weapon);
}

void CWeapons::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	CWeapons *pThis = (CWeapons *)pUserData;

	for(const SWeaponInfo &Weapon : pThis->m_vWeapons)
	{
		char aEscapedNote[sizeof(Weapon.m_aNote) * 2 + 1];
		EscapeConfigString(Weapon.m_aNote, aEscapedNote, sizeof(aEscapedNote));
		char aBuf[256 + sizeof(aEscapedNote)];
		str_format(aBuf, sizeof(aBuf), "add_weapon_angle %d %.2f %.2f %d \"%s\"",
			Weapon.m_WeaponID, Weapon.m_AngleStart, Weapon.m_AngleEnd, (int)Weapon.m_Color, aEscapedNote);
		pConfigManager->WriteLine(aBuf, ConfigDomain::RANBIWEAPONS);
	}
}

void CWeapons::OnUpdate()
{
	for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
	{
		int LocalId = GameClient()->m_aLocalIds[Dummy];
		if(LocalId < 0 || LocalId >= MAX_CLIENTS)
			continue;

		const CGameClient::CClientData &Client = GameClient()->m_aClients[LocalId];
		if(!Client.m_Active)
			continue;

		int CurrentWeapon = Client.m_Predicted.m_ActiveWeapon;
		if(CurrentWeapon < 0 || CurrentWeapon >= NUM_WEAPONS)
			continue;

		if(CurrentWeapon != m_aLastActiveWeapon[Dummy])
		{
			if(m_aLastActiveWeapon[Dummy] >= 0 && m_aLastActiveWeapon[Dummy] < NUM_WEAPONS &&
				Client.m_Predicted.m_aWeapons[m_aLastActiveWeapon[Dummy]].m_Got)
			{
				m_aRecentWeapon[Dummy] = m_aLastActiveWeapon[Dummy];
			}
			m_aLastActiveWeapon[Dummy] = CurrentWeapon;
		}
	}
}

void CWeapons::OnRender()
{
	if(!g_Config.m_RcShowWeaponsAngle)
		return;

	if(m_vWeapons.empty())
		return;

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const int CurId = GameClient()->m_Snap.m_SpecInfo.m_Active &&
					  GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW ?
				  GameClient()->m_Snap.m_SpecInfo.m_SpectatorId :
				  GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(CurId < 0 || CurId >= MAX_CLIENTS)
		return;

	const CGameClient::CClientData &Client = GameClient()->m_aClients[CurId];
	if(!Client.m_Active)
		return;

	const int ActiveWeapon = Client.m_Predicted.m_ActiveWeapon;
	const vec2 Pos = GameClient()->m_Camera.m_Center;
	float AimRad;
	if(GameClient()->m_Snap.m_aCharacters[CurId].m_HasExtendedDisplayInfo)
	{
		const CNetObj_DDNetCharacter &Ext = GameClient()->m_Snap.m_aCharacters[CurId].m_ExtendedData;
		AimRad = std::atan2((float)Ext.m_TargetY, (float)Ext.m_TargetX);
	}
	else
	{
		AimRad = Client.m_Predicted.m_Angle / 256.0f * 2.0f * pi;
	}
	if(AimRad < 0.0f)
		AimRad += 2.0f * pi;
	const float AimDeg = AimRad * 180.0f / pi;

	// The fan is drawn in world coordinates. The camera mapping is set explicitly
	// instead of relying on CHud::RenderCursor() leaving the world mapping active
	// (source does the latter), so the feature also works with tc_cursor_scale 0.
	const CScreenRect SavedScreen = Graphics()->GetScreen();
	const CScreenRect WorldScreen = Graphics()->MapScreenToWorld(Pos.x, Pos.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->ScreenAspect(), 1.0f);
	Graphics()->MapScreen(WorldScreen);

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	for(const SWeaponInfo &Weapon : m_vWeapons)
	{
		if(Weapon.m_WeaponID != ActiveWeapon)
			continue;

		bool Matched = false;
		if(Weapon.m_AngleEnd >= Weapon.m_AngleStart)
			Matched = AimDeg >= Weapon.m_AngleStart && AimDeg <= Weapon.m_AngleEnd;
		else
			Matched = AimDeg >= Weapon.m_AngleStart || AimDeg <= Weapon.m_AngleEnd;

		float StartRad = Weapon.m_AngleStart * pi / 180.0f;
		float EndRad = Weapon.m_AngleEnd * pi / 180.0f;
		if(EndRad < StartRad)
			EndRad += 2.0f * pi;

		const ColorRGBA Col = color_cast<ColorRGBA>(ColorHSLA(Weapon.m_Color));
		Graphics()->SetColor(Col.r, Col.g, Col.b, Matched ? 0.8f : 0.5f);

		const float InnerRadius = (float)g_Config.m_RcWeaponsAngleInnerRadius;
		const float OuterRadius = (float)g_Config.m_RcWeaponsAngleRadius;
		const int Segments = 32;
		const float AngleStep = (EndRad - StartRad) / (float)Segments;

		std::vector<IGraphics::CFreeformItem> vItems;
		vItems.reserve(Segments);

		for(int i = 0; i < Segments; i++)
		{
			float A1 = StartRad + AngleStep * (float)i;
			float A2 = StartRad + AngleStep * (float)(i + 1);
			vec2 Inner1 = Pos + vec2(std::cos(A1), std::sin(A1)) * InnerRadius;
			vec2 Outer1 = Pos + vec2(std::cos(A1), std::sin(A1)) * OuterRadius;
			vec2 Inner2 = Pos + vec2(std::cos(A2), std::sin(A2)) * InnerRadius;
			vec2 Outer2 = Pos + vec2(std::cos(A2), std::sin(A2)) * OuterRadius;

			vItems.emplace_back(Inner1.x, Inner1.y, Outer1.x, Outer1.y, Inner2.x, Inner2.y, Outer2.x, Outer2.y);
		}

		Graphics()->QuadsDrawFreeform(vItems.data(), (int)vItems.size());

		if(Matched && Weapon.m_aNote[0] != '\0')
		{
			Graphics()->QuadsEnd();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);

			const vec2 TextPos = GameClient()->m_Controls.m_aTargetPos[g_Config.m_ClDummy] + vec2(12.0f, 12.0f);
			const CScreenRect OldScreen = Graphics()->GetScreen();
			Graphics()->MapScreenToInterface(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y);

			const float FontSize = (float)g_Config.m_RcWeaponsAngleFontSize;
			const float TextW = TextRender()->TextWidth(FontSize, Weapon.m_aNote);

			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			const float Pad = 3.0f;
			Graphics()->SetColor(1.0f - Col.r, 1.0f - Col.g, 1.0f - Col.b, 0.5f);
			IGraphics::CFreeformItem BgItem(
				TextPos.x - TextW * 0.5f - Pad, TextPos.y - Pad,
				TextPos.x + TextW * 0.5f + Pad, TextPos.y - Pad,
				TextPos.x - TextW * 0.5f - Pad, TextPos.y + FontSize + Pad,
				TextPos.x + TextW * 0.5f + Pad, TextPos.y + FontSize + Pad);
			Graphics()->QuadsDrawFreeform(&BgItem, 1);
			Graphics()->QuadsEnd();

			TextRender()->TextColor(Col);
			TextRender()->Text(TextPos.x - TextW * 0.5f, TextPos.y, FontSize, Weapon.m_aNote);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			Graphics()->MapScreen(OldScreen);

			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
		}
	}

	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->MapScreen(SavedScreen);
}

void CWeapons::ConSwitchRecentWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CWeapons *pThis = (CWeapons *)pUserData;
	if(!pResult->GetInteger(0))
		return;

	const int Dummy = g_Config.m_ClDummy;
	const int RecentWeapon = pThis->m_aRecentWeapon[Dummy];
	if(RecentWeapon < 0 || RecentWeapon >= NUM_WEAPONS)
		return;

	const int LocalId = pThis->GameClient()->m_aLocalIds[Dummy];
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return;

	const CGameClient::CClientData &Client = pThis->GameClient()->m_aClients[LocalId];
	if(!Client.m_Active)
		return;

	if(Client.m_Predicted.m_aWeapons[RecentWeapon].m_Got)
		pThis->GameClient()->m_Controls.m_aInputData[Dummy].m_WantedWeapon = RecentWeapon + 1;
}
