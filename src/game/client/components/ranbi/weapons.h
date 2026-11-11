// ranbi: ported from RanbiClient 1.0.2 (DDNet 19.8 / TClient 10.8.7).
// Weapon angle markers (rc_show_weapons_angle*) and the recent-weapon switch.
// The list editor GUI is added by a later Ranbi settings page task; this
// component only provides rendering, the console commands and config storage.
#ifndef GAME_CLIENT_COMPONENTS_RANBI_WEAPONS_H
#define GAME_CLIENT_COMPONENTS_RANBI_WEAPONS_H

#include <engine/client/enums.h>
#include <engine/console.h>

#include <game/client/component.h>

#include <vector>

class IConfigManager;

struct SWeaponInfo
{
	int m_WeaponID;
	float m_AngleStart;
	float m_AngleEnd;
	unsigned int m_Color;
	char m_aNote[64];
};

class CWeapons : public CComponent
{
	int m_aRecentWeapon[NUM_DUMMIES];
	int m_aLastActiveWeapon[NUM_DUMMIES];

	static void ConSwitchRecentWeapon(IConsole::IResult *pResult, void *pUserData);
	static void ConAddWeaponAngle(IConsole::IResult *pResult, void *pUserData);
	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);

public:
	CWeapons();
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnUpdate() override;
	void OnRender() override;
	void OnReset() override;

	std::vector<SWeaponInfo> m_vWeapons;
};

#endif
