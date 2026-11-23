#ifndef GAME_CLIENT_COMPONENTS_RANBI_PRACTICE_GUEST_H
#define GAME_CLIENT_COMPONENTS_RANBI_PRACTICE_GUEST_H

#include <base/types.h>

#include <game/client/component.h>

#include <chrono>

// B-side component: enters the local practice server, connects the dummy if requested,
// and relays chat through the A/B file IPC. Team/state sync is server-side.
class CRanbiPracticeGuest : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnUpdate() override;
	void SendLineToHost(const char *pLine);

private:
	void InitChatPaths();
	void PollHostChat();
	void TryEnterGame(int Conn, const char *pName);

	bool m_DummyConnectAttempted = false;
	bool m_LoggedState = false;
	bool m_LoggedMainIngame = false;
	bool m_LoggedDummyEnter = false;
	int m_MainEnterAttempts = 0;
	int m_DummyEnterAttempts = 0;
	std::chrono::nanoseconds m_NextMainEnter{0};
	std::chrono::nanoseconds m_NextDummyEnter{0};
	char m_aChatA2B[IO_MAX_PATH_LENGTH] = "";
	char m_aChatB2A[IO_MAX_PATH_LENGTH] = "";
	int m_ChatReadOffset = 0;
	std::chrono::nanoseconds m_NextChatPoll{0};
};

#endif
