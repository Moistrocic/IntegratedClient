// integratedclient
// Ranbi settings page framework (task-3).
//
// The Ranbi feature pages are implemented by the later Ranbi integration tasks.
// Only fill in the bodies of the RenderSettingsRanbi* functions below; do not add
// a separate top-level page or "R" menu entry. The tab list, settings navigation
// entries and the Info page are owned by the integrated client framework.

#include <engine/graphics.h>

#include <game/client/components/menus.h>
#include <game/client/ui.h>
#include <game/localization.h>

static int s_CurRanbiTab = CMenus::RANBI_TAB_GENERAL;

void CMenus::SetRanbiSettingsTab(int Tab)
{
	if(Tab < RANBI_TAB_GENERAL || Tab >= RANBI_TAB_COUNT)
		Tab = RANBI_TAB_GENERAL;
	s_CurRanbiTab = Tab;
}

int CMenus::GetRanbiSettingsTab() const
{
	return s_CurRanbiTab;
}

static void RenderRanbiPlaceholder(CUi *pUi, CUIRect MainView)
{
	const float HeadlineFontSize = 20.0f;
	const float LineSize = 20.0f;

	CUIRect Label;
	MainView.HSplitTop(HeadlineFontSize, &Label, &MainView);
	pUi->DoLabel(&Label, Localize("Ranbi"), HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.HSplitTop(LineSize, &Label, &MainView);
	pUi->DoLabel(&Label, Localize("Ranbi features are not integrated yet. This page is a placeholder."), 14.0f, TEXTALIGN_ML);
}

void CMenus::RenderSettingsRanbi(CUIRect MainView)
{
	static CButtonContainer s_aPageTabs[RANBI_TAB_COUNT] = {};
	const char *apTabNames[RANBI_TAB_COUNT] = {
		Localize("General"),
		Localize("Nameplates"),
		Localize("Chat & Scoreboard"),
		Localize("Notify"),
		Localize("Weapons"),
	};

	CUIRect TabBar, TabButton;
	MainView.HSplitTop(24.0f, &TabBar, &MainView);
	const float TabWidth = TabBar.w / (float)RANBI_TAB_COUNT;
	for(int Tab = 0; Tab < RANBI_TAB_COUNT; ++Tab)
	{
		TabBar.VSplitLeft(TabWidth, &TabButton, &TabBar);
		const int Corners = Tab == RANBI_TAB_GENERAL ? IGraphics::CORNER_L : (Tab == RANBI_TAB_COUNT - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurRanbiTab == Tab, &TabButton, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			s_CurRanbiTab = Tab;
	}
	MainView.HSplitTop(10.0f, nullptr, &MainView);

	if(s_CurRanbiTab == RANBI_TAB_GENERAL)
		RenderSettingsRanbiGeneral(MainView);
	else if(s_CurRanbiTab == RANBI_TAB_NAMEPLATES)
		RenderSettingsRanbiNameplates(MainView);
	else if(s_CurRanbiTab == RANBI_TAB_CHAT_SCOREBOARD)
		RenderSettingsRanbiChatScoreboard(MainView);
	else if(s_CurRanbiTab == RANBI_TAB_NOTIFY)
		RenderSettingsRanbiNotify(MainView);
	else if(s_CurRanbiTab == RANBI_TAB_WEAPONS)
		RenderSettingsRanbiWeapons(MainView);
}

void CMenus::RenderSettingsRanbiGeneral(CUIRect MainView)
{
	// TODO(ranbi): cursor copy, deferred rendering, spectator skin, hammer fix and
	// the user-confirmed default-off options (auto change skin, show all players).
	RenderRanbiPlaceholder(Ui(), MainView);
}

void CMenus::RenderSettingsRanbiNameplates(CUIRect MainView)
{
	// TODO(ranbi): nameplate points/finished/dummy-hammer-reset status/range hidden.
	RenderRanbiPlaceholder(Ui(), MainView);
}

void CMenus::RenderSettingsRanbiChatScoreboard(CUIRect MainView)
{
	// TODO(ranbi): chat history dedup, chat points, user-confirmed default-off name
	// change option.
	RenderRanbiPlaceholder(Ui(), MainView);
}

void CMenus::RenderSettingsRanbiNotify(CUIRect MainView)
{
	// TODO(ranbi): skip three tiles notify (or EdgeHelper extension).
	RenderRanbiPlaceholder(Ui(), MainView);
}

void CMenus::RenderSettingsRanbiWeapons(CUIRect MainView)
{
	// TODO(ranbi): weapon angle list editor and switch recent weapon bind.
	RenderRanbiPlaceholder(Ui(), MainView);
}
// integratedclient
