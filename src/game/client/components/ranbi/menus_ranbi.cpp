// integratedclient
// Ranbi settings page framework (task-3).
//
// The Ranbi feature pages are implemented by the later Ranbi integration tasks.
// Only fill in the bodies of the RenderSettingsRanbi* functions below; do not add
// a separate top-level page or "R" menu entry. The tab list, settings navigation
// entries and the Info page are owned by the integrated client framework.

#include <base/color.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/bestclient/ui_theme/style.h>
#include <game/client/components/key_binder.h>
#include <game/client/components/menus.h>
#include <game/client/components/ranbi/weapons.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

static int s_CurRanbiTab = CMenus::RANBI_TAB_GENERAL;

// task-49: Bind Manager config delete confirmation state (popup callback has no arguments).
static std::string s_BindManagerPendingDeleteConfig;
static bool s_BindManagerForceConfigNameSync = false;
static char s_BindManagerPostActionStatus[512] = "";
static bool s_BindManagerPostActionStatusError = false;

static constexpr float s_FontSize = 14.0f;
static constexpr float s_EditBoxFontSize = 12.0f;
static constexpr float s_LineSize = 20.0f;
static constexpr float s_ColorPickerLineSize = 25.0f;
static constexpr float s_ColorPickerLabelSize = 13.0f;
static constexpr float s_ColorPickerLineSpacing = 5.0f;
static constexpr float s_HeadlineFontSize = 20.0f;
static constexpr float s_HeadlineHeight = s_HeadlineFontSize;
static constexpr float s_Margin = 10.0f;
static constexpr float s_MarginSmall = 5.0f;
static constexpr float s_MarginExtraSmall = 2.5f;
static constexpr float s_MarginBetweenSections = 30.0f;
static constexpr float s_MarginBetweenViews = 30.0f;

static void RenderRanbiSectionHeadline(CUi *pUi, CUIRect *pColumn, const char *pText)
{
	CUIRect Label;
	pColumn->HSplitTop(s_HeadlineHeight, &Label, pColumn);
	pUi->DoLabel(&Label, pText, s_HeadlineFontSize, TEXTALIGN_ML);
	pColumn->HSplitTop(s_MarginSmall, nullptr, pColumn);
}

static void RenderRanbiHint(CUi *pUi, CUIRect *pColumn, const char *pText)
{
	CUIRect Label;
	pColumn->HSplitTop(s_LineSize, &Label, pColumn);
	Label.VSplitLeft(s_Margin, nullptr, &Label);
	pUi->DoLabel(&Label, pText, s_FontSize * 0.85f, TEXTALIGN_ML);
}

static void RenderRanbiWarning(CUi *pUi, CUIRect *pColumn, const char *pText)
{
	CUIRect Label;
	pColumn->HSplitTop(s_LineSize, &Label, pColumn);
	Label.VSplitLeft(s_Margin, nullptr, &Label);
	SLabelProperties Props;
	Props.SetColor(ColorRGBA(1.0f, 0.45f, 0.35f, 1.0f));
	pUi->DoLabel(&Label, pText, s_FontSize * 0.85f, TEXTALIGN_ML, Props);
}

static void BeginRanbiPageScroll(CScrollRegion &ScrollRegion, CUIRect &MainView)
{
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	ScrollRegion.Begin(&MainView, &ScrollParams);
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);
}

static void EndRanbiPageScroll(CScrollRegion &ScrollRegion, const CUIRect &MainView, const CUIRect &Column)
{
	CUIRect ScrollRegionRect;
	ScrollRegionRect.x = MainView.x;
	ScrollRegionRect.y = Column.y + s_MarginSmall;
	ScrollRegionRect.w = MainView.w;
	ScrollRegionRect.h = 0.0f;
	ScrollRegion.AddRect(ScrollRegionRect);
	ScrollRegion.End();
}

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

void CMenus::RenderSettingsRanbi(CUIRect MainView)
{
	static CButtonContainer s_aPageTabs[RANBI_TAB_COUNT] = {};
	const char *apTabNames[RANBI_TAB_COUNT] = {
		Localize("General"),
		Localize("Nameplates"),
		Localize("Chat & Scoreboard"),
		Localize("Notify"),
		Localize("Weapons"),
		Localize("Bind Manager"),
		Localize("AI"),
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
	else if(s_CurRanbiTab == RANBI_TAB_BIND_MANAGER)
		RenderSettingsRanbiBindManager(MainView);
	else if(s_CurRanbiTab == RANBI_TAB_AI)
		RenderSettingsRanbiAi(MainView);
}

void CMenus::RenderSettingsRanbiGeneral(CUIRect MainView)
{
	static CScrollRegion s_ScrollRegion;
	BeginRanbiPageScroll(s_ScrollRegion, MainView);

	CUIRect Column = MainView;
	Column.HSplitTop(s_Margin, nullptr, &Column);

	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Fixes"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcHammerFix, Localize("Fix unintended hammer on dummy switch"), &g_Config.m_RcHammerFix, &Column, s_LineSize);
	RenderRanbiHint(Ui(), &Column, Localize("Only applies while dummy copy moves is enabled."));

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Dummy"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcCursorCopy, Localize("Keep crosshair when switching dummy"), &g_Config.m_RcCursorCopy, &Column, s_LineSize);

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Render"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcEmotionDeferredRender, Localize("Deferred emoticon rendering"), &g_Config.m_RcEmotionDeferredRender, &Column, s_LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcHookCollDeferredRender, Localize("Deferred hook collision line"), &g_Config.m_RcHookCollDeferredRender, &Column, s_LineSize);
	RenderRanbiHint(Ui(), &Column, Localize("Renders above water and transparent tiles."));
	Column.HSplitTop(s_MarginSmall, nullptr, &Column);
	{
		CUIRect Slider;
		Column.HSplitTop(s_LineSize, &Slider, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RcFrozenTeeGray, &g_Config.m_RcFrozenTeeGray, &Slider, Localize("Frozen tee gray filter"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	}
	RenderRanbiHint(Ui(), &Column, Localize("0 = original colors, 100 = fully gray frozen tee icons."));

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Official server list"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcLockFinishedName, Localize("Lock map finish name"), &g_Config.m_RcLockFinishedName, &Column, s_LineSize);
	RenderRanbiHint(Ui(), &Column, Localize("Queries the official server list map finishes with a fixed player name, independent of your current in-game name."));
	if(g_Config.m_RcLockFinishedName)
	{
		CUIRect EditBox, Label;
		Column.HSplitTop(s_LineSize + s_MarginExtraSmall, &EditBox, &Column);
		EditBox.VSplitMid(&Label, &EditBox);
		Ui()->DoLabel(&Label, Localize("Player name"), s_FontSize, TEXTALIGN_ML);
		static CLineInput s_LockFinishedNameInput(g_Config.m_RcLockFinishedNameId, sizeof(g_Config.m_RcLockFinishedNameId));
		s_LockFinishedNameInput.SetEmptyText(Client()->PlayerName());
		Ui()->DoEditBox(&s_LockFinishedNameInput, &EditBox, s_EditBoxFontSize);

		if(g_Config.m_RcLockFinishedNameId[0] == '\0')
			RenderRanbiWarning(Ui(), &Column, Localize("No locked player name set, falling back to your current player name."));
		if(!g_Config.m_BrIndicateFinished)
			RenderRanbiWarning(Ui(), &Column, Localize("Requires 'Indicate map finish' in the server browser filters; the lock has no effect while it is disabled."));
		if(g_Config.m_RcLockFinishedNameId[0] != '\0' && g_Config.m_BrIndicateFinished)
			RenderRanbiHint(Ui(), &Column, Localize("Map finishes refresh automatically about one second after changing this setting."));
	}

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Fast practice"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcFastPracticeGhostCoords, Localize("Use practice ghost coordinates"), &g_Config.m_RcFastPracticeGhostCoords, &Column, s_LineSize);
	RenderRanbiHint(Ui(), &Column, Localize("In BC fast practice, show the controlled practice character's position instead of the frozen body."));

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Risky features"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcAutoChangeSkin, Localize("Auto change skin on collision"), &g_Config.m_RcAutoChangeSkin, &Column, s_LineSize);
	RenderRanbiHint(Ui(), &Column, Localize("Copies the closest player's skin within collision range (0.6 skin or 0.7 skin parts)."));
	RenderRanbiWarning(Ui(), &Column, Localize("Warning: frequent skin changes may be rate-limited by the server and can look like impersonation."));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowAllPlayers, Localize("Receive all players outside the view"), &g_Config.m_RcShowAllPlayers, &Column, s_LineSize);
	RenderRanbiHint(Ui(), &Column, Localize("Requests every player from the server instead of only those in view."));
	RenderRanbiWarning(Ui(), &Column, Localize("Warning: increases bandwidth, may reduce frame rate and can violate server rules."));

	EndRanbiPageScroll(s_ScrollRegion, MainView, Column);
}

void CMenus::RenderSettingsRanbiNameplates(CUIRect MainView)
{
	static CScrollRegion s_ScrollRegion;
	BeginRanbiPageScroll(s_ScrollRegion, MainView);

	CUIRect Column = MainView;
	Column.HSplitTop(s_Margin, nullptr, &Column);

	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Points"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNameplatesShowPoints, Localize("Show DDNet points"), &g_Config.m_RcNameplatesShowPoints, &Column, s_LineSize);
	RenderRanbiHint(Ui(), &Column, Localize("Requires a DDNet, EGO or Legit server and the name to be shown."));

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Completed"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNameplatesShowFinished, Localize("Show finished map marker"), &g_Config.m_RcNameplatesShowFinished, &Column, s_LineSize);
	if(g_Config.m_RcNameplatesShowFinished)
	{
		CUIRect Slider;
		Column.HSplitTop(s_LineSize, &Slider, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RcNameplatesShowFinishedSize, &g_Config.m_RcNameplatesShowFinishedSize, &Slider, Localize("Marker size"), 0, 50);
	}

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Dummy status"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNameplatesShowDummyCopyStatus, Localize("Show dummy copy status"), &g_Config.m_RcNameplatesShowDummyCopyStatus, &Column, s_LineSize);
	if(g_Config.m_RcNameplatesShowDummyCopyStatus)
	{
		CUIRect Slider;
		Column.HSplitTop(s_LineSize, &Slider, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RcNameplatesShowDummyCopyStatusSize, &g_Config.m_RcNameplatesShowDummyCopyStatusSize, &Slider, Localize("Dummy copy status size"), 0, 50);
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNameplatesShowHammerFlyStatus, Localize("Show hammer fly status"), &g_Config.m_RcNameplatesShowHammerFlyStatus, &Column, s_LineSize);
	if(g_Config.m_RcNameplatesShowHammerFlyStatus)
	{
		CUIRect Slider;
		Column.HSplitTop(s_LineSize, &Slider, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RcNameplatesShowHammerFlyStatusSize, &g_Config.m_RcNameplatesShowHammerFlyStatusSize, &Slider, Localize("Hammer fly status size"), 0, 50);
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNameplatesShowDummyResetStatus, Localize("Show dummy reset status"), &g_Config.m_RcNameplatesShowDummyResetStatus, &Column, s_LineSize);
	if(g_Config.m_RcNameplatesShowDummyResetStatus)
	{
		CUIRect Slider;
		Column.HSplitTop(s_LineSize, &Slider, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RcNameplatesShowDummyResetStatusSize, &g_Config.m_RcNameplatesShowDummyResetStatusSize, &Slider, Localize("Dummy reset status size"), 0, 50);
	}
	RenderRanbiHint(Ui(), &Column, Localize("Dummy status icons are only shown on your own nameplates."));

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Range hidden"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNameplatesRangeHidden, Localize("Hide nameplate parts by range"), &g_Config.m_RcNameplatesRangeHidden, &Column, s_LineSize);
	if(g_Config.m_RcNameplatesRangeHidden)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNameplatesRangeHiddenName, Localize("Hide name"), &g_Config.m_RcNameplatesRangeHiddenName, &Column, s_LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNameplatesRangeHiddenPoints, Localize("Hide points"), &g_Config.m_RcNameplatesRangeHiddenPoints, &Column, s_LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNameplatesRangeHiddenPositionX, Localize("Hide tee coordinates"), &g_Config.m_RcNameplatesRangeHiddenPositionX, &Column, s_LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNameplatesRangeHiddenFinished, Localize("Hide finished marker"), &g_Config.m_RcNameplatesRangeHiddenFinished, &Column, s_LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNameplatesRangeHiddenStatus, Localize("Hide dummy status icons"), &g_Config.m_RcNameplatesRangeHiddenStatus, &Column, s_LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNameplatesRangeHiddenHookStatus, Localize("Hide hook strong/weak"), &g_Config.m_RcNameplatesRangeHiddenHookStatus, &Column, s_LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNameplatesRangeHiddenLove, Localize("Hide friend mark"), &g_Config.m_RcNameplatesRangeHiddenLove, &Column, s_LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNameplatesRangeHiddenOthers, Localize("Hide other parts"), &g_Config.m_RcNameplatesRangeHiddenOthers, &Column, s_LineSize);

		CUIRect Slider;
		Column.HSplitTop(s_LineSize, &Slider, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RcNameplatesRangeHiddenRadius, &g_Config.m_RcNameplatesRangeHiddenRadius, &Slider, Localize("Radius (tiles)"), 0, 20);
		Column.HSplitTop(s_LineSize, &Slider, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RcNameplatesRangeHiddenAngleStart, &g_Config.m_RcNameplatesRangeHiddenAngleStart, &Slider, Localize("Angle start (degrees)"), 0, 360);
		Column.HSplitTop(s_LineSize, &Slider, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RcNameplatesRangeHiddenAngleEnd, &g_Config.m_RcNameplatesRangeHiddenAngleEnd, &Slider, Localize("Angle end (degrees)"), 0, 360);
		RenderRanbiHint(Ui(), &Column, Localize("Applies to other players around the player you are viewing; your own nameplate is never hidden."));
	}

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Matched position"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcNameplatesShowPositionXMatched, Localize("Highlight players with matching position X"), &g_Config.m_RcNameplatesShowPositionXMatched, &Column, s_LineSize);
	if(g_Config.m_RcNameplatesShowPositionXMatched)
	{
		static CButtonContainer s_MatchedColorPicker;
		DoLine_ColorPicker(&s_MatchedColorPicker, s_ColorPickerLineSize, s_ColorPickerLabelSize, s_ColorPickerLineSpacing, &Column, Localize("Matched color"), &g_Config.m_RcNameplatesShowPositionXMatchedColor, ColorRGBA(1.0f, 0.0f, 0.0f), false);
	}
	RenderRanbiHint(Ui(), &Column, Localize("Colors the tee coordinate part and requires BestClient 'Show tee coordinates' (bc_show_tee_coords)."));

	EndRanbiPageScroll(s_ScrollRegion, MainView, Column);
}

