// integratedclient
// IntegratedClient project information page (top-level settings page).

#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

void CMenus::RenderSettingsIntegratedClient(CUIRect MainView)
{
	const float LineSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float MarginBetweenViews = 30.0f;
	const float HeadlineFontSize = 20.0f;
	const float HeadlineHeight = HeadlineFontSize;

	CUIRect LeftView, RightView, Button, Label;
	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	// Project information
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, INTEGRATEDCLIENT_NAME, HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	char aBuf[256];
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	str_format(aBuf, sizeof(aBuf), "%s %s", Localize("Version"), INTEGRATEDCLIENT_VERSION);
	Ui()->DoLabel(&Label, aBuf, 14.0f, TEXTALIGN_ML);

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	str_format(aBuf, sizeof(aBuf), "%s BestClient / TClient / DDNet", Localize("Based on"));
	Ui()->DoLabel(&Label, aBuf, 14.0f, TEXTALIGN_ML);

	LeftView.HSplitTop(MarginBetweenViews, nullptr, &LeftView);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, Localize("Source clients"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	Ui()->DoLabel(&Label, "BestClient (base)", 14.0f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	Ui()->DoLabel(&Label, "TClient / DDNet (upstream)", 14.0f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	Ui()->DoLabel(&Label, Localize("RanbiClient (integrated)"), 14.0f, TEXTALIGN_ML);

	// Links
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, Localize("Links"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);

	RightView.HSplitTop(LineSize * 2.0f, &Button, &RightView);
	static CButtonContainer s_GithubButton;
	if(DoButtonLineSize_Menu(&s_GithubButton, "GitHub", 0, &Button, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://github.com/Moistrocic/IntegratedClient");

	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(LineSize * 2.0f, &Button, &RightView);
	static CButtonContainer s_IntegrationRegistryButton;
	if(DoButtonLineSize_Menu(&s_IntegrationRegistryButton, Localize("Integration registry"), 0, &Button, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://github.com/Moistrocic/IntegratedClient/blob/main/docs/integration.md");
}
// integratedclient
