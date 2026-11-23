#include "practice_guest.h"

#include <base/dbg.h>
#include <base/io.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/components/chat.h>
#include <game/client/gameclient.h>

void CRanbiPracticeGuest::InitChatPaths()
{
	if(m_aChatA2B[0] != '\0')
		return;
	const int HostPid = g_Config.m_RcRanbiPracticeGuestChatPid;
	if(HostPid <= 0)
		return;
	char aName[160];
	str_format(aName, sizeof(aName), "ranbi_practice_chat_%d_a2b.txt", HostPid);
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, aName, m_aChatA2B, sizeof(m_aChatA2B));
	str_format(aName, sizeof(aName), "ranbi_practice_chat_%d_b2a.txt", HostPid);
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, aName, m_aChatB2A, sizeof(m_aChatB2A));
	dbg_msg("ranbi_practice_guest", "chat IPC paths: a2b='%s' b2a='%s'", m_aChatA2B, m_aChatB2A);
}

void CRanbiPracticeGuest::SendLineToHost(const char *pLine)
{
	if(pLine == nullptr || pLine[0] == '\0')
		return;
	InitChatPaths();
	if(m_aChatB2A[0] == '\0')
		return;
	IOHANDLE File = Storage()->OpenFile(m_aChatB2A, IOFLAG_APPEND, IStorage::TYPE_ABSOLUTE);
	if(!File)
		return;
	io_write(File, pLine, str_length(pLine));
	io_write(File, "\n", 1);
	io_close(File);
	dbg_msg("ranbi_practice_guest", "chat -> host: %s", pLine);
}

void CRanbiPracticeGuest::PollHostChat()
{
	InitChatPaths();
	if(m_aChatA2B[0] == '\0')
		return;
	const auto Now = time_get_nanoseconds();
	if(Now < m_NextChatPoll)
		return;
	m_NextChatPoll = Now + std::chrono::milliseconds(200);
	char *pData = const_cast<char *>(Storage()->ReadFileStr(m_aChatA2B, IStorage::TYPE_ABSOLUTE));
	if(pData == nullptr)
		return;
	const int Len = str_length(pData);
	if(Len < m_ChatReadOffset)
		m_ChatReadOffset = 0;
	char *pCursor = pData + m_ChatReadOffset;
	while(pCursor != nullptr && *pCursor != '\0')
	{
		char *pEnd = const_cast<char *>(str_find(pCursor, "\n"));
		if(pEnd != nullptr)
			*pEnd = '\0';
		if(*pCursor != '\0')
			GameClient()->m_Chat.AddLine(-1, 0, pCursor);
		pCursor = pEnd != nullptr ? pEnd + 1 : nullptr;
	}
	m_ChatReadOffset = Len;
	free(pData);
}

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
		m_aChatA2B[0] = '\0';
		m_aChatB2A[0] = '\0';
		m_ChatReadOffset = 0;
		return;
	}
	PollHostChat();
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
