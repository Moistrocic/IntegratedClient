// integratedclient
// Ranbi settings page framework (task-3).
//
// The Ranbi feature pages are implemented by the later Ranbi integration tasks.
// Only fill in the bodies of the RenderSettingsRanbi* functions below; do not add
// a separate top-level page or "R" menu entry. The tab list, settings navigation
// entries and the Info page are owned by the integrated client framework.

#include <base/color.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/menus.h>
#include <game/client/components/ranbi/weapons.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <vector>

static int s_CurRanbiTab = CMenus::RANBI_TAB_GENERAL;

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

static void RenderRanbiPlaceholder(CUi *pUi, CUIRect MainView, const char *pMessage)
{
	CUIRect Label;
	MainView.HSplitTop(s_HeadlineFontSize, &Label, &MainView);
	pUi->DoLabel(&Label, Localize("Ranbi"), s_HeadlineFontSize, TEXTALIGN_ML);
	MainView.HSplitTop(s_Margin, nullptr, &MainView);
	MainView.HSplitTop(s_LineSize, &Label, &MainView);
	pUi->DoLabel(&Label, pMessage, s_FontSize, TEXTALIGN_ML);
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
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Points"));
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RcChatShowPoints, Localize("Show DDNet points in chat"), &g_Config.m_RcChatShowPoints, &Column, s_LineSize);
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
	RenderRanbiSectionHeadline(Ui(), &Column, Localize("Weapon angle list"));
	RenderRanbiHint(Ui(), &Column, Localize("Entries are saved to ranbi_weapons_angle.cfg when the client exits."));

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

	CUIRect ListView = RightView;
	ListView.HSplitTop(s_Margin, nullptr, &ListView);

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

void CMenus::RenderSettingsRanbiAi(CUIRect MainView)
{
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
		static CLineInput s_SystemPromptInput(g_Config.m_RcAiSystemPrompt, sizeof(g_Config.m_RcAiSystemPrompt));
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
	Ui()->DoScrollbarOption(&g_Config.m_RcAiMaxSentenceLength, &g_Config.m_RcAiMaxSentenceLength, &Slider, Localize("Max sentence length (characters)"), 1, 100);
	Column.HSplitTop(s_LineSize, &Slider, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcAiMinSentenceLength, &g_Config.m_RcAiMinSentenceLength, &Slider, Localize("Min sentence length (characters)"), 1, 100);
	Column.HSplitTop(s_LineSize, &Slider, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcAiTemperature, &g_Config.m_RcAiTemperature, &Slider, Localize("Temperature (x0.1)"), 0, 20);
	Column.HSplitTop(s_LineSize, &Slider, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_RcAiContextCount, &g_Config.m_RcAiContextCount, &Slider, Localize("Context count"), 0, 20);

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
	RenderRanbiHint(Ui(), &Column, Localize("Text files in ranbi/knowledge/*.txt are used as extra context; ddnet_wiki.txt is created on first start."));

	EndRanbiPageScroll(s_ScrollRegion, MainView, Column);
}

// integratedclient
