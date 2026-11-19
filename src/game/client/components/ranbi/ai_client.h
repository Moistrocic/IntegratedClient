// ranbi: ported from RanbiClient 1.0.2 (DDNet 19.8) ai_client.h.
// AI auto reply (OpenAI-compatible API). This feature is default off and only
// replies when another player mentions the local player's name.
#ifndef GAME_CLIENT_COMPONENTS_RANBI_AI_CLIENT_H
#define GAME_CLIENT_COMPONENTS_RANBI_AI_CLIENT_H

#include <engine/client/enums.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

class IHttpRequest;

class CAiClient : public CComponent
{
	struct CContextEntry
	{
		char m_aSpeaker[16];
		char m_aText[2048];
		bool m_IsReply;
	};

	int64_t m_aNextReplyTime[NUM_DUMMIES];
	std::shared_ptr<IHttpRequest> m_pRequest;
	int m_ReplyDummy;
	int m_ReplyTeam;
	std::vector<CContextEntry> m_Context;
	bool m_KnowledgeHit;
	bool m_KnowledgeRequiredHit;
	int m_KnowledgeOtherHitCount;
	char m_aPendingSpeaker[16];
	char m_aPendingText[256];
	std::vector<std::string> m_ReplyQueue;
	char m_aLastError[256];
	// task-57: bounded feedback rewrite loop when the generated reply exceeds the 3 message /
	// max chars per message limits. Never sent to game chat; context only stores the final reply.
	bool m_IsRewriteRequest;
	int m_RewriteCount;
	char m_aRewritePrevReply[2048];

	bool ParseMention(const char *pText, int &Dummy);
	static void TruncateReply(char *pText, int Size);
	static void SplitSentences(const char *pText, std::vector<std::string> &Sentences);
	int LoadKnowledge(const char *pQuestion, char *aKbBuf, int KbBufSize);
	void SendRequest(const char *pText, const char *pSpeaker, int Dummy, int Team, const char *pPrevReply = nullptr);
	void HandleResponse();
	void FinalizeReply(const char *pReply);
	void SendChatMessage(int Dummy, int Team, const char *pMessage);
	void SetLastError(const char *pMessage);

public:
	CAiClient();

	int Sizeof() const override { return sizeof(*this); }

	void OnInit() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnUpdate() override;
	void OnReset() override;

	const char *LastError() const { return m_aLastError; }
	bool IsBusy() const { return m_pRequest != nullptr; }
};

#endif