void CMenus::RenderSettingsRanbiChatScoreboard(CUIRect MainView)
{
	static CScrollRegion s_ScrollRegion;
	BeginRanbiPageScroll(s_ScrollRegion, MainView);

	CUIRect Column = MainView;
	Column.HSplitTop(s_Margin, nullptr, &Column);

	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Chat"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcChatNoDuplicateHistory, Localize("Skip duplicate messages in chat history"), &g_Config.m_RcChatNoDuplicateHistory, &Column, s_LineSize);
	RenderRanbiHint(Ui(), &Column, Localize("Pressing Up or Down in the chat input skips repeated consecutive messages."));

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Repeat"));
	static CButtonContainer s_RepeatKeyReader, s_RepeatKeyClear;
	DoLine_KeyReader(Column, s_RepeatKeyReader, s_RepeatKeyClear, Localize("Repeat last player message"), "ranbi_repeat_last");
	RenderRanbiHint(Ui(), &Column, Localize("Repeats the most recent message from another player in the same chat channel; does nothing when nobody spoke yet."));

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Points"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcChatShowPoints, Localize("Show DDNet points in chat"), &g_Config.m_RcChatShowPoints, &Column, s_LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcReplaceBcPoints, Localize("Replace BC point fetching"), &g_Config.m_RcReplaceBcPoints, &Column, s_LineSize);
	RenderRanbiHint(Ui(), &Column, Localize("Uses Ranbi/DDNet (ddnet.org) points on all servers instead of the BC provider logic."));
	RenderRanbiHint(Ui(), &Column, Localize("Requires a DDNet, EGO or Legit server."));

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Spectator"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowSpectatorSkin, Localize("Show spectator skin in Following line"), &g_Config.m_RcShowSpectatorSkin, &Column, s_LineSize);
	RenderRanbiHint(Ui(), &Column, Localize("Appends the skin name; BestClient spectator names stay unchanged."));

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Risky features"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcScoreboardCopyName, Localize("Take player name from scoreboard"), &g_Config.m_RcScoreboardCopyName, &Column, s_LineSize);
	RenderRanbiHint(Ui(), &Column, Localize("Adds a 'Take Name' action to the scoreboard player menu and requires a confirmation each time."));
	RenderRanbiWarning(Ui(), &Column, Localize("Warning: renaming to another player can be seen as impersonation and may violate server rules."));

	EndRanbiPageScroll(s_ScrollRegion, MainView, Column);
}

