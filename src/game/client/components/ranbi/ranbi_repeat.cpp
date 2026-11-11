// ranbi: task-12 "repeat the last player message" feature.

#include "ranbi_repeat.h"

#include <base/str.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/gameclient.h>

CRanbiRepeat::CRanbiRepeat()
{
	OnReset();
}

void CRanbiRepeat::OnReset()
{
	m_aLastMessage[0] = '\0';
	m_HasLastMessage = false;
	m_LastTeamMessage = false;
	m_LastRepeatTime = 0;
}

void CRanbiRepeat::OnConsoleInit()
{
	Console()->Register("ranbi_repeat_last", "", CFGFLAG_CLIENT, ConRepeatLast, this, "Repeat the last chat message from another player");
}

void CRanbiRepeat::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType != NETMSGTYPE_SV_CHAT)
		return;

	const CNetMsg_Sv_Chat *pMsg = static_cast<CNetMsg_Sv_Chat *>(pRawMsg);
	if(pMsg->m_ClientId < 0 || pMsg->m_pMessage[0] == '\0')
		return;
	// Skip whispers: repeating them in public would leak private messages.
	if(pMsg->m_Team < 0 || pMsg->m_Team > 1)
		return;

	// Ignore our own messages (main connection and dummy).
	const int MainLocalId = GameClient()->m_aLocalIds[0];
	const int DummyLocalId = GameClient()->m_aLocalIds[1];
	if(pMsg->m_ClientId == MainLocalId || pMsg->m_ClientId == DummyLocalId)
		return;

	str_copy(m_aLastMessage, pMsg->m_pMessage, sizeof(m_aLastMessage));
	m_LastTeamMessage = pMsg->m_Team == 1;
	m_HasLastMessage = true;
}

void CRanbiRepeat::RepeatLast()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	if(!m_HasLastMessage || m_aLastMessage[0] == '\0')
		return;

	// Safe failure on cooldown: silent no-op instead of unbounded spam.
	const int64_t Now = time_get();
	if(m_LastRepeatTime != 0 && Now - m_LastRepeatTime < time_freq())
		return;
	m_LastRepeatTime = Now;

	if(Client()->IsSixup())
	{
		protocol7::CNetMsg_Cl_Say Msg;
		Msg.m_Mode = m_LastTeamMessage ? protocol7::CHAT_TEAM : protocol7::CHAT_ALL;
		Msg.m_Target = -1;
		Msg.m_pMessage = m_aLastMessage;
		Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL, true);
	}
	else
	{
		CNetMsg_Cl_Say Msg;
		Msg.m_Team = m_LastTeamMessage ? 1 : 0;
		Msg.m_pMessage = m_aLastMessage;
		Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);
	}
}

void CRanbiRepeat::ConRepeatLast(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	static_cast<CRanbiRepeat *>(pUserData)->RepeatLast();
}
