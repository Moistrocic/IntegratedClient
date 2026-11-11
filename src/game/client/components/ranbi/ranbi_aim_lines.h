// ranbi: task-12 "weapon aim lines" feature.
// Draws local-only predicted aim lines for the laser, shotgun and grenade
// (rc_weapon_aim_lines). No gameplay/network behavior is changed.
#ifndef GAME_CLIENT_COMPONENTS_RANBI_RANBI_AIM_LINES_H
#define GAME_CLIENT_COMPONENTS_RANBI_RANBI_AIM_LINES_H

#include <game/client/component.h>

class CRanbiAimLines : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnRender() override;
};

#endif