void CMenus::RenderSettingsRanbiNotify(CUIRect MainView)
{
	static CScrollRegion s_ScrollRegion;
	BeginRanbiPageScroll(s_ScrollRegion, MainView);

	CUIRect Column = MainView;
	Column.HSplitTop(s_Margin, nullptr, &Column);

	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Skip three tiles"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowEnableSkipThreeTiles, Localize("Show three-tile water skip hint"), &g_Config.m_RcShowEnableSkipThreeTiles, &Column, s_LineSize);
	RenderRanbiHint(Ui(), &Column, Localize("Shows an arrow when the current position can be skipped with the three-tile water technique."));

	if(g_Config.m_RcShowEnableSkipThreeTiles)
	{
		CUIRect Slider;
		Column.HSplitTop(s_LineSize, &Slider, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RcShowEnableSkipThreeTilesInfoSize, &g_Config.m_RcShowEnableSkipThreeTilesInfoSize, &Slider, Localize("Hint size"), 0, 50);

		static CButtonContainer s_HintColorPicker;
		DoLine_ColorPicker(&s_HintColorPicker, s_ColorPickerLineSize, s_ColorPickerLabelSize, s_ColorPickerLineSpacing, &Column, Localize("Hint color"), &g_Config.m_RcShowEnableSkipThreeTilesInfoColor, ColorRGBA(0.5f, 0.8f, 0.5f), false);

		Column.HSplitTop(s_LineSize, &Slider, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RcShowEnableSkipThreeTilesInfoPositionX, &g_Config.m_RcShowEnableSkipThreeTilesInfoPositionX, &Slider, Localize("Hint position X"), 0, 100);
		Column.HSplitTop(s_LineSize, &Slider, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RcShowEnableSkipThreeTilesInfoPositionY, &g_Config.m_RcShowEnableSkipThreeTilesInfoPositionY, &Slider, Localize("Hint position Y"), 0, 100);

		static CLineInput s_LeftTextInput(g_Config.m_RcShowEnableSkipThreeTilesInfoPositionLeftText, sizeof(g_Config.m_RcShowEnableSkipThreeTilesInfoPositionLeftText));
		static CLineInput s_RightTextInput(g_Config.m_RcShowEnableSkipThreeTilesInfoPositionRightText, sizeof(g_Config.m_RcShowEnableSkipThreeTilesInfoPositionRightText));
		static CLineInput s_DjLeftTextInput(g_Config.m_RcShowEnableSkipThreeTilesInfoPositionDJLeftText, sizeof(g_Config.m_RcShowEnableSkipThreeTilesInfoPositionDJLeftText));
		static CLineInput s_DjRightTextInput(g_Config.m_RcShowEnableSkipThreeTilesInfoPositionDJRightText, sizeof(g_Config.m_RcShowEnableSkipThreeTilesInfoPositionDJRightText));

		CUIRect EditBox, Label;
		Column.HSplitTop(s_LineSize + s_MarginExtraSmall, &EditBox, &Column);
		EditBox.VSplitMid(&Label, &EditBox);
		Ui()->DoLabel(&Label, Localize("Left arrow text"), s_FontSize, TEXTALIGN_ML);
		Ui()->DoEditBox(&s_LeftTextInput, &EditBox, s_EditBoxFontSize);

		Column.HSplitTop(s_LineSize + s_MarginExtraSmall, &EditBox, &Column);
		EditBox.VSplitMid(&Label, &EditBox);
		Ui()->DoLabel(&Label, Localize("Right arrow text"), s_FontSize, TEXTALIGN_ML);
		Ui()->DoEditBox(&s_RightTextInput, &EditBox, s_EditBoxFontSize);

		Column.HSplitTop(s_LineSize + s_MarginExtraSmall, &EditBox, &Column);
		EditBox.VSplitMid(&Label, &EditBox);
		Ui()->DoLabel(&Label, Localize("DJ left arrow text"), s_FontSize, TEXTALIGN_ML);
		Ui()->DoEditBox(&s_DjLeftTextInput, &EditBox, s_EditBoxFontSize);

		Column.HSplitTop(s_LineSize + s_MarginExtraSmall, &EditBox, &Column);
		EditBox.VSplitMid(&Label, &EditBox);
		Ui()->DoLabel(&Label, Localize("DJ right arrow text"), s_FontSize, TEXTALIGN_ML);
		Ui()->DoEditBox(&s_DjRightTextInput, &EditBox, s_EditBoxFontSize);
	}

	EndRanbiPageScroll(s_ScrollRegion, MainView, Column);
}

static int s_RanbiSelectedWeapon = -1;
static int s_RanbiLastSelectedWeapon = -1;
static SWeaponInfo s_RanbiEditWeapon;
static bool s_RanbiAngleInit = false;
static bool s_RanbiWeaponEditorInit = false;

static const char *GetRanbiWeaponName(int WeaponID)
{
	switch(WeaponID)
	{
	case WEAPON_HAMMER: return Localize("Hammer");
	case WEAPON_GUN: return Localize("Gun");
	case WEAPON_SHOTGUN: return Localize("Shotgun");
	case WEAPON_GRENADE: return Localize("Grenade");
	case WEAPON_LASER: return Localize("Laser");
	case WEAPON_NINJA: return Localize("Ninja");
	default: return "";
	}
}

static void ResetRanbiWeaponEditor()
{
	s_RanbiEditWeapon = SWeaponInfo{};
	s_RanbiEditWeapon.m_WeaponID = WEAPON_HAMMER;
	s_RanbiEditWeapon.m_AngleStart = 0.0f;
	s_RanbiEditWeapon.m_AngleEnd = 90.0f;
	s_RanbiEditWeapon.m_Color = color_cast<ColorHSLA>(ColorRGBA(1.0f, 1.0f, 1.0f)).Pack();
	s_RanbiAngleInit = false;
}

