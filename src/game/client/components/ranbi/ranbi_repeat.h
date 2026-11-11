// ranbi: task-12 "repeat the last player message" feature.
// Tracks the most recent chat message from another player and repeats it in the
// same channel when the console command (bound via DoLine_KeyReader) is used.
#ifndef GAME_CLIENT_COMPONENTS_RANBI_RANBI_REPEAT_H
#define GAME_CLIENT_COMPONENTS_RANBI_RANBI_REPEAT_H

#include <base/time.h>

#include <engine/console.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

class CRanbiRepeat : public CComponent
{
	char m_aLastMessage[MAX_CHAT_LENGTH];
	bool m_HasLastMessage = false;
	bool m_LastTeamMessage = false;
	int64_t m_LastRepeatTime = 0;

	static void ConRepeatLast(IConsole::IResult *pResult, void *pUserData);
	void RepeatLast();

public:
	CRanbiRepeat();
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnReset() override;
};

#endif
