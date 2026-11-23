#include "practice_guest.h"

#include <base/dbg.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

void CRanbiPracticeGuest::TryEnterGame(int Conn, const char *pName)
{
	auto &Next = Conn == IClient::CONN_MAIN ? m_NextMainEnter : m_NextDummyEnter;
	auto &Attempts = Conn == IClient::CONN_MAIN ? m_MainEnterAttempts : m_DummyEnterAttempts;
	const auto Now = time_get_nanoseconds();
	if(Now < Next || Attempts >= 20)
		return;
	Next = Now + std::chrono::milliseconds(1000);
	Attempts++;
	Client()->EnterGame(Conn);
	dbg_msg("ranbi_practice_guest", "EnterGame(%s) attempt %d", pName, Attempts);
}

void CRanbiPracticeGuest::OnUpdate()
{
	if(g_Config.m_RcRanbiPracticeGuest == 0)
	{
		m_DummyConnectAttempted = false;
		m_LoggedState = false;
		m_LoggedMainIngame = false;
		m_LoggedDummyEnter = false;
		m_MainEnterAttempts = 0;
		m_DummyEnterAttempts = 0;
		return;
	}
	if(Client()->State() != IClient::STATE_ONLINE || GameClient()->Map() == nullptr || !GameClient()->Map()->IsLoaded())
		return;
	if(!m_LoggedState)
	{
		m_LoggedState = true;
		dbg_msg("ranbi_practice_guest", "guest connected; state sync handled by server file (body_state='%s' dummy_state='%s')",
			g_Config.m_RcRanbiPracticeGuestBodyState, g_Config.m_RcRanbiPracticeGuestDummyState);
	}

	const int MainId = GameClient()->m_aLocalIds[0];
	if(MainId < 0)
	{
		TryEnterGame(IClient::CONN_MAIN, "main");
		return;
	}
	if(!m_LoggedMainIngame)
	{
		m_LoggedMainIngame = true;
		dbg_msg("ranbi_practice_guest", "guest main ingame id=%d team=%d", MainId, GameClient()->m_aClients[MainId].m_Team);
	}

	if(!g_Config.m_RcRanbiPracticeGuestDummy)
		return;
	if(!m_DummyConnectAttempted && !Client()->DummyConnected() && !Client()->DummyConnecting())
	{
		m_DummyConnectAttempted = true;
		Client()->DummyConnect();
		dbg_msg("ranbi_practice_guest", "requested dummy connection");
	}
	if(Client()->DummyConnected())
	{
		const int DummyId = GameClient()->m_aLocalIds[1];
		if(DummyId < 0)
			TryEnterGame(IClient::CONN_DUMMY, "dummy");
		else if(!m_LoggedDummyEnter)
		{
			m_LoggedDummyEnter = true;
			dbg_msg("ranbi_practice_guest", "guest dummy ingame id=%d team=%d", DummyId, GameClient()->m_aClients[DummyId].m_Team);
		}
	}
}