void CMenus::RenderSettingsRanbiWeapons(CUIRect MainView)
{
	auto &vWeapons = GameClient()->m_Weapons.m_vWeapons;

	if(!s_RanbiWeaponEditorInit)
	{
		ResetRanbiWeaponEditor();
		s_RanbiWeaponEditorInit = true;
	}

	CUIRect LeftView, RightView, Button, Label;
	MainView.VSplitMid(&LeftView, &RightView, s_MarginBetweenViews);
	LeftView.VSplitLeft(s_MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(s_MarginSmall, &RightView, nullptr);

	CUIRect LeftViewRect = LeftView;
	static CScrollRegion s_LeftScrollRegion;
	BeginRanbiPageScroll(s_LeftScrollRegion, LeftView);

	CUIRect Column = LeftView;
	Column.HSplitTop(s_Margin, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Weapon angle display"));

	static CButtonContainer s_ReaderSwitchRecentWeapon, s_ClearSwitchRecentWeapon;
	DoLine_KeyReader(Column, s_ReaderSwitchRecentWeapon, s_ClearSwitchRecentWeapon, Localize("Switch recent weapon key"), "+switch_recent_weapon");
	Column.HSplitTop(s_MarginExtraSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcShowWeaponsAngle, Localize("Show weapon angle markers"), &g_Config.m_RcShowWeaponsAngle, &Column, s_LineSize);
	if(g_Config.m_RcShowWeaponsAngle)
	{
		CUIRect Slider;
		Column.HSplitTop(s_LineSize, &Slider, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RcWeaponsAngleInnerRadius, &g_Config.m_RcWeaponsAngleInnerRadius, &Slider, Localize("Inner radius"), 0, 800);
		Column.HSplitTop(s_LineSize, &Slider, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RcWeaponsAngleRadius, &g_Config.m_RcWeaponsAngleRadius, &Slider, Localize("Outer radius"), 0, 800);
		Column.HSplitTop(s_LineSize, &Slider, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_RcWeaponsAngleFontSize, &g_Config.m_RcWeaponsAngleFontSize, &Slider, Localize("Note font size"), 6, 24);
	}

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Weapon aim lines"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcWeaponAimLines, Localize("Show weapon aim lines"), &g_Config.m_RcWeaponAimLines, &Column, s_LineSize);
	RenderRanbiHint(Ui(), &Column, Localize("Draws a predicted laser line, shotgun spread lines or grenade trajectory for your own tee."));

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);

	if(s_RanbiSelectedWeapon != s_RanbiLastSelectedWeapon)
	{
		s_RanbiLastSelectedWeapon = s_RanbiSelectedWeapon;
		if(s_RanbiSelectedWeapon >= 0 && s_RanbiSelectedWeapon < (int)vWeapons.size())
			s_RanbiEditWeapon = vWeapons[s_RanbiSelectedWeapon];
		s_RanbiAngleInit = false;
	}

	Column.HSplitTop(s_MarginSmall, nullptr, &Column);

	static CUi::SDropDownState s_DropDownState;
	static CScrollRegion s_DropDownScrollRegion;
	s_DropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_DropDownScrollRegion;
	CUIRect DropDownRect;
	Column.HSplitTop(s_LineSize + s_MarginExtraSmall, &DropDownRect, &Column);
	DropDownRect.VSplitMid(&Label, &DropDownRect);
	Ui()->DoLabel(&Label, Localize("Weapon"), s_FontSize, TEXTALIGN_ML);
	const char *apWeaponNames[] = {
		Localize("Hammer"), Localize("Gun"), Localize("Shotgun"),
		Localize("Grenade"), Localize("Laser"), Localize("Ninja")};
	const int WeaponSelectedNew = Ui()->DoDropDown(&DropDownRect, s_RanbiEditWeapon.m_WeaponID, apWeaponNames, NUM_WEAPONS, s_DropDownState);
	if(WeaponSelectedNew != s_RanbiEditWeapon.m_WeaponID)
		s_RanbiEditWeapon.m_WeaponID = WeaponSelectedNew;

	Column.HSplitTop(s_MarginSmall, nullptr, &Column);

	static char s_aAngleMin[32];
	static char s_aAngleMax[32];
	if(!s_RanbiAngleInit)
	{
		str_format(s_aAngleMin, sizeof(s_aAngleMin), "%.2f", s_RanbiEditWeapon.m_AngleStart);
		str_format(s_aAngleMax, sizeof(s_aAngleMax), "%.2f", s_RanbiEditWeapon.m_AngleEnd);
		s_RanbiAngleInit = true;
	}

	static CLineInput s_AngleMinInput(s_aAngleMin, sizeof(s_aAngleMin));
	static CLineInput s_AngleMaxInput(s_aAngleMax, sizeof(s_aAngleMax));

	Column.HSplitTop(s_LineSize + s_MarginExtraSmall, &Button, &Column);
	Button.VSplitMid(&Label, &Button);
	Ui()->DoLabel(&Label, Localize("Angle start"), s_FontSize, TEXTALIGN_ML);
	if(Ui()->DoEditBox(&s_AngleMinInput, &Button, s_EditBoxFontSize))
		s_RanbiEditWeapon.m_AngleStart = str_tofloat(s_aAngleMin);

	Column.HSplitTop(s_MarginExtraSmall, nullptr, &Column);

	Column.HSplitTop(s_LineSize + s_MarginExtraSmall, &Button, &Column);
	Button.VSplitMid(&Label, &Button);
	Ui()->DoLabel(&Label, Localize("Angle end"), s_FontSize, TEXTALIGN_ML);
	if(Ui()->DoEditBox(&s_AngleMaxInput, &Button, s_EditBoxFontSize))
		s_RanbiEditWeapon.m_AngleEnd = str_tofloat(s_aAngleMax);

	Column.HSplitTop(s_MarginExtraSmall, nullptr, &Column);

	static CButtonContainer s_WeaponColor;
	DoLine_ColorPicker(&s_WeaponColor, s_ColorPickerLineSize, s_ColorPickerLabelSize, s_ColorPickerLineSpacing, &Column, Localize("Color"), &s_RanbiEditWeapon.m_Color, ColorRGBA(1.0f, 1.0f, 1.0f), false);

	Column.HSplitTop(s_MarginExtraSmall, nullptr, &Column);

	static CLineInput s_NoteInput(s_RanbiEditWeapon.m_aNote, sizeof(s_RanbiEditWeapon.m_aNote));
	Column.HSplitTop(s_LineSize + s_MarginExtraSmall, &Button, &Column);
	Button.VSplitMid(&Label, &Button);
	Ui()->DoLabel(&Label, Localize("Note"), s_FontSize, TEXTALIGN_ML);
	Ui()->DoEditBox(&s_NoteInput, &Button, s_EditBoxFontSize);

	Column.HSplitTop(s_Margin, nullptr, &Column);

	static CButtonContainer s_AddButton, s_ModifyButton, s_DeleteButton;
	Column.HSplitTop(s_LineSize, &Button, &Column);
	CUIRect BtnAdd, BtnModify, BtnDelete;
	const float BtnW = (Button.w - s_MarginSmall * 2.0f) / 3.0f;
	Button.VSplitLeft(BtnW, &BtnAdd, &Button);
	Button.VSplitLeft(s_MarginSmall, nullptr, &Button);
	Button.VSplitLeft(BtnW, &BtnModify, &Button);
	Button.VSplitLeft(s_MarginSmall, nullptr, &Button);
	BtnDelete = Button;

	if(DoButton_Menu(&s_AddButton, Localize("Add"), 0, &BtnAdd))
	{
		vWeapons.push_back(s_RanbiEditWeapon);
		s_RanbiSelectedWeapon = (int)vWeapons.size() - 1;
		ResetRanbiWeaponEditor();
	}
	if(DoButton_Menu(&s_ModifyButton, Localize("Modify"), 0, &BtnModify) && s_RanbiSelectedWeapon >= 0 && s_RanbiSelectedWeapon < (int)vWeapons.size())
	{
		vWeapons[s_RanbiSelectedWeapon] = s_RanbiEditWeapon;
		s_RanbiLastSelectedWeapon = -1;
	}
	if(DoButton_Menu(&s_DeleteButton, Localize("Delete"), 0, &BtnDelete) && s_RanbiSelectedWeapon >= 0 && s_RanbiSelectedWeapon < (int)vWeapons.size())
	{
		vWeapons.erase(vWeapons.begin() + s_RanbiSelectedWeapon);
		s_RanbiSelectedWeapon = -1;
		s_RanbiLastSelectedWeapon = -1;
		ResetRanbiWeaponEditor();
	}

	EndRanbiPageScroll(s_LeftScrollRegion, LeftViewRect, Column);

	// task-32: list header and list are anchored at the top-right view; the left
	// editor column scrolls independently and can no longer overlap the list.
	CUIRect RightColumn = RightView;
	RightColumn.HSplitTop(s_Margin, nullptr, &RightColumn);
	RenderRanbiSectionHeadline(Ui(), &RightColumn, Localize("Weapon angle list"));
	RenderRanbiHint(Ui(), &RightColumn, Localize("Entries are saved to ranbi_weapons_angle.cfg when the client exits."));

	CUIRect ListView = RightColumn;

	static CScrollRegion s_ListScrollRegion;
	CScrollRegionParams ListScrollParams;
	ListScrollParams.m_ScrollUnit = 60.0f;
	ListScrollParams.m_ScrollbarMargin = 5.0f;
	s_ListScrollRegion.Begin(&ListView, &ListScrollParams);

	if(vWeapons.empty())
	{
		Ui()->DoLabel(&ListView, Localize("No weapon angle entries yet. Use Add on the left to create one."), s_FontSize, TEXTALIGN_ML);
		CUIRect ListScrollRect;
		ListScrollRect.x = ListView.x;
		ListScrollRect.y = ListView.y;
		ListScrollRect.w = ListView.w;
		ListScrollRect.h = 0.0f;
		s_ListScrollRegion.AddRect(ListScrollRect);
		s_ListScrollRegion.End();
		return;
	}

	CUIRect ListBox = ListView;
	const float HeaderH = s_LineSize + s_MarginExtraSmall;
	const float RowH = s_LineSize + s_MarginExtraSmall;
	const float ColWeapon = 80.0f;
	const float ColAngle = 110.0f;
	const float ColColor = 40.0f;
	const float ColSpacing = s_FontSize * 0.5f;

	CUIRect Header, Row;
	ListBox.HSplitTop(HeaderH, &Header, &ListBox);
	Header.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_T, 5.0f);
	{
		CUIRect Cell;
		Header.VSplitLeft(ColWeapon, &Cell, &Header);
		Header.VSplitLeft(ColSpacing, nullptr, &Header);
		Ui()->DoLabel(&Cell, Localize("Weapon"), s_FontSize, TEXTALIGN_ML);
		Header.VSplitLeft(ColAngle, &Cell, &Header);
		Header.VSplitLeft(ColSpacing, nullptr, &Header);
		Ui()->DoLabel(&Cell, Localize("Angle range"), s_FontSize, TEXTALIGN_ML);
		Header.VSplitLeft(ColColor, &Cell, &Header);
		Header.VSplitLeft(ColSpacing, nullptr, &Header);
		Ui()->DoLabel(&Cell, Localize("Color"), s_FontSize, TEXTALIGN_ML);
		Ui()->DoLabel(&Header, Localize("Note"), s_FontSize, TEXTALIGN_ML);
	}

	static std::vector<int> s_SortedIndices;
	s_SortedIndices.resize(vWeapons.size());
	for(int i = 0; i < (int)vWeapons.size(); i++)
		s_SortedIndices[i] = i;
	std::sort(s_SortedIndices.begin(), s_SortedIndices.end(), [&vWeapons](int a, int b) {
		if(vWeapons[a].m_WeaponID != vWeapons[b].m_WeaponID)
			return vWeapons[a].m_WeaponID < vWeapons[b].m_WeaponID;
		return vWeapons[a].m_AngleStart < vWeapons[b].m_AngleStart;
	});

	static std::vector<CButtonContainer> s_aListButtons;
	if((int)s_aListButtons.size() < (int)s_SortedIndices.size())
		s_aListButtons.resize(s_SortedIndices.size());

	for(int si = 0; si < (int)s_SortedIndices.size(); si++)
	{
		const int i = s_SortedIndices[si];
		ListBox.HSplitTop(RowH, &Row, &ListBox);

		if(Ui()->DoButtonLogic(&s_aListButtons[si], 0, &Row, BUTTONFLAG_LEFT))
			s_RanbiSelectedWeapon = i;

		if(s_RanbiSelectedWeapon == i)
			Row.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f), IGraphics::CORNER_NONE, 0.0f);
		else if(si % 2 == 0)
			Row.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f), IGraphics::CORNER_NONE, 0.0f);

		char aBuf[64];
		CUIRect Cell;
		Row.VSplitLeft(ColWeapon, &Cell, &Row);
		Row.VSplitLeft(ColSpacing, nullptr, &Row);
		Ui()->DoLabel(&Cell, GetRanbiWeaponName(vWeapons[i].m_WeaponID), s_FontSize, TEXTALIGN_ML);

		Row.VSplitLeft(ColAngle, &Cell, &Row);
		Row.VSplitLeft(ColSpacing, nullptr, &Row);
		str_format(aBuf, sizeof(aBuf), "%.2f~%.2f", vWeapons[i].m_AngleStart, vWeapons[i].m_AngleEnd);
		Ui()->DoLabel(&Cell, aBuf, s_FontSize, TEXTALIGN_ML);

		Row.VSplitLeft(ColColor, &Cell, &Row);
		Row.VSplitLeft(ColSpacing, nullptr, &Row);
		const ColorRGBA Col = color_cast<ColorRGBA>(ColorHSLA(vWeapons[i].m_Color, false));
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(Col.r, Col.g, Col.b, 1.0f);
		IGraphics::CQuadItem QuadColor(Cell.x + 5.0f, Cell.y + 5.0f, Cell.h - 10.0f, Cell.h - 10.0f);
		Graphics()->QuadsDrawTL(&QuadColor, 1);
		Graphics()->QuadsEnd();

		Ui()->DoLabel(&Row, vWeapons[i].m_aNote, s_FontSize, TEXTALIGN_ML);
	}

	if(!vWeapons.empty())
		ListBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_B, 5.0f);

	CUIRect ListScrollRect;
	ListScrollRect.x = ListView.x;
	ListScrollRect.y = ListView.y;
	ListScrollRect.w = ListView.w;
	ListScrollRect.h = ListBox.y - ListScrollRect.y;
	s_ListScrollRegion.AddRect(ListScrollRect);
	s_ListScrollRegion.End();
}

// ranbi: AI auto reply settings (batch8). The feature is default off; the API
// token is stored in plain text and chat/server data is sent to a third party.
static void RenderRanbiAiWarning(CUi *pUi, CUIRect *pColumn, const char *pText)
{
	CUIRect Label;
	pColumn->HSplitTop(s_LineSize, &Label, pColumn);
	Label.VSplitLeft(s_Margin, nullptr, &Label);
	SLabelProperties Props;
	Props.SetColor(ColorRGBA(1.0f, 0.45f, 0.35f, 1.0f));
	pUi->DoLabel(&Label, pText, s_FontSize * 0.85f, TEXTALIGN_ML, Props);
}

