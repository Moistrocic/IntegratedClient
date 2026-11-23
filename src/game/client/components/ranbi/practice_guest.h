#ifndef GAME_CLIENT_COMPONENTS_RANBI_PRACTICE_GUEST_H
#define GAME_CLIENT_COMPONENTS_RANBI_PRACTICE_GUEST_H

#include <game/client/component.h>

#include <chrono>

// B-side component: the second client process only connects, enters the game, connects
// the dummy if requested and then stays idle. All team/practice/position/weapon/status
// sync is applied server-side from the sync file.
class CRanbiPracticeGuest : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnUpdate() override;

private:
	void TryEnterGame(int Conn, const char *pName);

	bool m_DummyConnectAttempted = false;
	bool m_LoggedState = false;
	bool m_LoggedMainIngame = false;
	bool m_LoggedDummyEnter = false;
	int m_MainEnterAttempts = 0;
	int m_DummyEnterAttempts = 0;
	std::chrono::nanoseconds m_NextMainEnter{0};
	std::chrono::nanoseconds m_NextDummyEnter{0};
};

#endif
