/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_start.h"

#include <engine/client/updater.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>
#include <engine/updater.h>

#include <generated/client_data.h>

#include <algorithm>

// bestclient
#include <game/client/components/bestclient/version.h>
// bestclient
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

void CMenusStart::RenderStartMenu(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);

	const float Rounding = 10.0f;
	const float VMargin = MainView.w / 2 - 190.0f;
	const float ExtMenuBottomOffset = 40.0f;

	CUIRect Button;
	int NewPage = -1;

	const auto SetIconMode = [&]() {
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	};
	const auto ResetIconMode = [&]() {
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	};

	CUIRect ExtMenu;
	MainView.VSplitLeft(30.0f, nullptr, &ExtMenu);
	ExtMenu.VSplitLeft(100.0f, &ExtMenu, nullptr);
	ExtMenu.HSplitBottom(ExtMenuBottomOffset, &ExtMenu, nullptr);

	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_DiscordButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_DiscordButton, BcLocalize("Discord"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f))) // bestclient
		Client()->ViewLink("https://discord.gg/bestclient");

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr);
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_TelegramButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_TelegramButton, BcLocalize("Telegram"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f))) // bestclient
		Client()->ViewLink("https://t.me/bestddnet");

	constexpr float LogoW = 360.0f;
	constexpr float LogoH = 103.0f;
	constexpr float LogoGap = 30.0f;
	constexpr float ButtonH = 40.0f;
	constexpr float ButtonGap = 5.0f;
	constexpr int MenuButtonCount = 5;
	const float ButtonsH = MenuButtonCount * ButtonH + (MenuButtonCount - 1) * ButtonGap;
	const float TotalBlockH = LogoH + LogoGap + ButtonsH;

	CUIRect Menu;
	MainView.VMargin(VMargin, &Menu);

	const float BlockStartY = MainView.y + (MainView.h - TotalBlockH) / 2.0f - 50.0f;

	{
		const float LogoX = MainView.x + MainView.w / 2.0f - LogoW / 2.0f;
		const IGraphics::CTextureHandle &LogoTexture = GameClient()->m_Menus.BcLogoTexture();
		Graphics()->TextureSet(LogoTexture.IsValid() && !LogoTexture.IsNullTexture() ? LogoTexture : g_pData->m_aImages[IMAGE_BANNER].m_Id);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1, 1, 1, 1);
		IGraphics::CQuadItem LogoQuad(LogoX, BlockStartY, LogoW, LogoH);
		Graphics()->QuadsDrawTL(&LogoQuad, 1);
		Graphics()->QuadsEnd();
	}

	const auto ScaleButtonRect = [](const CUIRect &Base, float Scale) -> CUIRect {
		CUIRect Out = Base;
		Out.w *= Scale;
		Out.h *= Scale;
		Out.x = Base.x + (Base.w - Out.w) * 0.5f;
		Out.y = Base.y + (Base.h - Out.h) * 0.5f;
		return Out;
	};

	CUIRect aMenuButtons[MenuButtonCount];
	{
		float Y = BlockStartY + LogoH + LogoGap;
		for(int i = 0; i < MenuButtonCount; ++i)
		{
			aMenuButtons[i] = {Menu.x, Y, Menu.w, ButtonH};
			Y += ButtonH + ButtonGap;
		}
	}

	static float s_aMenuButtonScale[MenuButtonCount] = {};
	static bool s_MenuButtonScaleInit = false;
	if(!s_MenuButtonScaleInit)
	{
		for(int i = 0; i < MenuButtonCount; ++i)
			s_aMenuButtonScale[i] = 1.0f;
		s_MenuButtonScaleInit = true;
	}

	{
		const bool AnimEnabled = g_Config.m_BcMainMenuAnimation != 0;
		int HoveredIndex = -1;
		if(AnimEnabled)
		{
			for(int i = 0; i < MenuButtonCount; ++i)
			{
				const CUIRect Scaled = ScaleButtonRect(aMenuButtons[i], s_aMenuButtonScale[i]);
				if(Ui()->MouseHovered(&Scaled))
				{
					HoveredIndex = i;
					break;
				}
			}
		}
		const bool AnyHovered = HoveredIndex != -1;
		const float HoverScale = 1.08f;
		const float OtherScale = 0.94f;
		const float Speed = (float)g_Config.m_BcMainMenuAnimationSpeed;
		const float Blend = AnimEnabled ? std::clamp(Client()->RenderFrameTime() * Speed, 0.0f, 1.0f) : 1.0f;
		for(int i = 0; i < MenuButtonCount; ++i)
		{
			const float Target = (AnimEnabled && AnyHovered) ? (i == HoveredIndex ? HoverScale : OtherScale) : 1.0f;
			s_aMenuButtonScale[i] += (Target - s_aMenuButtonScale[i]) * Blend;
		}
	}

	{
		CUIRect ScaledButton = ScaleButtonRect(aMenuButtons[0], s_aMenuButtonScale[0]);
		static CButtonContainer s_PlayButton;
		if((GameClient()->m_Menus.DoButton_MenuEx(&s_PlayButton, Localize("Play", "Start menu"), 0, &ScaledButton, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "play_game" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), true) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P)))
			NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
	}

	{
		CUIRect ScaledButton = ScaleButtonRect(aMenuButtons[1], s_aMenuButtonScale[1]);
		static CButtonContainer s_DemoButton;
		if((GameClient()->m_Menus.DoButton_MenuEx(&s_DemoButton, Localize("Demos"), 0, &ScaledButton, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "demos" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), true) || CheckHotKey(KEY_D)))
			NewPage = CMenus::PAGE_DEMOS;
	}

	{
		CUIRect ScaledButton = ScaleButtonRect(aMenuButtons[2], s_aMenuButtonScale[2]);
		static CButtonContainer s_MapEditorButton;
		if((GameClient()->m_Menus.DoButton_MenuEx(&s_MapEditorButton, Localize("Editor"), 0, &ScaledButton, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "editor" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, GameClient()->Editor()->HasUnsavedData() ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), true) || CheckHotKey(KEY_E)))
		{
			g_Config.m_ClEditor = 1;
			Input()->MouseModeRelative();
		}
	}

	{
		CUIRect ScaledButton = ScaleButtonRect(aMenuButtons[3], s_aMenuButtonScale[3]);
		static CButtonContainer s_LocalServerButton;
		const bool LocalServerRunning = GameClient()->m_LocalServer.IsServerRunning();
		if((GameClient()->m_Menus.DoButton_MenuEx(&s_LocalServerButton, LocalServerRunning ? Localize("Stop server") : Localize("Run server"), 0, &ScaledButton, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "local_server" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, LocalServerRunning ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), true) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R))))
		{
			if(LocalServerRunning)
				GameClient()->m_LocalServer.KillServer();
			else
				GameClient()->m_LocalServer.RunServer({});
		}
	}

	{
		CUIRect ScaledButton = ScaleButtonRect(aMenuButtons[4], s_aMenuButtonScale[4]);
		static CButtonContainer s_SettingsButton;
		if(GameClient()->m_Menus.DoButton_MenuEx(&s_SettingsButton, Localize("Settings"), 0, &ScaledButton, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "settings" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), true) || CheckHotKey(KEY_S))
			NewPage = CMenus::PAGE_SETTINGS;
	}

	{
		CUIRect QuitArea;
		MainView.VMargin(VMargin, &QuitArea);
		QuitArea.HSplitBottom(25.0f, &QuitArea, nullptr);
		QuitArea.HSplitBottom(40.0f, &QuitArea, &Button);
		CUIRect QuitButton = Button;
		QuitButton.w = QuitButton.h;
		QuitButton.x += (Button.w - QuitButton.w) / 2.0f;
		static CButtonContainer s_QuitButton;
		bool UsedEscape = false;
		SetIconMode();
		if(GameClient()->m_Menus.DoButton_Menu(&s_QuitButton, FontIcon::POWER_OFF, 0, &QuitButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, Rounding, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
		{
			ResetIconMode();
			if(g_Config.m_BcConfirmQuit || UsedEscape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
				GameClient()->m_Menus.ShowQuitPopup();
			else
				Client()->Quit();
		}
		ResetIconMode();
	}

	{
		CUIRect CurVersion, ConsoleButton;
		// integratedclient: the 40px strip on the right is reserved for the "NEW" release badge
		CUIRect VersionBadge;
		MainView.HSplitBottom(95.0f, nullptr, &CurVersion);
		CurVersion.VSplitRight(40.0f, &CurVersion, &VersionBadge);
		CurVersion.HSplitTop(20.0f, &ConsoleButton, &CurVersion);
		CurVersion.HSplitTop(5.0f, nullptr, &CurVersion);
		ConsoleButton.VSplitRight(40.0f, nullptr, &ConsoleButton);

		CUIRect VersionLine1, VersionLine2, VersionLine3, VersionLine4;
		CurVersion.HSplitTop(16.0f, &VersionLine1, &CurVersion);
		CurVersion.HSplitTop(2.0f, nullptr, &CurVersion);
		CurVersion.HSplitTop(16.0f, &VersionLine2, &CurVersion);
		CurVersion.HSplitTop(2.0f, nullptr, &CurVersion);
		CurVersion.HSplitTop(16.0f, &VersionLine3, &CurVersion);
		CurVersion.HSplitTop(2.0f, nullptr, &CurVersion);
		CurVersion.HSplitTop(16.0f, &VersionLine4, &CurVersion);

		char aDDNetBuf[64];
		char aTClientBuf[64];
		char aBestClientBuf[64];
		char aIntegratedBuf[64];
		str_format(aDDNetBuf, sizeof(aDDNetBuf), "DDNet %s", GAME_RELEASE_VERSION);
		str_format(aTClientBuf, sizeof(aTClientBuf), "TClient %s", TCLIENT_VERSION);
		str_format(aBestClientBuf, sizeof(aBestClientBuf), "BestClient %s", BESTCLIENT_VERSION);
		// integratedclient: project version goes after the source client versions
		str_format(aIntegratedBuf, sizeof(aIntegratedBuf), "%s %s", INTEGRATEDCLIENT_NAME, INTEGRATEDCLIENT_VERSION);
		Ui()->DoLabel(&VersionLine1, aDDNetBuf, 14.0f, TEXTALIGN_MR);
		Ui()->DoLabel(&VersionLine2, aTClientBuf, 14.0f, TEXTALIGN_MR);
		Ui()->DoLabel(&VersionLine3, aBestClientBuf, 14.0f, TEXTALIGN_MR);
		Ui()->DoLabel(&VersionLine4, aIntegratedBuf, 14.0f, TEXTALIGN_MR);

		// integratedclient: "new release available" badge. Clicking it opens the GitHub
		// release page; it never downloads or installs anything. The check itself lives in
		// CMenus (async, silent on failure).
		if(GameClient()->m_Menus.HasIntegratedClientVersionUpdate())
		{
			CUIRect Badge = VersionBadge;
			Badge.h = 14.0f;
			Badge.y = VersionLine4.y + (VersionLine4.h - Badge.h) / 2.0f;
			Badge.x += 2.0f;
			Badge.w -= 4.0f;
			static CButtonContainer s_NewReleaseButton;
			const bool Clicked = Ui()->DoButtonLogic(&s_NewReleaseButton, 0, &Badge, BUTTONFLAG_LEFT) != 0;
			const bool Hovered = Ui()->MouseInside(&Badge);
			const ColorRGBA BadgeColor = Hovered ? ColorRGBA(1.0f, 0.42f, 0.20f, 1.0f) : ColorRGBA(0.91f, 0.26f, 0.15f, 1.0f);
			Graphics()->DrawRect(Badge.x, Badge.y, Badge.w, Badge.h, BadgeColor, IGraphics::CORNER_ALL, 3.5f);
			const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			Ui()->DoLabel(&Badge, "NEW", 9.0f, TEXTALIGN_MC);
			TextRender()->TextColor(PrevTextColor);
			if(Clicked)
				Client()->ViewLink("https://github.com/Moistrocic/IntegratedClient/releases/latest");
		}

		static CButtonContainer s_ConsoleButton;
		SetIconMode();
		if(GameClient()->m_Menus.DoButton_Menu(&s_ConsoleButton, FontIcon::TERMINAL, 0, &ConsoleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f)))
			GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
		ResetIconMode();
	}

	if(NewPage != -1)
	{
		GameClient()->m_Menus.SetShowStart(false);
		GameClient()->m_Menus.SetMenuPage(NewPage);
	}

	RenderUpdatePopup(MainView);
}

void CMenusStart::RenderUpdatePopup(CUIRect MainView)
{
	(void)MainView;
	IUpdater *pUpdater = Client()->Updater();
	if(pUpdater == nullptr)
		return;
	const IUpdater::EUpdaterState State = pUpdater->GetCurrentState();
	if(State == IUpdater::CLEAN || State == IUpdater::GETTING_MANIFEST)
		return;
	if(State == IUpdater::VERSION_AVAILABLE)
	{
		const char *pLatest = pUpdater->GetLatestVersionString();
		if(pLatest[0] == '\0')
			return;
		if(g_Config.m_RcUpdateSkippedVersion[0] != '\0' && !RanbiVersionIsNewer(pLatest, g_Config.m_RcUpdateSkippedVersion))
			return;
		if(str_comp(m_aUpdateSeenVersion, pLatest) != 0)
		{
			str_copy(m_aUpdateSeenVersion, pLatest, sizeof(m_aUpdateSeenVersion));
			m_UpdatePopupCancelled = false;
		}
		if(m_UpdatePopupCancelled)
			return;
	}
	if(State == IUpdater::FAIL)
	{
		if(!m_UpdateStarted || m_UpdatePopupCancelled)
			return;
	}

	if(State == IUpdater::NEED_RESTART && m_UpdateStarted)
	{
		pUpdater->ApplyUpdateAndRestart();
		return;
	}

	CUIRect Screen = *Ui()->Screen();
	Graphics()->DrawRect(0.0f, 0.0f, Screen.w, Screen.h, ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f), IGraphics::CORNER_NONE, 0.0f);

	CUIRect Box;
	Box.w = std::min(560.0f, Screen.w - 40.0f);
	Box.h = 270.0f;
	Box.x = (Screen.w - Box.w) / 2.0f;
	Box.y = (Screen.h - Box.h) / 2.0f;
	Graphics()->DrawRect(Box.x, Box.y, Box.w, Box.h, ColorRGBA(0.08f, 0.09f, 0.08f, 1.0f), IGraphics::CORNER_ALL, 10.0f);
	Graphics()->DrawRect(Box.x, Box.y, Box.w, 4.0f, ColorRGBA(0.68f, 0.82f, 0.28f, 1.0f), IGraphics::CORNER_ALL, 2.0f);

	CUIRect Label = Box;
	Label.x += 20.0f;
	Label.w -= 40.0f;
	Label.y += 20.0f;
	Label.h = 30.0f;
	Ui()->DoLabel(&Label, Localize("IntegratedClient Update"), 24.0f, TEXTALIGN_ML);

	char aBuf[512];
	if(State == IUpdater::VERSION_AVAILABLE)
	{
		str_format(aBuf, sizeof(aBuf), Localize("Version %s is available (current: %s)."), pUpdater->GetLatestVersionString(), INTEGRATEDCLIENT_VERSION);
		Label.y += 42.0f;
		Label.h = 60.0f;
		Ui()->DoLabel(&Label, aBuf, 14.0f, TEXTALIGN_ML);
		Label.y += 52.0f;
		Label.h = 28.0f;
		Ui()->DoLabel(&Label, Localize("Update now? The client will download the package and restart."), 13.0f, TEXTALIGN_ML);

		CUIRect Buttons = Label;
		Buttons.y = Box.y + Box.h - 62.0f;
		Buttons.h = 40.0f;
		const float Gap = 12.0f;
		CUIRect B1, B2, B3;
		Buttons.VSplitLeft((Buttons.w - 2.0f * Gap) / 3.0f, &B1, &Buttons);
		Buttons.VSplitLeft(Gap, nullptr, &Buttons);
		Buttons.VSplitLeft((Buttons.w - Gap) / 2.0f, &B2, &Buttons);
		Buttons.VSplitLeft(Gap, nullptr, &Buttons);
		B3 = Buttons;
		static CButtonContainer s_ConfirmButton;
		static CButtonContainer s_LaterButton;
		static CButtonContainer s_NeverButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_ConfirmButton, Localize("Update and restart"), 0, &B1))
		{
			m_UpdateStarted = true;
			pUpdater->InitiateUpdate();
		}
		if(GameClient()->m_Menus.DoButton_Menu(&s_LaterButton, Localize("Later"), 0, &B2))
			m_UpdatePopupCancelled = true;
		if(GameClient()->m_Menus.DoButton_Menu(&s_NeverButton, Localize("Never this version"), 0, &B3))
		{
			str_copy(g_Config.m_RcUpdateSkippedVersion, pUpdater->GetLatestVersionString(), sizeof(g_Config.m_RcUpdateSkippedVersion));
			m_UpdatePopupCancelled = true;
		}
		return;
	}

	if(State == IUpdater::DOWNLOADING)
	{
		char aFile[256];
		pUpdater->GetCurrentFile(aFile, sizeof(aFile));
		str_format(aBuf, sizeof(aBuf), Localize("Downloading from %s: %s (%d%%)"), pUpdater->GetDownloadSource(), aFile, pUpdater->GetCurrentPercent());
		Label.y += 50.0f;
		Label.h = 30.0f;
		Ui()->DoLabel(&Label, aBuf, 15.0f, TEXTALIGN_ML);
		CUIRect Bar = Label;
		Bar.y += 44.0f;
		Bar.h = 18.0f;
		Graphics()->DrawRect(Bar.x, Bar.y, Bar.w, Bar.h, ColorRGBA(0.18f, 0.18f, 0.18f, 1.0f), IGraphics::CORNER_ALL, 4.0f);
		Graphics()->DrawRect(Bar.x, Bar.y, Bar.w * pUpdater->GetCurrentPercent() / 100.0f, Bar.h, ColorRGBA(0.68f, 0.82f, 0.28f, 1.0f), IGraphics::CORNER_ALL, 4.0f);
		CUIRect Hint = Bar;
		Hint.y = Box.y + Box.h - 94.0f;
		Hint.h = 24.0f;
		Ui()->DoLabel(&Hint, Localize("Keep the client open until the update finishes."), 12.0f, TEXTALIGN_MC);
		CUIRect Switch = Bar;
		Switch.y = Box.y + Box.h - 58.0f;
		Switch.h = 36.0f;
		Switch.w = 300.0f;
		Switch.x = Box.x + (Box.w - Switch.w) / 2.0f;
		static CButtonContainer s_SwitchSourceButton;
		const char *pSwitchLabel = pUpdater->IsUsingFallbackDownload() ? Localize("Switch to GitHub download") : Localize("Switch to backup server download");
		if(GameClient()->m_Menus.DoButton_Menu(&s_SwitchSourceButton, pSwitchLabel, 0, &Switch))
			pUpdater->SwitchDownloadSource();
		return;
	}

	if(State == IUpdater::NEED_RESTART)
	{
		Label.y += 50.0f;
		Label.h = 40.0f;
		Ui()->DoLabel(&Label, Localize("Update downloaded. Restart to apply."), 16.0f, TEXTALIGN_ML);
		CUIRect B = Label;
		B.y = Box.y + Box.h - 62.0f;
		B.h = 40.0f;
		B.w = 220.0f;
		static CButtonContainer s_RestartButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_RestartButton, Localize("Restart and apply"), 0, &B))
		{
			m_UpdateStarted = true;
			pUpdater->ApplyUpdateAndRestart();
		}
		return;
	}

	if(State == IUpdater::FAIL)
	{
		Label.y += 50.0f;
		Label.h = 40.0f;
		Ui()->DoLabel(&Label, Localize("Update check or download failed."), 16.0f, TEXTALIGN_ML);

		const bool DownloadFailed = pUpdater->IsDownloadFailed();
		const bool CanSwitch = pUpdater->CanSwitchDownloadSource();
		const int NumButtons = 1 + (DownloadFailed ? 1 : 0) + (CanSwitch ? 1 : 0);
		CUIRect Buttons = Label;
		Buttons.y = Box.y + Box.h - 62.0f;
		Buttons.h = 40.0f;
		const float Gap = 12.0f;
		const float ButtonWidth = (Buttons.w - (NumButtons - 1) * Gap) / NumButtons;
		CUIRect Slots[3];
		for(int i = 0; i < NumButtons; i++)
		{
			Buttons.VSplitLeft(ButtonWidth, &Slots[i], &Buttons);
			if(i + 1 < NumButtons)
				Buttons.VSplitLeft(Gap, nullptr, &Buttons);
		}
		int Slot = 0;
		CUIRect RetryButton, SwitchButton, CloseButton;
		if(DownloadFailed)
			RetryButton = Slots[Slot++];
		if(CanSwitch)
			SwitchButton = Slots[Slot++];
		CloseButton = Slots[Slot++];
		static CButtonContainer s_RetryButton;
		static CButtonContainer s_CloseButton;
		static CButtonContainer s_SwitchSourceButton;
		if(DownloadFailed && GameClient()->m_Menus.DoButton_Menu(&s_RetryButton, Localize("Retry"), 0, &RetryButton))
			pUpdater->RetryDownload();
		if(DownloadFailed && CanSwitch)
		{
			const char *pSwitchLabel = pUpdater->IsUsingFallbackDownload() ? Localize("Switch to GitHub download") : Localize("Switch to backup server download");
			if(GameClient()->m_Menus.DoButton_Menu(&s_SwitchSourceButton, pSwitchLabel, 0, &SwitchButton))
				pUpdater->SwitchDownloadSource();
		}
		if(GameClient()->m_Menus.DoButton_Menu(&s_CloseButton, Localize("Close"), 0, &CloseButton))
			m_UpdatePopupCancelled = true;
	}
}

bool CMenusStart::CheckHotKey(int Key) const
{
	return !Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && !Input()->AltIsPressed() &&
	       Input()->KeyPress(Key) &&
	       !GameClient()->m_GameConsole.IsActive();
}