// ranbi (task-69): the system prompt config may still contain line breaks from the retired
// multiline input. The single-line edit box must not render them as line breaks, so the display
// text replaces every '\n' and '\r' byte with a space. The replacement keeps the byte length and
// all byte offsets unchanged (also for "\r\n", which becomes two spaces), so cursor and selection
// offsets stay valid; the actual buffer and the saved config value are not modified.
// ranbi (task-69): system prompt single-line display callback begin
static std::string s_SystemPromptDisplayText;
static const char *RanbiSystemPromptDisplayText(char *pText, size_t NumChars)
{
	(void)NumChars;
	s_SystemPromptDisplayText.assign(pText, str_length(pText));
	for(char &Character : s_SystemPromptDisplayText)
	{
		if(Character == '\n' || Character == '\r')
			Character = ' ';
	}
	return s_SystemPromptDisplayText.c_str();
}
// ranbi (task-69): system prompt single-line display callback end

void CMenus::RenderSettingsRanbiAi(CUIRect MainView)
{
	// ranbi: static inputs keep their cursor/selection state across frames.
	static CLineInput s_SystemPromptInput(g_Config.m_RcAiSystemPrompt, sizeof(g_Config.m_RcAiSystemPrompt));
	static bool s_SystemPromptDisplayInitialized = false;
	if(!s_SystemPromptDisplayInitialized)
	{
		// ranbi (task-69): display legacy multiline content as a single line without changing the
		// stored value; byte offsets are kept identical by the 1:1 replacement.
		s_SystemPromptInput.SetDisplayTextCallback(RanbiSystemPromptDisplayText);
		s_SystemPromptDisplayInitialized = true;
	}
	static CLineInput s_ReplySuffixInput(g_Config.m_RcAiReplySuffix, sizeof(g_Config.m_RcAiReplySuffix));

	static CScrollRegion s_ScrollRegion;
	BeginRanbiPageScroll(s_ScrollRegion, MainView);

	CUIRect Column = MainView;
	Column.HSplitTop(s_Margin, nullptr, &Column);

	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Warnings"));
	RenderRanbiAiWarning(Ui(), &Column, Localize("Warning: auto reply sends chat messages as you. Many servers ban bots and automated chat."));
	RenderRanbiAiWarning(Ui(), &Column, Localize("Privacy: chat messages, server info and MOTD are sent to the configured third-party API."));
	RenderRanbiAiWarning(Ui(), &Column, Localize("The API token is stored in plain text in settings_ranbi.cfg."));

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("API"));
	{
		static CLineInput s_BaseUrlInput(g_Config.m_RcAiBaseUrl, sizeof(g_Config.m_RcAiBaseUrl));
		static CLineInput s_ModelInput(g_Config.m_RcAiModel, sizeof(g_Config.m_RcAiModel));
		static CLineInput s_TokenInput(g_Config.m_RcAiToken, sizeof(g_Config.m_RcAiToken));
		static bool s_TokenInputInitialized = false;
		if(!s_TokenInputInitialized)
		{
			s_TokenInput.SetHidden(true);
			s_TokenInputInitialized = true;
		}

		CUIRect EditBox, Label;
		Column.HSplitTop(s_LineSize + s_MarginExtraSmall, &EditBox, &Column);
		EditBox.VSplitMid(&Label, &EditBox);
		Ui()->DoLabel(&Label, Localize("Base URL"), s_FontSize, TEXTALIGN_ML);
		Ui()->DoEditBox(&s_BaseUrlInput, &EditBox, s_EditBoxFontSize);

		Column.HSplitTop(s_LineSize + s_MarginExtraSmall, &EditBox, &Column);
		EditBox.VSplitMid(&Label, &EditBox);
		Ui()->DoLabel(&Label, Localize("Model"), s_FontSize, TEXTALIGN_ML);
		Ui()->DoEditBox(&s_ModelInput, &EditBox, s_EditBoxFontSize);

		Column.HSplitTop(s_LineSize + s_MarginExtraSmall, &EditBox, &Column);
		EditBox.VSplitMid(&Label, &EditBox);
		Ui()->DoLabel(&Label, Localize("API token (hidden)"), s_FontSize, TEXTALIGN_ML);
		Ui()->DoEditBox(&s_TokenInput, &EditBox, s_EditBoxFontSize);

		Column.HSplitTop(s_LineSize + s_MarginExtraSmall, &EditBox, &Column);
		EditBox.VSplitMid(&Label, &EditBox);
		Ui()->DoLabel(&Label, Localize("System prompt"), s_FontSize, TEXTALIGN_ML);
		Ui()->DoEditBox(&s_SystemPromptInput, &EditBox, s_EditBoxFontSize);
	}
	RenderRanbiHint(Ui(), &Column, Localize("OpenAI-compatible API, e.g. https://api.openai.com/v1 or a local endpoint."));

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Auto reply"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcAiAutoReply, Localize("Auto AI reply (default off)"), &g_Config.m_RcAiAutoReply, &Column, s_LineSize);
	RenderRanbiHint(Ui(), &Column, Localize("Replies only when another player mentions your name. Keep it off to keep normal chat behavior."));

	CUIRect Slider;
	Column.HSplitTop(s_LineSize, &Slider, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcAiReplyInterval, &g_Config.m_RcAiReplyInterval, &Slider, Localize("Reply interval (seconds)"), 1, 60);
	Column.HSplitTop(s_LineSize, &Slider, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcAiMaxSentenceLength, &g_Config.m_RcAiMaxSentenceLength, &Slider, Localize("Max message length (characters)"), 1, 100);
	Column.HSplitTop(s_LineSize, &Slider, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcAiTemperature, &g_Config.m_RcAiTemperature, &Slider, Localize("Temperature (x0.1)"), 0, 20);
	Column.HSplitTop(s_LineSize, &Slider, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcAiContextCount, &g_Config.m_RcAiContextCount, &Slider, Localize("Context count"), 0, 20);

	// ranbi (task-97/task-65): optional text appended to the end of each AI reply before it is
	// sent; the enabled checkbox only controls appending, the entered text is always kept.
	Column.HSplitTop(s_MarginSmall, nullptr, &Column);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcAiReplySuffixEnabled, Localize("Append reply suffix"), &g_Config.m_RcAiReplySuffixEnabled, &Column, s_LineSize);
	CUIRect ReplySuffixLabel, ReplySuffixBox;
	Column.HSplitTop(s_LineSize, &ReplySuffixLabel, &Column);
	Ui()->DoLabel(&ReplySuffixLabel, Localize("Reply suffix"), s_FontSize, TEXTALIGN_ML);
	Column.HSplitTop(s_LineSize + s_MarginExtraSmall, &ReplySuffixBox, &Column);
	Ui()->DoEditBox(&s_ReplySuffixInput, &ReplySuffixBox, s_EditBoxFontSize);
	RenderRanbiHint(Ui(), &Column, Localize("When enabled, non-empty content is appended to every AI message. When disabled, nothing is appended and the text is kept."));

	if(g_Config.m_RcAiAutoReply && (g_Config.m_RcAiBaseUrl[0] == '\0' || g_Config.m_RcAiModel[0] == '\0' || g_Config.m_RcAiToken[0] == '\0'))
		RenderRanbiAiWarning(Ui(), &Column, Localize("Auto reply is enabled but Base URL, Model or API token is empty; requests will fail."));

	const char *pLastError = GameClient()->m_AiClient.LastError();
	if(pLastError[0] != '\0')
	{
		char aError[384];
		str_format(aError, sizeof(aError), Localize("Last AI request error: %s"), pLastError);
		RenderRanbiAiWarning(Ui(), &Column, aError);
	}
	else if(GameClient()->m_AiClient.IsBusy())
	{
		RenderRanbiHint(Ui(), &Column, Localize("Waiting for the AI response..."));
	}
	else
	{
		RenderRanbiHint(Ui(), &Column, Localize("Request failures are logged to the console (ranbi_ai)."));
	}

	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Knowledge base"));
	RenderRanbiHint(Ui(), &Column, Localize("ranbi/knowledge/required.md is read for every reply and is created on first start."));
	RenderRanbiHint(Ui(), &Column, Localize("Other .md files in ranbi/knowledge are only read on demand: when a question contains the file name or matches a description listed in required.md."));

	EndRanbiPageScroll(s_ScrollRegion, MainView, Column);
}

namespace
{
	struct SBindManagerUiState
	{
		std::vector<CButtonContainer> m_vConfigButtons;
		std::vector<CButtonContainer> m_vConfigDeleteButtons;
		std::vector<CButtonContainer> m_vExpandButtons;
		std::vector<CButtonContainer> m_vDeleteButtons;
		std::vector<CButtonContainer> m_vAddCommandButtons;
		std::vector<std::vector<CButtonContainer>> m_vDeleteCommandButtons;
		std::vector<CButtonContainer> m_vKeyReaderButtons;
		std::vector<CButtonContainer> m_vKeyClearButtons;
		std::vector<CLineInput> m_vNameInputs;
		std::vector<std::array<char, 64>> m_vNameBuffers;
		std::vector<CLineInput> m_vInitInputs;
		std::vector<std::array<char, 512>> m_vInitBuffers;
		std::vector<std::vector<CLineInput>> m_vCommandInputs;
		std::vector<std::vector<std::array<char, 512>>> m_vCommandBuffers;
		int m_DragIndex = -1;
		int64_t m_DragStart = 0;
		bool m_Initialized = false;
		bool m_ConfigNameDirty = false;
		std::vector<CUIRect> m_vHeaderRects;
		char m_aStatus[512] = "";
		bool m_StatusError = false;
		CLineInput m_ImportPathInput;
		CLineInput m_ExportPathInput;
		std::array<char, 512> m_aImportPathBuffer = {};
		std::array<char, 512> m_aExportPathBuffer = {};
	};

	void EnsureBindManagerUiSize(SBindManagerUiState &State, const std::vector<CBindManager::SEntry> &Entries)
	{
		const size_t Num = Entries.size();
		State.m_vConfigButtons.resize(std::max<size_t>(Num, 1));
		State.m_vExpandButtons.resize(Num);
		State.m_vDeleteButtons.resize(Num);
		State.m_vAddCommandButtons.resize(Num);
		State.m_vKeyReaderButtons.resize(Num);
		State.m_vKeyClearButtons.resize(Num);
		State.m_vNameBuffers.resize(Num);
		State.m_vNameInputs.resize(Num);
		State.m_vInitBuffers.resize(Num);
		State.m_vInitInputs.resize(Num);
		State.m_vCommandBuffers.resize(Num);
		State.m_vCommandInputs.resize(Num);
		State.m_vDeleteCommandButtons.resize(Num);
		for(size_t i = 0; i < Num; i++)
		{
			const size_t NumCommands = std::max<size_t>(1, Entries[i].m_vCommands.size());
			State.m_vCommandBuffers[i].resize(NumCommands);
			State.m_vCommandInputs[i].resize(NumCommands);
			State.m_vDeleteCommandButtons[i].resize(NumCommands);
		}
	}

	ColorRGBA BindManagerDefaultColorRgba()
	{
		return color_cast<ColorRGBA>(ColorHSLA(CBindManager::DEFAULT_COMMAND_COLOR, false));
	}

	void SetBindManagerStatus(SBindManagerUiState &State, bool Error, const char *pMessage)
	{
		str_copy(State.m_aStatus, pMessage && pMessage[0] ? pMessage : "", sizeof(State.m_aStatus));
		State.m_StatusError = Error;
	}

	void ReportBindManagerResult(SBindManagerUiState &State, CBindManager &Manager, bool Ok, const char *pSuccess)
	{
		if(Ok)
		{
			// task-87: manager.txt/import may succeed with sanitized (invalid) keys; surface
			// that warning instead of a plain success message.
			char aWarning[512];
			if(Manager.LastError(aWarning, sizeof(aWarning)))
				SetBindManagerStatus(State, true, aWarning);
			else
				SetBindManagerStatus(State, false, pSuccess);
			return;
		}
		char aError[512];
		if(Manager.LastError(aError, sizeof(aError)))
			SetBindManagerStatus(State, true, aError);
		else
			SetBindManagerStatus(State, true, Localize("Operation failed (see console for details)"));
	}
} // namespace

void CMenus::PopupConfirmDeleteBindManagerConfig()
{
	if(s_BindManagerPendingDeleteConfig.empty())
		return;
	const std::string Name = s_BindManagerPendingDeleteConfig;
	s_BindManagerPendingDeleteConfig.clear();
	std::string Error;
	if(GameClient()->m_BindManager.DeleteConfig(Name.c_str(), Error))
	{
		str_copy(s_BindManagerPostActionStatus, Localize("Config deleted"), sizeof(s_BindManagerPostActionStatus));
		s_BindManagerPostActionStatusError = false;
	}
	else
	{
		str_copy(s_BindManagerPostActionStatus, Error.empty() ? Localize("Operation failed (see console for details)") : Error.c_str(), sizeof(s_BindManagerPostActionStatus));
		s_BindManagerPostActionStatusError = true;
	}
	s_BindManagerForceConfigNameSync = true;
}

void CMenus::RenderSettingsRanbiBindManager(CUIRect MainView)
{
	CBindManager &Manager = GameClient()->m_BindManager;
	static SBindManagerUiState s_State;
	static CScrollRegion s_ScrollRegion;
	static CButtonContainer s_aSaveButton;
	static CButtonContainer s_aLoadButton;
	static CButtonContainer s_aImportButton;
	static CButtonContainer s_aExportButton;
	static CButtonContainer s_aRenameConfigButton;
	static CButtonContainer s_aNewConfigButton;
	static CButtonContainer s_aAddEntryButton;
	static CLineInput s_ConfigNameInput;
	static std::array<char, 128> s_aConfigNameBuffer = {};

	if(s_BindManagerForceConfigNameSync)
	{
		s_State.m_ConfigNameDirty = false;
		s_BindManagerForceConfigNameSync = false;
	}
	if(s_BindManagerPostActionStatus[0] != '\0')
	{
		SetBindManagerStatus(s_State, s_BindManagerPostActionStatusError, s_BindManagerPostActionStatus);
		s_BindManagerPostActionStatus[0] = '\0';
	}

	if(!s_State.m_Initialized)
	{
		s_State.m_Initialized = true;
		s_State.m_ImportPathInput.SetBuffer(s_State.m_aImportPathBuffer.data(), s_State.m_aImportPathBuffer.size());
		s_State.m_ExportPathInput.SetBuffer(s_State.m_aExportPathBuffer.data(), s_State.m_aExportPathBuffer.size());
		Manager.RefreshConfigs();
		if(g_Config.m_RcBindManagerConfig[0] != '\0')
			Manager.SelectConfig(g_Config.m_RcBindManagerConfig, true);
	}

	BeginRanbiPageScroll(s_ScrollRegion, MainView);
	CUIRect Column = MainView;
	Column.HSplitTop(s_Margin, nullptr, &Column);

	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Bind Manager"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcBindManagerInit, Localize("Initialize binds and parameters before entering a game"), &g_Config.m_RcBindManagerInit, &Column, s_LineSize);
	RenderRanbiHint(Ui(), &Column, Localize("When disabled, no Bind Manager cfg is executed when joining a server and the current binds stay unchanged."));

	if(s_State.m_aStatus[0] != '\0')
	{
		if(s_State.m_StatusError)
			RenderRanbiWarning(Ui(), &Column, s_State.m_aStatus);
		else
			RenderRanbiHint(Ui(), &Column, s_State.m_aStatus);
	}

	// 保存 / 加载 / 导入 / 导出
	CUIRect ButtonRow;
	Column.HSplitTop(s_LineSize + s_MarginExtraSmall, &ButtonRow, &Column);
	const float ButtonWidth = ButtonRow.w / 4.0f;
	const char *apButtonNames[4] = {Localize("Save"), Localize("Load"), Localize("Import"), Localize("Export")};
	CButtonContainer *apButtons[4] = {&s_aSaveButton, &s_aLoadButton, &s_aImportButton, &s_aExportButton};
	for(int i = 0; i < 4; i++)
	{
		CUIRect Button;
		ButtonRow.VSplitLeft(ButtonWidth, &Button, &ButtonRow);
		Button.HMargin(1.0f, &Button);
		if(DoButton_Menu(apButtons[i], apButtonNames[i], 0, &Button))
		{
			if(i == 0)
			{
				bool RenameOk = true;
				if(s_State.m_ConfigNameDirty && s_aConfigNameBuffer[0] != '\0')
				{
					RenameOk = Manager.RenameConfig(s_aConfigNameBuffer.data());
					if(RenameOk)
					{
						s_State.m_ConfigNameDirty = false;
						Ui()->SetActiveItem(nullptr);
					}
					else
					{
						char aError[512];
						if(Manager.LastError(aError, sizeof(aError)))
							SetBindManagerStatus(s_State, true, aError);
						else
							SetBindManagerStatus(s_State, true, Localize("Operation failed (see console for details)"));
					}
				}
				if(RenameOk)
					ReportBindManagerResult(s_State, Manager, Manager.SaveCurrent(), Localize("Configuration saved"));
			}
			else if(i == 1)
			{
				ReportBindManagerResult(s_State, Manager, Manager.LoadCurrent(), Localize("Configuration loaded"));
			}
			else if(i == 2)
			{
				char aPath[512];
				aPath[0] = '\0';
#if defined(CONF_FAMILY_WINDOWS)
				if(Manager.PickFile(false, "", aPath, sizeof(aPath)))
#else
				str_copy(aPath, s_State.m_aImportPathBuffer.data(), sizeof(aPath));
				if(aPath[0] != '\0')
#endif
				{
					std::string Error;
					const bool Ok = Manager.ImportZip(aPath, Error);
					ReportBindManagerResult(s_State, Manager, Ok, Localize("Configuration imported"));
					if(!Ok && !Error.empty())
						SetBindManagerStatus(s_State, true, Error.c_str());
				}
			}
			else
			{
				char aPath[512];
				aPath[0] = '\0';
				char aDefaultName[128];
				str_format(aDefaultName, sizeof(aDefaultName), "%s.zip", Manager.CurrentConfig()[0] ? Manager.CurrentConfig() : "bind");
#if defined(CONF_FAMILY_WINDOWS)
				if(Manager.PickFile(true, aDefaultName, aPath, sizeof(aPath)))
#else
				str_copy(aPath, s_State.m_aExportPathBuffer.data(), sizeof(aPath));
				if(aPath[0] != '\0')
#endif
				{
					std::string Error;
					const bool Ok = Manager.ExportZip(aPath, Error);
					ReportBindManagerResult(s_State, Manager, Ok, Localize("Configuration exported"));
					if(!Ok && !Error.empty())
						SetBindManagerStatus(s_State, true, Error.c_str());
				}
			}
		}
	}

#if !defined(CONF_FAMILY_WINDOWS)
	// Native file dialogs are Windows-only; fall back to explicit paths on other platforms.
	CUIRect FallbackRow;
	Column.HSplitTop(s_LineSize, &FallbackRow, &Column);
	FallbackRow.VSplitLeft(110.0f, &FallbackRow, &FallbackRow);
	Ui()->DoLabel(&FallbackRow, Localize("Zip path (fallback)"), s_EditBoxFontSize, TEXTALIGN_ML);
	Column.HSplitTop(s_LineSize, &FallbackRow, &Column);
	CUIRect ImportPathBox, ExportPathBox;
	FallbackRow.VSplitMid(&ImportPathBox, &ExportPathBox);
	Ui()->DoEditBox(&s_State.m_ImportPathInput, &ImportPathBox, s_EditBoxFontSize);
	Ui()->DoEditBox(&s_State.m_ExportPathInput, &ExportPathBox, s_EditBoxFontSize);
#endif

	// 配置列表
	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Configs"));
	RenderRanbiHint(Ui(), &Column, Localize("All folders in binds/ are listed. Selecting a config loads its manager.txt; rename changes the folder name."));

	CUIRect ConfigNameRow;
	Column.HSplitTop(s_LineSize, &ConfigNameRow, &Column);
	CUIRect ConfigNameBox, ConfigRenameBox;
	ConfigNameRow.VSplitLeft(ConfigNameRow.w - 110.0f, &ConfigNameBox, &ConfigRenameBox);
	ConfigRenameBox.HMargin(2.0f, &ConfigRenameBox);
	if(!s_ConfigNameInput.IsActive() && !s_State.m_ConfigNameDirty)
	{
		str_copy(s_aConfigNameBuffer.data(), Manager.CurrentConfig(), s_aConfigNameBuffer.size());
		s_ConfigNameInput.SetBuffer(s_aConfigNameBuffer.data(), s_aConfigNameBuffer.size());
	}
	if(Ui()->DoEditBox(&s_ConfigNameInput, &ConfigNameBox, s_EditBoxFontSize))
		s_State.m_ConfigNameDirty = true;
	if(DoButton_Menu(&s_aRenameConfigButton, Localize("Rename"), 0, &ConfigRenameBox))
	{
		const std::string NewName(s_aConfigNameBuffer.data());
		const bool Ok = Manager.RenameConfig(NewName.c_str());
		if(Ok)
		{
			s_State.m_ConfigNameDirty = false;
			Ui()->SetActiveItem(nullptr);
			s_ConfigNameInput.SetBuffer(s_aConfigNameBuffer.data(), s_aConfigNameBuffer.size());
			SetBindManagerStatus(s_State, false, Localize("Config renamed"));
		}
		else
		{
			char aError[512];
			if(Manager.LastError(aError, sizeof(aError)))
				SetBindManagerStatus(s_State, true, aError);
			else
				SetBindManagerStatus(s_State, true, Localize("Operation failed (see console for details)"));
		}
		s_State.m_Initialized = true;
	}

	CUIRect NewConfigRow;
	Column.HSplitTop(s_LineSize, &NewConfigRow, &Column);
	CUIRect NewConfigButton;
	NewConfigRow.VSplitRight(150.0f, &NewConfigRow, &NewConfigButton);
	NewConfigButton.HMargin(2.0f, &NewConfigButton);
	if(DoButton_Menu(&s_aNewConfigButton, Localize("New config"), 0, &NewConfigButton))
	{
		const bool Ok = Manager.CreateConfig();
		if(Ok)
		{
			s_State.m_ConfigNameDirty = false;
			Ui()->SetActiveItem(nullptr);
			SetBindManagerStatus(s_State, false, Localize("Config created"));
		}
		else
		{
			char aError[512];
			if(Manager.LastError(aError, sizeof(aError)))
				SetBindManagerStatus(s_State, true, aError);
			else
				SetBindManagerStatus(s_State, true, Localize("Operation failed (see console for details)"));
		}
	}

	const std::vector<std::string> &Configs = Manager.Configs();
	s_State.m_vConfigButtons.resize(std::max<size_t>(Configs.size(), 1));
	s_State.m_vConfigDeleteButtons.resize(std::max<size_t>(Configs.size(), 1));
	for(size_t i = 0; i < Configs.size(); i++)
	{
		CUIRect ConfigRow, ConfigSelectRow, ConfigDeleteButton;
		Column.HSplitTop(s_LineSize, &ConfigRow, &Column);
		ConfigRow.VSplitRight(86.0f, &ConfigSelectRow, &ConfigDeleteButton);
		ConfigDeleteButton.HMargin(2.0f, &ConfigDeleteButton);
		const bool Selected = str_comp(Configs[i].c_str(), Manager.CurrentConfig()) == 0;
		if(DoButton_Menu(&s_State.m_vConfigDeleteButtons[i], Localize("Delete"), 0, &ConfigDeleteButton))
		{
			s_BindManagerPendingDeleteConfig = Configs[i];
			char aMessage[512];
			str_format(aMessage, sizeof(aMessage), Localize("Are you sure you want to delete config \"%s\"?"), Configs[i].c_str());
			PopupConfirm(Localize("Delete config"), aMessage, Localize("Delete"), Localize("Cancel"), &CMenus::PopupConfirmDeleteBindManagerConfig);
		}
		if(DoButton_Menu(&s_State.m_vConfigButtons[i], Configs[i].c_str(), Selected ? 1 : 0, &ConfigSelectRow))
		{
			const bool Ok = Manager.SelectConfig(Configs[i].c_str(), true);
			if(Ok)
			{
				// task-88 (verifier B3): switching config clears the previously managed keys;
				// the selected config is applied by Load/join (closed loop).
				if(!Selected)
					Manager.ClearManagedBindings();
				s_State.m_ConfigNameDirty = false;
				Ui()->SetActiveItem(nullptr);
			}
			ReportBindManagerResult(s_State, Manager, Ok, Localize("Configuration selected"));
		}
	}
	if(Configs.empty())
		RenderRanbiHint(Ui(), &Column, Localize("No configs yet. Press Save to create the default config in binds/default/."));

	// 条目列表
	Column.HSplitTop(s_MarginBetweenSections, nullptr, &Column);
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Entries (manager.txt order)"));
	{
		CUIRect AddRow;
		Column.HSplitTop(s_LineSize, &AddRow, &Column);
		CUIRect AddButton;
		AddRow.VSplitRight(140.0f, &AddRow, &AddButton);
		AddButton.HMargin(2.0f, &AddButton);
		RenderRanbiHint(Ui(), &AddRow, Localize("Long-press the \u2630 handle and drag to reorder; order is written to manager.txt immediately."));
		if(DoButton_Menu(&s_aAddEntryButton, Localize("Add entry"), 0, &AddButton))
		{
			Manager.AddEntry();
			Manager.SetDirty();
		}
	}

	EnsureBindManagerUiSize(s_State, Manager.Entries());
	s_State.m_vHeaderRects.clear();
	std::vector<CBindManager::SEntry> &Entries = Manager.Entries();
	for(int i = 0; i < (int)Entries.size(); i++)
	{
		CBindManager::SEntry &Entry = Entries[i];

		CUIRect Header;
		Column.HSplitTop(26.0f, &Header, &Column);
		s_ScrollRegion.AddRect(Header);
		s_State.m_vHeaderRects.push_back(Header);

		CUIRect Handle, Expand, Enabled, KeyRect, NameRect, DeleteRect;
		Header.VSplitLeft(18.0f, &Handle, &Header);
		Header.VSplitLeft(18.0f, &Expand, &Header);
		Header.VSplitLeft(18.0f, &Enabled, &Header);
		Header.VSplitLeft(150.0f, &KeyRect, &Header);
		Header.VSplitLeft(4.0f, nullptr, &Header);
		Header.VSplitRight(24.0f, &NameRect, &DeleteRect);

		// Long-press drag handle.
		if(Ui()->MouseButtonClicked(0) && s_State.m_DragIndex < 0 && Ui()->MouseInside(&Handle))
		{
			s_State.m_DragIndex = i;
			s_State.m_DragStart = time_get();
		}
		if(s_State.m_DragIndex == i)
		{
			SLabelProperties Props;
			Props.SetColor(ColorRGBA(1.0f, 0.75f, 0.2f, 1.0f));
			Ui()->DoLabel(&Handle, "\u2630", s_FontSize, TEXTALIGN_MC, Props);
		}
		else
		{
			Ui()->DoLabel(&Handle, "\u2630", s_FontSize, TEXTALIGN_MC);
		}

		Expand.HMargin(3.0f, &Expand);
		if(Ui()->DoButtonLogic(&s_State.m_vExpandButtons[i], 0, &Expand, BUTTONFLAG_LEFT))
		{
			Entry.m_Expanded = !Entry.m_Expanded;
			Manager.SetDirty();
		}
		Ui()->DoLabel(&Expand, Entry.m_Expanded ? "\u25bc" : "\u25b6", s_FontSize * 0.8f, TEXTALIGN_MC);

		Enabled.HMargin(4.0f, &Enabled);
		if(DoButton_CheckBox(&Entry.m_Enabled, "", Entry.m_Enabled, &Enabled))
		{
			Entry.m_Enabled = !Entry.m_Enabled;
			if(!Entry.m_Enabled)
				Manager.CleanupEntryBinding(Entry); // task-87 (verifier B3): remove old key immediately
			Manager.SetDirty();
		}

		const CKeyBinder::CKeyReaderResult KeyReaderResult = GameClient()->m_KeyBinder.DoKeyReader(
			&s_State.m_vKeyReaderButtons[i], &s_State.m_vKeyClearButtons[i], &KeyRect,
			CBindSlot(Entry.m_Key, Entry.m_ModifierMask), false);
		if(KeyReaderResult.m_Aborted)
		{
			Manager.CleanupEntryBinding(Entry); // task-88 (verifier B3): old managed key must not stay bound
			Entry.m_Key = KEY_UNKNOWN;
			Entry.m_ModifierMask = 0;
			Manager.SetDirty();
		}
		else if(KeyReaderResult.m_Bind.m_Key != Entry.m_Key || KeyReaderResult.m_Bind.m_ModifierMask != Entry.m_ModifierMask)
		{
			Manager.CleanupEntryBinding(Entry); // unbind old key, then use the newly chosen one
			Entry.m_Key = KeyReaderResult.m_Bind.m_Key;
			Entry.m_ModifierMask = KeyReaderResult.m_Bind.m_ModifierMask;
			Manager.SetDirty();
		}

		if(!s_State.m_vNameInputs[i].IsActive())
		{
			str_copy(s_State.m_vNameBuffers[i].data(), Entry.m_Name.c_str(), s_State.m_vNameBuffers[i].size());
			s_State.m_vNameInputs[i].SetBuffer(s_State.m_vNameBuffers[i].data(), s_State.m_vNameBuffers[i].size());
		}
		if(Ui()->DoEditBox(&s_State.m_vNameInputs[i], &NameRect, s_EditBoxFontSize))
		{
			Entry.m_Name = s_State.m_vNameBuffers[i].data();
			Manager.SetDirty();
		}

		DeleteRect.HMargin(3.0f, &DeleteRect);
		if(Ui()->DoButtonLogic(&s_State.m_vDeleteButtons[i], 0, &DeleteRect, BUTTONFLAG_LEFT))
		{
			Manager.CleanupEntryBinding(Entry); // task-87 (verifier B3): remove old key immediately
			Manager.RemoveEntry(i);
			break;
		}
		Ui()->DoLabel(&DeleteRect, "X", s_FontSize * 0.8f, TEXTALIGN_MC);

		if(Entry.m_Expanded)
		{
			CUIRect InitRow;
			Column.HSplitTop(22.0f, &InitRow, &Column);
			s_ScrollRegion.AddRect(InitRow);
			CUIRect InitLabel, InitEdit, InitColorButton;
			InitRow.VSplitLeft(110.0f, &InitLabel, &InitEdit);
			InitEdit.VSplitRight(26.0f, &InitEdit, &InitColorButton);
			InitColorButton.HMargin(2.0f, &InitColorButton);
			Ui()->DoLabel(&InitLabel, Localize("Init commands"), s_EditBoxFontSize, TEXTALIGN_ML);
			DoButton_ColorPicker(&InitColorButton, &Entry.m_InitColor, false, BindManagerDefaultColorRgba(), false);
			if(!s_State.m_vInitInputs[i].IsActive())
			{
				str_copy(s_State.m_vInitBuffers[i].data(), Entry.m_Init.c_str(), s_State.m_vInitBuffers[i].size());
				s_State.m_vInitInputs[i].SetBuffer(s_State.m_vInitBuffers[i].data(), s_State.m_vInitBuffers[i].size());
			}
			if(Ui()->DoEditBox(&s_State.m_vInitInputs[i], &InitEdit, s_EditBoxFontSize))
			{
				Entry.m_Init = s_State.m_vInitBuffers[i].data();
				Manager.SetDirty();
			}
			if(!Entry.m_Init.empty() && !Manager.IsCommandLineExecutable(Entry.m_Init.c_str()))
				RenderRanbiWarning(Ui(), &Column, Localize("Init contains a command that is not executable (it is still written to init.cfg unchanged)."));

			if(Entry.m_Key == KEY_UNKNOWN)
				RenderRanbiWarning(Ui(), &Column, Localize("This entry has no key bound; it cannot be advanced by pressing a key."));

			if(Entry.m_vCommands.empty())
			{
				Entry.m_vCommands.emplace_back();
				Entry.m_vCommandColors.push_back(CBindManager::DEFAULT_COMMAND_COLOR);
			}
			const size_t NumCommands = Entry.m_vCommands.size();
			Entry.m_vCommandColors.resize(NumCommands, CBindManager::DEFAULT_COMMAND_COLOR);
			s_State.m_vCommandBuffers[i].resize(NumCommands);
			s_State.m_vCommandInputs[i].resize(NumCommands);
			if(s_State.m_vAddCommandButtons.size() <= (size_t)i)
				s_State.m_vAddCommandButtons.resize((size_t)i + 1);
			s_State.m_vDeleteCommandButtons[i].resize(NumCommands);
			for(size_t CommandIdx = 0; CommandIdx < NumCommands; CommandIdx++)
			{
				CUIRect CommandRow;
				Column.HSplitTop(22.0f, &CommandRow, &Column);
				s_ScrollRegion.AddRect(CommandRow);
				CUIRect CommandLabel, CommandEdit, CommandColorButton, DeleteCommand;
				CommandRow.VSplitLeft(110.0f, &CommandLabel, &CommandEdit);
				CommandEdit.VSplitRight(52.0f, &CommandEdit, &CommandColorButton);
				CommandColorButton.VSplitLeft(26.0f, &CommandColorButton, &DeleteCommand);
				CommandColorButton.HMargin(2.0f, &CommandColorButton);
				DoButton_ColorPicker(&CommandColorButton, &Entry.m_vCommandColors[CommandIdx], false, BindManagerDefaultColorRgba(), false);
				char aLabel[64];
				str_format(aLabel, sizeof(aLabel), Localize("Command %d"), (int)CommandIdx + 1);
				Ui()->DoLabel(&CommandLabel, aLabel, s_EditBoxFontSize, TEXTALIGN_ML);
				if(!s_State.m_vCommandInputs[i][CommandIdx].IsActive())
				{
					str_copy(s_State.m_vCommandBuffers[i][CommandIdx].data(), Entry.m_vCommands[CommandIdx].c_str(), s_State.m_vCommandBuffers[i][CommandIdx].size());
					s_State.m_vCommandInputs[i][CommandIdx].SetBuffer(s_State.m_vCommandBuffers[i][CommandIdx].data(), s_State.m_vCommandBuffers[i][CommandIdx].size());
				}
				if(Ui()->DoEditBox(&s_State.m_vCommandInputs[i][CommandIdx], &CommandEdit, s_EditBoxFontSize))
				{
					Entry.m_vCommands[CommandIdx] = s_State.m_vCommandBuffers[i][CommandIdx].data();
					Manager.SetDirty();
				}
				if(Entry.m_vCommands[CommandIdx][0] != '\0' && !Manager.IsCommandLineExecutable(Entry.m_vCommands[CommandIdx].c_str()))
					RenderRanbiWarning(Ui(), &Column, Localize("This command is not executable (it is still written to the cfg unchanged)."));
				DeleteCommand.HMargin(3.0f, &DeleteCommand);
				if(Ui()->DoButtonLogic(&s_State.m_vDeleteCommandButtons[i][CommandIdx], 0, &DeleteCommand, BUTTONFLAG_LEFT))
				{
					Entry.m_vCommands.erase(Entry.m_vCommands.begin() + CommandIdx);
					if(CommandIdx < Entry.m_vCommandColors.size())
						Entry.m_vCommandColors.erase(Entry.m_vCommandColors.begin() + CommandIdx);
					Manager.SetDirty();
					break;
				}
				Ui()->DoLabel(&DeleteCommand, "X", s_FontSize * 0.8f, TEXTALIGN_MC);
			}
			CUIRect AddCommandRow;
			Column.HSplitTop(22.0f, &AddCommandRow, &Column);
			s_ScrollRegion.AddRect(AddCommandRow);
			AddCommandRow.VSplitLeft(110.0f, nullptr, &AddCommandRow);
			if(DoButton_Menu(&s_State.m_vAddCommandButtons[i], Localize("Extend / add command"), 0, &AddCommandRow))
			{
				Entry.m_vCommands.emplace_back();
				Entry.m_vCommandColors.push_back(CBindManager::DEFAULT_COMMAND_COLOR);
				Manager.SetDirty();
			}
		}
	}

	// Finish long-press drag on release.
	if(s_State.m_DragIndex >= 0 && !Ui()->MouseButton(0))
	{
		int Target = s_State.m_DragIndex;
		for(size_t i = 0; i < s_State.m_vHeaderRects.size(); i++)
		{
			if(Ui()->MousePos().y >= s_State.m_vHeaderRects[i].y && Ui()->MousePos().y <= s_State.m_vHeaderRects[i].y + s_State.m_vHeaderRects[i].h)
			{
				Target = (int)i;
				break;
			}
		}
		if(Target != s_State.m_DragIndex)
		{
			Manager.MoveEntry(s_State.m_DragIndex, Target, true);
			SetBindManagerStatus(s_State, false, Localize("Entry order updated"));
		}
		else if(time_get() - s_State.m_DragStart < time_freq() / 2)
		{
			// short click on the handle only starts/aborts dragging (no reorder)
		}
		s_State.m_DragIndex = -1;
	}
	else if(s_State.m_DragIndex < 0)
	{
		s_State.m_DragStart = 0;
	}

	EndRanbiPageScroll(s_ScrollRegion, MainView, Column);
}

// integratedclient
