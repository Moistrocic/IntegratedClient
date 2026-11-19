// ranbi: ported from RanbiClient 1.0.2 (DDNet 19.8) ai_client.cpp.
// Ported to the DDNet 20.1.1 / BestClient base: CHttpRequest -> IHttpRequest,
// Http()->Run(shared_ptr<IHttpRequest>), Client()->ServerInfo(), and sixup chat
// sending. The feature is default off (rc_ai_auto_reply); the token is stored in
// plain text in the Ranbi config file, and chat/MOTD/server info are sent to the
// configured third-party API. See the Ranbi AI settings page for the warnings.

#include "ai_client.h"

#include <base/dbg.h>
#include <base/io.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// task-96: default required.md template (usage of the new knowledge base). Other .md files
// are selected only by question containing the file name or by a description in this file.
static const char s_aRequiredKnowledgeTemplate[] =
	"# Ranbi AI 知识库（required.md）\n"
	"\n"
	"本文件是必读知识库：每次 AI 回复都会读取并注入（即使内容为空）。\n"
	"\n"
	"## 其他知识文件\n"
	"在 ranbi/knowledge/ 中放置 .md 文件；只有满足以下任一条件时才会读取：\n"
	"1. 玩家提问中包含文件名（不含 .md，不区分大小写）；\n"
	"2. 本文件中写了该文件的描述，且提问命中该描述。\n"
	"\n"
	"描述格式（每行一个）：\n"
	"- filename.md: 这个文件包含的知识描述（关键词/用途）\n"
	"或：\n"
	"filename.md: 描述\n"
	"\n"
	"示例：\n"
	"- hdf.md: HDF 相关功能与用法\n"
	"没有描述、提问也未包含文件名的 .md 文件不会被读取。\n";

// UTF-8-safe truncation to a maximum number of characters (system prompt defense).
static int TruncateUtf8Chars(const char *pSrc, char *pDst, int DstSize, int MaxChars)
{
	int OutLen = 0;
	int Chars = 0;
	const char *p = pSrc;
	while(*p && Chars < MaxChars)
	{
		const char *pNext = p;
		const int Ch = str_utf8_decode(&pNext);
		if(Ch == -1 || pNext <= p)
			break;
		const int CharLen = (int)(pNext - p);
		if(OutLen + CharLen >= DstSize)
			break;
		memcpy(pDst + OutLen, p, (size_t)CharLen);
		OutLen += CharLen;
		p = pNext;
		Chars++;
	}
	pDst[OutLen] = '\0';
	return OutLen;
}

CAiClient::CAiClient()
{
	OnReset();
}

void CAiClient::OnReset()
{
	for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
		m_aNextReplyTime[Dummy] = 0;
	if(m_pRequest)
		m_pRequest->Abort();
	m_pRequest = nullptr;
	m_ReplyDummy = 0;
	m_ReplyTeam = 0;
	m_KnowledgeHit = false;
	m_KnowledgeRequiredHit = false;
	m_KnowledgeOtherHitCount = 0;
	m_aPendingSpeaker[0] = '\0';
	m_aPendingText[0] = '\0';
	m_Context.clear();
	m_ReplyQueue.clear();
	m_aLastError[0] = '\0';
	m_IsRewriteRequest = false;
	m_RewriteCount = 0;
	m_aRewritePrevReply[0] = '\0';
}

void CAiClient::SetLastError(const char *pMessage)
{
	str_copy(m_aLastError, pMessage);
}

void CAiClient::OnInit()
{
	// task-96: only ranbi/knowledge/required.md is generated/read as the mandatory knowledge
	// base. Old files (e.g. ddnet_wiki.txt) are left untouched but no longer generated/read.
	Storage()->CreateFolder("ranbi", IStorage::TYPE_SAVE);
	Storage()->CreateFolder("ranbi/knowledge", IStorage::TYPE_SAVE);

	const char *pRequiredPath = "ranbi/knowledge/required.md";
	if(Storage()->FileExists(pRequiredPath, IStorage::TYPE_SAVE))
		return;
	IOHANDLE WriteHandle = Storage()->OpenFile(pRequiredPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!WriteHandle)
		return;
	const int TemplateLen = str_length(s_aRequiredKnowledgeTemplate);
	if(io_write(WriteHandle, s_aRequiredKnowledgeTemplate, TemplateLen) != (unsigned)TemplateLen)
		Storage()->RemoveFile(pRequiredPath, IStorage::TYPE_SAVE);
	io_close(WriteHandle);
}

// task-102 B4: JSON-escape into a dynamically sized string so long context/user/rewrite content
// cannot be silently truncated by fixed buffers. EscapeJson's worst case is 6x (control chars).
static std::string EscapeJsonString(const std::string &Text)
{
	std::vector<char> Buffer(Text.size() * 6 + 2);
	EscapeJson(Buffer.data(), (int)Buffer.size(), Text.c_str());
	return std::string(Buffer.data());
}

static bool IsWordChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// 任意位置出现名字（词边界，区分大小写）即触发
bool CAiClient::ParseMention(const char *pText, int &Dummy)
{
	for(int D = 0; D < NUM_DUMMIES; D++)
	{
		if(D == 1 && !Client()->DummyConnected())
			continue;
		const int LocalId = GameClient()->m_aLocalIds[D];
		if(LocalId < 0 || LocalId >= MAX_CLIENTS)
			continue;
		const char *pName = GameClient()->m_aClients[LocalId].m_aName;
		const int NameLen = str_length(pName);
		if(NameLen == 0)
			continue;

		const char *pFound = pText;
		while((pFound = str_find(pFound, pName)) != nullptr)
		{
			const bool LeftOk = pFound == pText || !IsWordChar(pFound[-1]);
			const bool RightOk = !IsWordChar(pFound[NameLen]);
			if(LeftOk && RightOk)
			{
				Dummy = D;
				return true;
			}
			pFound += NameLen;
		}
	}
	return false;
}

// 加权截断：汉字(U+4E00-U+9FFF)权重16，其他10，总权重上限10240（=1024*10）
void CAiClient::TruncateReply(char *pText, int Size)
{
	int Total = 0;
	const char *p = pText;
	while(*p)
	{
		const char *pNext = p;
		const int Ch = str_utf8_decode(&pNext);
		if(Ch == -1)
			break;
		Total += (Ch >= 0x4E00 && Ch <= 0x9FFF) ? 16 : 10;
		if(Total > 10240)
			break;
		p = pNext;
	}
	if(p - pText >= Size)
		p = pText + Size - 1;
	((char *)p)[0] = '\0';
}

static bool IsOnlyWhitespace(const std::string &Text)
{
	for(const char Ch : Text)
	{
		if(Ch != ' ' && Ch != '\t' && Ch != '\n' && Ch != '\r')
			return false;
	}
	return true;
}

// UTF-8 字符数（不含终止符）
static int Utf8CharCount(const char *pText)
{
	int Count = 0;
	for(const char *p = pText; *p;)
	{
		const char *pNext = p;
		const int Ch = str_utf8_decode(&pNext);
		if(Ch == -1)
			break;
		Count++;
		p = pNext;
	}
	return Count;
}

// 按句号（。或.）拆分句子，忽略空句；无句号的剩余部分也作为一句
void CAiClient::SplitSentences(const char *pText, std::vector<std::string> &Sentences)
{
	const char *pStart = pText;
	const char *p = pText;
	while(*p)
	{
		const char *pNext = p;
		const int Ch = str_utf8_decode(&pNext);
		if(Ch == -1)
			break;
		if(Ch == 0x3002 || Ch == '.')
		{
			std::string Sentence(pStart, pNext);
			if(!Sentence.empty() && !IsOnlyWhitespace(Sentence))
				Sentences.push_back(Sentence);
			pStart = pNext;
		}
		p = pNext;
	}
	if(pStart != p)
	{
		std::string Tail(pStart, p);
		// task-98 B3: whitespace-only tail must not be queued (suffix must not attach to it).
		if(!Tail.empty() && !IsOnlyWhitespace(Tail))
			Sentences.push_back(Tail);
	}
}

// task-100: split one overlong sentence into UTF-8 character chunks of at most MaxChars.
static void SplitUtf8Chunks(const std::string &Text, int MaxChars, std::vector<std::string> &Chunks)
{
	Chunks.clear();
	if(MaxChars < 1)
		MaxChars = 1;
	std::string Current;
	int Count = 0;
	const char *p = Text.c_str();
	while(*p)
	{
		const char *pNext = p;
		const int Ch = str_utf8_decode(&pNext);
		if(Ch == -1 || pNext <= p)
			break;
		if(Count >= MaxChars)
		{
			Chunks.push_back(Current);
			Current.clear();
			Count = 0;
		}
		Current.append(p, (size_t)(pNext - p));
		Count++;
		p = pNext;
	}
	if(!Current.empty())
		Chunks.push_back(Current);
}

// task-100: pack sentences greedily into at most 3 messages; each message contains at most
// rc_ai_max_sentence_length UTF-8 characters. Overlong single sentences are split. Extra
// chunks beyond the 3-message limit are dropped.
static void BuildReplyMessages(const std::vector<std::string> &Sentences, std::vector<std::string> &Messages)
{
	constexpr size_t MAX_MESSAGES = 3;
	int MaxChars = g_Config.m_RcAiMaxSentenceLength;
	if(MaxChars < 1)
		MaxChars = 1;
	Messages.clear();
	std::string Current;
	auto Flush = [&]() {
		if(!Current.empty() && Messages.size() < MAX_MESSAGES)
			Messages.push_back(Current);
		Current.clear();
	};
	for(const std::string &Sentence : Sentences)
	{
		if(Messages.size() >= MAX_MESSAGES)
			break;
		if(Sentence.empty() || IsOnlyWhitespace(Sentence))
			continue;
		const int SentenceChars = Utf8CharCount(Sentence.c_str());
		if(SentenceChars <= MaxChars)
		{
			if(Current.empty())
				Current = Sentence;
			else if(Utf8CharCount(Current.c_str()) + SentenceChars <= MaxChars)
				Current += Sentence;
			else
			{
				Flush();
				Current = Sentence;
			}
		}
		else
		{
			Flush();
			std::vector<std::string> Chunks;
			SplitUtf8Chunks(Sentence, MaxChars, Chunks);
			for(std::string &Chunk : Chunks)
			{
				if(Messages.size() >= MAX_MESSAGES)
					break;
				Messages.push_back(std::move(Chunk));
			}
		}
	}
	Flush();
}

// task-57: true when the reply needs a rewrite: any sentence exceeds the per-message char limit,
// or sentence-packed blocks exceed the 3-message limit.
static bool ReplyExceedsLimits(const std::vector<std::string> &Sentences, int MaxChars)
{
	if(MaxChars < 1)
		MaxChars = 1;
	int Blocks = 0;
	std::string Current;
	for(const std::string &Sentence : Sentences)
	{
		if(Sentence.empty() || IsOnlyWhitespace(Sentence))
			continue;
		const int SentenceChars = Utf8CharCount(Sentence.c_str());
		if(SentenceChars > MaxChars)
			return true;
		if(Current.empty())
			Current = Sentence;
		else if(Utf8CharCount(Current.c_str()) + SentenceChars <= MaxChars)
			Current += Sentence;
		else
		{
			Blocks++;
			Current = Sentence;
		}
	}
	if(!Current.empty())
		Blocks++;
	return Blocks > 3;
}

// task-96: knowledge list callback collects .md files only (required.md is handled separately).
static int KnowledgeListCallback(const char *pName, int IsDir, int Type, void *pUser)
{
	(void)Type;
	const int Len = str_length(pName);
	if(IsDir || Len < 4 || str_comp(pName + Len - 3, ".md") != 0)
		return 0;
	auto *pEntries = static_cast<std::set<std::string> *>(pUser);
	pEntries->emplace(pName);
	return 0;
}

static void BindAiLog(const char *pFormat, ...)
{
	if(!g_Config.m_Debug)
		return;
	char aBody[512];
	va_list Args;
	va_start(Args, pFormat);
	str_format_v(aBody, sizeof(aBody), pFormat, Args);
	va_end(Args);
	dbg_msg("ranbi_ai", "%s", aBody);
}

// task-96: selection helper (file name in question, or description keyword/fragment match).
static bool KnowledgeHit(const char *pQuestion, const char *pFileName, const char *pContent)
{
	char aName[128];
	str_copy(aName, pFileName, sizeof(aName));
	const char *pDot = strrchr(aName, '.');
	if(pDot)
		*(char *)pDot = '\0';
	if(str_find_nocase(pQuestion, aName))
		return true;

	// 提问中的字母数字单词（长度≥2）作为独立词出现在内容中即命中
	const char *p = pQuestion;
	while(*p)
	{
		if(IsWordChar(*p))
		{
			const char *pWord = p;
			while(IsWordChar(*p))
				++p;
			const int WordLen = (int)(p - pWord);
			if(WordLen >= 2 && WordLen < 64)
			{
				char aWord[64];
				str_copy(aWord, pWord, WordLen + 1);
				const char *pFound = pContent;
				while((pFound = str_find_nocase(pFound, aWord)) != nullptr)
				{
					const bool LeftOk = pFound == pContent || !IsWordChar(pFound[-1]);
					const bool RightOk = !IsWordChar(pFound[WordLen]);
					if(LeftOk && RightOk)
						return true;
					pFound += WordLen;
				}
			}
		}
		else
		{
			const char *pNext = p;
			if(str_utf8_decode(&pNext) == -1)
				break;
			p = pNext;
		}
	}

	// 提问任一连续4字符片段出现在内容中
	const char *p2 = pQuestion;
	while(*p2)
	{
		const char *pEnd = p2;
		int Chars = 0;
		for(; Chars < 4 && *pEnd; Chars++)
			str_utf8_decode(&pEnd);
		if(Chars < 4)
			break;
		char aFragment[20];
		str_copy(aFragment, p2, (int)(pEnd - p2) + 1);
		if(str_find(pContent, aFragment))
			return true;
		const char *pNext = p2;
		if(str_utf8_decode(&pNext) == -1)
			break;
		p2 = pNext;
	}
	return false;
}

// task-96: inject required.md every reply, then only .md files selected by the question
// (file name) or by their description in required.md. No full content scans of unselected files.
// Keeps the original 4KB single-file and caller-provided total buffer limits.
int CAiClient::LoadKnowledge(const char *pQuestion, char *aKbBuf, int KbBufSize)
{
	if(KbBufSize <= 0)
		return 0;
	aKbBuf[0] = '\0';
	int TotalLen = 0;
	m_KnowledgeHit = false;
	m_KnowledgeRequiredHit = false;
	m_KnowledgeOtherHitCount = 0;

	auto ReadLimited = [&](const char *pPath, int MaxBytes, bool AllowTruncate, std::string &Out) -> bool {
		Out.clear();
		IOHANDLE File = Storage()->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		const int64_t FileSize = io_length(File);
		if(FileSize < 0)
		{
			io_close(File);
			return false;
		}
		if(FileSize > MaxBytes && !AllowTruncate)
		{
			io_close(File);
			return false;
		}
		const int ReadSize = (int)std::min<int64_t>(FileSize, MaxBytes);
		Out.resize((size_t)ReadSize);
		unsigned Read = 0;
		if(ReadSize > 0)
			Read = io_read(File, Out.data(), (unsigned)ReadSize);
		io_close(File);
		Out.resize(Read);
		return true;
	};

	auto AppendKnowledge = [&](const char *pName, const std::string &Content) -> bool {
		if(TotalLen >= KbBufSize - 1)
			return false;
		const int HeaderLen = str_format(aKbBuf + TotalLen, KbBufSize - TotalLen, "\n[知识库: %s]\n", pName);
		if(HeaderLen < 0 || HeaderLen >= KbBufSize - TotalLen)
			return false;
		TotalLen += HeaderLen;
		const int CopyLen = std::min((int)Content.size(), KbBufSize - TotalLen - 1);
		if(CopyLen > 0)
		{
			memcpy(aKbBuf + TotalLen, Content.data(), (size_t)CopyLen);
			TotalLen += CopyLen;
			aKbBuf[TotalLen] = '\0';
		}
		return true;
	};

	// 1) required.md is mandatory every reply; inject even if empty.
	std::string RequiredContent;
	if(ReadLimited("ranbi/knowledge/required.md", 4096, true, RequiredContent))
	{
		if(AppendKnowledge("required.md", RequiredContent))
		{
			m_KnowledgeRequiredHit = true;
			m_KnowledgeHit = true;
		}
	}
	else
	{
		BindAiLog("[知识库] required.md 读取失败或不存在");
	}

	// 2) parse descriptions for other files from required.md: "[-] filename.md: description"
	std::vector<std::pair<std::string, std::string>> vDescriptions;
	{
		size_t Pos = 0;
		while(Pos < RequiredContent.size())
		{
			size_t End = RequiredContent.find('\n', Pos);
			if(End == std::string::npos)
				End = RequiredContent.size();
			std::string Line = RequiredContent.substr(Pos, End - Pos);
			Pos = End + 1;
			if(!Line.empty() && Line.back() == '\r')
				Line.pop_back();
			const size_t MdPos = Line.find(".md");
			if(MdPos == std::string::npos)
				continue;
			const size_t Colon = Line.find(':', MdPos + 3);
			if(Colon == std::string::npos)
				continue;
			size_t NameStart = MdPos;
			while(NameStart > 0 && Line[NameStart - 1] != ' ' && Line[NameStart - 1] != '\t' && Line[NameStart - 1] != '-' && Line[NameStart - 1] != '*' && Line[NameStart - 1] != '#')
				NameStart--;
			std::string Name = Line.substr(NameStart, MdPos + 3 - NameStart);
			std::string Description = Line.substr(Colon + 1);
			const size_t DescStart = Description.find_first_not_of(" \t");
			if(DescStart == std::string::npos)
				continue;
			Description.erase(0, DescStart);
			if(!Name.empty() && !Description.empty())
				vDescriptions.emplace_back(std::move(Name), std::move(Description));
		}
	}

	// 3) other .md files: only when the question contains the file name or matches its description.
	std::set<std::string> aFiles;
	Storage()->ListDirectory(IStorage::TYPE_SAVE, "ranbi/knowledge", KnowledgeListCallback, &aFiles);
	for(const std::string &FileName : aFiles)
	{
		if(str_comp_nocase(FileName.c_str(), "required.md") == 0)
			continue;
		if(TotalLen >= KbBufSize - 1)
			break;
		char aStem[128];
		str_copy(aStem, FileName.c_str(), sizeof(aStem));
		const char *pDot = strrchr(aStem, '.');
		if(pDot)
			*(char *)pDot = '\0';

		bool Selected = str_find_nocase(pQuestion, aStem) != nullptr;
		if(!Selected)
		{
			for(const auto &[Name, Description] : vDescriptions)
			{
				if(str_comp_nocase(Name.c_str(), FileName.c_str()) != 0)
					continue;
				Selected = KnowledgeHit(pQuestion, FileName.c_str(), Description.c_str());
				break;
			}
		}
		if(!Selected)
			continue;

		char aPath[512];
		str_format(aPath, sizeof(aPath), "ranbi/knowledge/%s", FileName.c_str());
		std::string Content;
		if(!ReadLimited(aPath, 4096, false, Content))
		{
			BindAiLog("[知识库] 跳过（读取失败或超过4KB）: %s", FileName.c_str());
			continue;
		}
		if(!AppendKnowledge(FileName.c_str(), Content))
			break;
		m_KnowledgeOtherHitCount++;
		m_KnowledgeHit = true;
		BindAiLog("[知识库] 按需读取: %s", FileName.c_str());
	}
	return TotalLen;
}

// ranbi: send a chat message as the given dummy, supporting the 0.7 (sixup) protocol.
void CAiClient::SendChatMessage(int Dummy, int Team, const char *pMessage)
{
	if(Client()->IsSixup())
	{
		protocol7::CNetMsg_Cl_Say Msg7;
		Msg7.m_Mode = Team ? protocol7::CHAT_TEAM : protocol7::CHAT_ALL;
		Msg7.m_Target = -1;
		Msg7.m_pMessage = pMessage;
		Client()->SendPackMsg(Dummy, &Msg7, MSGFLAG_VITAL, true);
		return;
	}

	CNetMsg_Cl_Say Msg;
	Msg.m_Team = Team;
	Msg.m_pMessage = pMessage;
	Client()->SendPackMsg(Dummy, &Msg, MSGFLAG_VITAL);
}

void CAiClient::SendRequest(const char *pText, const char *pSpeaker, int Dummy, int Team, const char *pPrevReply)
{
	// rc_ai_model / rc_ai_base_url / rc_ai_token
	char aModel[512];
	EscapeJson(aModel, sizeof(aModel), g_Config.m_RcAiModel);

	// rc_ai_system_prompt (defensive UTF-8 truncation to at most 1024 characters; the config
	// buffer was expanded from 512 to 4096 bytes for up to 1024 Chinese characters).
	// task-102 B1: when the configured prompt is empty, the fallback must use the same rules as
	// the normal system prompt (max 3 messages, each max N chars, period-terminated sentences).
	char aSystem[9000];
	char aPrompt[4096];
	char aDefaultPrompt[512];
	str_format(aDefaultPrompt, sizeof(aDefaultPrompt),
		"你是DDNet这款游戏的玩家，你需要回复其他玩家跟你的谈话，且谈话可能为空，可能仅是打招呼。每次回复最多分成3条消息，每条消息不超过%d个字符（UTF-8字符），每句话以句号（。或.）结尾，过长的回复会被截断。",
		std::max(1, g_Config.m_RcAiMaxSentenceLength));
	const char *pPrompt = g_Config.m_RcAiSystemPrompt[0] != '\0' ? g_Config.m_RcAiSystemPrompt : aDefaultPrompt;
	TruncateUtf8Chars(pPrompt, aPrompt, sizeof(aPrompt), 1024);
	str_copy(aSystem, aPrompt, sizeof(aSystem));
	int SystemLen = str_length(aSystem);

	// 注入身份信息：告知自己的名字与消息格式，避免回复时用错称呼
	char aIdentity[768];
	int IdentityLen = 0;
	for(int D = 0; D < NUM_DUMMIES; D++)
	{
		if(D == 1 && !Client()->DummyConnected())
			continue;
		const int LocalId = GameClient()->m_aLocalIds[D];
		if(LocalId < 0 || LocalId >= MAX_CLIENTS)
			continue;
		const char *pName = GameClient()->m_aClients[LocalId].m_aName;
		if(pName[0] == '\0')
			continue;
		const int Len = str_format(aIdentity + IdentityLen, sizeof(aIdentity) - IdentityLen,
			"\n你的游戏名字是\"%s\"%s", pName, D == Dummy ? "（本次对话以该名字的身份回复）" : "（你的分身名字）");
		if(Len < 0 || Len >= (int)sizeof(aIdentity) - IdentityLen)
			break;
		IdentityLen += Len;
	}
	const int RuleLen = str_format(aIdentity + IdentityLen, (int)sizeof(aIdentity) - IdentityLen,
		"%s", "\n其他玩家提到你的名字就是在跟你说话。不知道的知识或名词名称先查询知识库，如果知识库没有则回答不知道。");
	if(RuleLen >= 0 && RuleLen < (int)sizeof(aIdentity) - IdentityLen)
		IdentityLen += RuleLen;
	// rc_ai_max_sentence_length：现在表示“每条消息最大字符数”，每次最多发送3条消息
	const int SentenceRuleLen = str_format(aIdentity + IdentityLen, (int)sizeof(aIdentity) - IdentityLen,
		"\n每次回复最多分成3条消息，每条消息不超过%d个字符（UTF-8字符），每句话以句号（。或.）结尾。", g_Config.m_RcAiMaxSentenceLength);
	if(SentenceRuleLen >= 0 && SentenceRuleLen < (int)sizeof(aIdentity) - IdentityLen)
		IdentityLen += SentenceRuleLen;
	// 语言跟随：对方用什么语言说话就用什么语言回复，对方可能在一条消息中切换多种语言
	const int LangRuleLen = str_format(aIdentity + IdentityLen, (int)sizeof(aIdentity) - IdentityLen,
		"%s", "\n对方用什么语言跟你说话，你就用什么语言回复。对方可能会在一条消息中切换多种语言。");
	if(LangRuleLen >= 0 && LangRuleLen < (int)sizeof(aIdentity) - IdentityLen)
		IdentityLen += LangRuleLen;
	if(IdentityLen > 0 && IdentityLen < (int)sizeof(aSystem) - SystemLen - 1)
	{
		str_copy(aSystem + SystemLen, aIdentity, sizeof(aSystem) - SystemLen);
		SystemLen += IdentityLen;
	}

	m_KnowledgeHit = false;
	SystemLen += LoadKnowledge(pText, aSystem + SystemLen, sizeof(aSystem) - SystemLen);

	// 追加当前服务器信息到系统提示词末尾
	const CServerInfo &ServerInfo = Client()->ServerInfo();
	if(ServerInfo.m_aName[0] != '\0')
	{
		const int InfoLen = str_format(aSystem + SystemLen, sizeof(aSystem) - SystemLen,
			"\n当前服务器信息：服务器名称%s，地图%s，游戏模式%s，玩家%d/%d。",
			ServerInfo.m_aName, ServerInfo.m_aMap, ServerInfo.m_aGameType, ServerInfo.m_NumPlayers, ServerInfo.m_MaxPlayers);
		if(InfoLen >= 0 && InfoLen < (int)sizeof(aSystem) - SystemLen)
			SystemLen += InfoLen;
	}

	// 追加服务器公告到系统提示词末尾
	const char *pMotd = GameClient()->m_Motd.ServerMotd();
	if(pMotd[0] != '\0')
	{
		const int MotdLen = str_format(aSystem + SystemLen, sizeof(aSystem) - SystemLen,
			"\n服务器公告：%s", pMotd);
		if(MotdLen >= 0 && MotdLen < (int)sizeof(aSystem) - SystemLen)
			SystemLen += MotdLen;
	}

	// task-102 B4: build the JSON body with explicit capacity checks. Optional context messages
	// are skipped when the body would grow too large; required user/rewrite messages and the
	// closing "]} "must never be truncated (otherwise we abort before sending any HTTP request).
	// A generous hard cap keeps memory bounded even with large context/replies.
	const int UrlLen = str_length(g_Config.m_RcAiBaseUrl);
	char aUrl[512];
	if(UrlLen > 0 && g_Config.m_RcAiBaseUrl[UrlLen - 1] == '/')
		str_format(aUrl, sizeof(aUrl), "%schat/completions", g_Config.m_RcAiBaseUrl);
	else
		str_format(aUrl, sizeof(aUrl), "%s/chat/completions", g_Config.m_RcAiBaseUrl);

	char aToken[512];
	str_format(aToken, sizeof(aToken), "Bearer %s", g_Config.m_RcAiToken);

	constexpr size_t MAX_BODY_BYTES = 256 * 1024;
	std::string aBody;
	aBody.reserve(8192);
	aBody += "{\"model\":\"";
	aBody += EscapeJsonString(aModel);
	aBody += "\",\"temperature\":";
	{
		char aTemperature[32];
		const int TemperatureLen = str_format(aTemperature, sizeof(aTemperature), "%.1f", g_Config.m_RcAiTemperature / 10.0f);
		if(TemperatureLen < 0 || TemperatureLen >= (int)sizeof(aTemperature))
		{
			SetLastError("AI request temperature formatting failed");
			return;
		}
		aBody += aTemperature;
	}
	aBody += ",\"messages\":[{\"role\":\"system\",\"content\":\"";
	aBody += EscapeJsonString(aSystem);
	aBody += "\"}";

	// task-102 B4: build the required tail first (current user + rewrite assistant/feedback +
	// closing "]}"). Optional context messages are only appended when they fit while leaving
	// room for the tail, so a large context is truncated rather than producing broken JSON.
	std::string aTail;
	{
		char aCurMsg[300];
		const int CurLen = str_format(aCurMsg, sizeof(aCurMsg), "%s: %s", pSpeaker, pText);
		if(CurLen < 0 || CurLen >= (int)sizeof(aCurMsg))
		{
			SetLastError("AI request current message formatting failed");
			return;
		}
		aTail += ",{\"role\":\"user\",\"content\":\"";
		aTail += EscapeJsonString(aCurMsg);
		aTail += "\"}";
	}
	if(pPrevReply && pPrevReply[0] != '\0')
	{
		// task-57: feedback rewrite request. Only HTTP; no chat, no m_Context pollution.
		aTail += ",{\"role\":\"assistant\",\"content\":\"";
		aTail += EscapeJsonString(pPrevReply);
		aTail += "\"}";
		const int MaxChars = std::max(1, g_Config.m_RcAiMaxSentenceLength);
		char aFeedback[1024];
		const int FeedbackLen = str_format(aFeedback, sizeof(aFeedback),
			"你上一轮的回复超过了限制：最多只能发送3条消息，每条消息最多%d个字符（UTF-8字符）。请重新生成回复，最多3条消息，每条不超过%d个字符，不要解释。",
			MaxChars, MaxChars);
		if(FeedbackLen < 0 || FeedbackLen >= (int)sizeof(aFeedback))
		{
			SetLastError("AI rewrite feedback formatting failed");
			return;
		}
		aTail += ",{\"role\":\"user\",\"content\":\"";
		aTail += EscapeJsonString(aFeedback);
		aTail += "\"}";
	}
	aTail += "]}";
	if(aTail.size() + 1024 > MAX_BODY_BYTES)
	{
		SetLastError("AI request required messages are too large");
		dbg_msg("ranbi_ai", "[AI失败] 请求必需内容超过上限，已取消发送");
		return;
	}

	auto TryAppendContext = [&](const std::string &Piece) {
		if(aBody.size() + Piece.size() + aTail.size() > MAX_BODY_BYTES)
			return;
		aBody += Piece;
	};

	for(const CContextEntry &Entry : m_Context)
	{
		if(Entry.m_IsReply)
		{
			std::string Piece = ",{\"role\":\"assistant\",\"content\":\"";
			Piece += EscapeJsonString(Entry.m_aText);
			Piece += "\"}";
			TryAppendContext(Piece);
		}
		else
		{
			char aMsg[2304];
			const int MsgLen = str_format(aMsg, sizeof(aMsg), "%s: %s", Entry.m_aSpeaker, Entry.m_aText);
			if(MsgLen < 0 || MsgLen >= (int)sizeof(aMsg))
				continue; // oversized context entry: skip, keep JSON valid
			std::string Piece = ",{\"role\":\"user\",\"content\":\"";
			Piece += EscapeJsonString(aMsg);
			Piece += "\"}";
			TryAppendContext(Piece);
		}
	}

	if(aBody.size() + aTail.size() > MAX_BODY_BYTES)
	{
		SetLastError("AI request is too large (JSON body limit)");
		dbg_msg("ranbi_ai", "[AI失败] 请求体超过上限，已取消发送");
		return;
	}
	aBody += aTail;

	m_aLastError[0] = '\0';
	m_pRequest = HttpPostJson(aUrl, aBody.c_str());
	m_pRequest->HeaderString("Authorization", aToken);
	m_pRequest->Timeout(CTimeout{10000, 60000, 500, 5});
	m_pRequest->FailOnErrorStatus(false);
	m_pRequest->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pRequest);
	m_ReplyDummy = Dummy;
	m_ReplyTeam = Team;
	m_aNextReplyTime[Dummy] = time_get() + time_freq() * g_Config.m_RcAiReplyInterval;
}

void CAiClient::FinalizeReply(const char *pReply)
{
	char aReply[2048];
	if(pReply)
		str_copy(aReply, pReply, sizeof(aReply));
	else
		aReply[0] = '\0';
	TruncateReply(aReply, sizeof(aReply));

	if(g_Config.m_RcAiContextCount > 0)
	{
		CContextEntry UserEntry;
		str_copy(UserEntry.m_aSpeaker, m_aPendingSpeaker, sizeof(UserEntry.m_aSpeaker));
		str_copy(UserEntry.m_aText, m_aPendingText, sizeof(UserEntry.m_aText));
		UserEntry.m_IsReply = false;
		m_Context.push_back(UserEntry);

		CContextEntry ReplyEntry;
		ReplyEntry.m_aSpeaker[0] = '\0';
		// task-100: context stores the full final reply, never the suffix (suffix is outgoing only).
		str_copy(ReplyEntry.m_aText, aReply, sizeof(ReplyEntry.m_aText));
		ReplyEntry.m_IsReply = true;
		m_Context.push_back(ReplyEntry);

		while((int)m_Context.size() > g_Config.m_RcAiContextCount * 2)
			m_Context.erase(m_Context.begin());
	}

	m_aLastError[0] = '\0';
	dbg_msg("ranbi_ai", "[AI] 说话人: %s | 回复给: %s | 回复: %s | 知识库: %s (required=%d others=%d) | rewrite=%d | tokens=0",
		m_aPendingSpeaker, m_ReplyDummy == 0 ? "本体" : "分身", aReply, m_KnowledgeHit ? "是" : "否",
		m_KnowledgeRequiredHit ? 1 : 0, m_KnowledgeOtherHitCount, m_RewriteCount);

	// ranbi: never send a reply if auto reply was disabled while the request was in flight
	if(!g_Config.m_RcAiAutoReply)
	{
		dbg_msg("ranbi_ai", "[AI] auto reply disabled, dropping reply");
		m_IsRewriteRequest = false;
		m_RewriteCount = 0;
		m_aRewritePrevReply[0] = '\0';
		return;
	}

	const int MaxChars = std::max(1, g_Config.m_RcAiMaxSentenceLength);
	std::vector<std::string> Sentences;
	SplitSentences(aReply, Sentences);
	if(ReplyExceedsLimits(Sentences, MaxChars))
		dbg_msg("ranbi_ai", "[AI] 重写后仍超长已截断");

	// task-100: at most 3 outgoing messages, each AI content <= MaxChars UTF-8 characters.
	m_ReplyQueue.clear();
	BuildReplyMessages(Sentences, m_ReplyQueue);
	m_ReplyQueue.erase(std::remove_if(m_ReplyQueue.begin(), m_ReplyQueue.end(), [](const std::string &Sentence) {
		return IsOnlyWhitespace(Sentence);
	}),
		m_ReplyQueue.end());
	// task-96/task-57: suffix is appended to EVERY outgoing message, never to m_Context.
	if(g_Config.m_RcAiReplySuffixEnabled && g_Config.m_RcAiReplySuffix[0] != '\0')
	{
		for(std::string &Message : m_ReplyQueue)
			Message += g_Config.m_RcAiReplySuffix;
	}
	if(!m_ReplyQueue.empty())
	{
		char aMessage[4096];
		str_format(aMessage, sizeof(aMessage), "%s: %s", m_aPendingSpeaker, m_ReplyQueue[0].c_str());
		SendChatMessage(m_ReplyDummy, m_ReplyTeam, aMessage);
		m_ReplyQueue.erase(m_ReplyQueue.begin());
		m_aNextReplyTime[m_ReplyDummy] = time_get() + time_freq() * g_Config.m_RcAiReplyInterval;
	}

	m_IsRewriteRequest = false;
	m_RewriteCount = 0;
	m_aRewritePrevReply[0] = '\0';
}

void CAiClient::HandleResponse()
{
	// task-57: if a feedback-rewrite request fails (HTTP/JSON), fall back to the previous reply
	// instead of losing the answer; this is never sent as a separate chat message.
	auto HandleRewriteFailure = [&]() -> bool {
		if(!m_IsRewriteRequest)
			return false;
		dbg_msg("ranbi_ai", "[AI] 重写请求失败，使用上一轮回复");
		char aPrevReply[2048];
		str_copy(aPrevReply, m_aRewritePrevReply, sizeof(aPrevReply));
		m_pRequest = nullptr;
		FinalizeReply(aPrevReply);
		return true;
	};

	char aError[256];
	if(m_pRequest->State() != EHttpState::DONE)
	{
		str_format(aError, sizeof(aError), "Request failed (state %d)", (int)m_pRequest->State());
		SetLastError(aError);
		dbg_msg("ranbi_ai", "[AI失败] 请求异常 state=%d", (int)m_pRequest->State());
		m_pRequest = nullptr;
		if(HandleRewriteFailure())
			return;
		return;
	}
	if(m_pRequest->StatusCode() < 200 || m_pRequest->StatusCode() >= 300)
	{
		str_format(aError, sizeof(aError), "HTTP status %d", m_pRequest->StatusCode());
		SetLastError(aError);
		dbg_msg("ranbi_ai", "[AI失败] HTTP状态码 %d", m_pRequest->StatusCode());
		m_pRequest = nullptr;
		if(HandleRewriteFailure())
			return;
		return;
	}
	json_value *pObj = m_pRequest->ResultJson();
	if(!pObj)
	{
		SetLastError("Response is not valid JSON");
		dbg_msg("ranbi_ai", "[AI失败] 响应JSON解析失败");
		m_pRequest = nullptr;
		if(HandleRewriteFailure())
			return;
		return;
	}
	const json_value *pChoices = json_object_get(pObj, "choices");
	const json_value *pFirst = json_array_length(pChoices) > 0 ? json_array_get(pChoices, 0) : nullptr;
	const json_value *pMessage = pFirst ? json_object_get(pFirst, "message") : nullptr;
	const json_value *pContent = pMessage ? json_object_get(pMessage, "content") : nullptr;
	const char *pReply = pContent && pContent->type == json_string ? json_string_get(pContent) : nullptr;
	if(!pReply)
	{
		SetLastError("Response has no message content");
		dbg_msg("ranbi_ai", "[AI失败] 响应中无 content 字段");
		json_value_free(pObj);
		m_pRequest = nullptr;
		if(HandleRewriteFailure())
			return;
		return;
	}

	char aReply[2048];
	str_copy(aReply, pReply, sizeof(aReply));
	TruncateReply(aReply, sizeof(aReply));

	// task-100: detect replies that exceed "at most 3 messages / each max chars".
	const int MaxChars = std::max(1, g_Config.m_RcAiMaxSentenceLength);
	std::vector<std::string> Sentences;
	SplitSentences(aReply, Sentences);
	const bool ExceedsLimits = ReplyExceedsLimits(Sentences, MaxChars);

	// auto reply may have been disabled while the request was in flight: never send/continue.
	if(!g_Config.m_RcAiAutoReply)
	{
		dbg_msg("ranbi_ai", "[AI] auto reply disabled, dropping reply");
		m_IsRewriteRequest = false;
		m_RewriteCount = 0;
		m_aRewritePrevReply[0] = '\0';
		json_value_free(pObj);
		m_pRequest = nullptr;
		return;
	}

	// task-57: bounded feedback rewrite loop (max 2 rewrites); HTTP only, never chat.
	if(ExceedsLimits && m_RewriteCount < 2)
	{
		str_copy(m_aRewritePrevReply, aReply, sizeof(m_aRewritePrevReply));
		m_RewriteCount++;
		m_IsRewriteRequest = true;
		dbg_msg("ranbi_ai", "[AI] 回复超限，发送第%d次重写请求", m_RewriteCount);
		json_value_free(pObj);
		m_pRequest = nullptr;
		SendRequest(m_aPendingText, m_aPendingSpeaker, m_ReplyDummy, m_ReplyTeam, m_aRewritePrevReply);
		return;
	}
	if(m_IsRewriteRequest)
		dbg_msg("ranbi_ai", "[AI] 重写%s", ExceedsLimits ? "后仍超长" : "成功");

	FinalizeReply(aReply);
	json_value_free(pObj);
	m_pRequest = nullptr;
}

void CAiClient::OnMessage(int MsgType, void *pRawMsg)
{
	// rc_ai_auto_reply
	if(MsgType != NETMSGTYPE_SV_CHAT)
		return;
	CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
	if(!g_Config.m_RcAiAutoReply || pMsg->m_ClientId < 0)
		return;
	if(pMsg->m_ClientId == GameClient()->m_aLocalIds[0] || (Client()->DummyConnected() && pMsg->m_ClientId == GameClient()->m_aLocalIds[1]))
		return; // 自己的消息不触发

	int Dummy;
	if(!ParseMention(pMsg->m_pMessage, Dummy))
		return;
	if(time_get() < m_aNextReplyTime[Dummy])
		return;
	if(m_pRequest)
		return;
	if(g_Config.m_RcAiToken[0] == '\0' || g_Config.m_RcAiModel[0] == '\0')
	{
		SetLastError("AI token or model is empty");
		return;
	}

	str_copy(m_aPendingSpeaker, GameClient()->m_aClients[pMsg->m_ClientId].m_aName, sizeof(m_aPendingSpeaker));
	str_copy(m_aPendingText, pMsg->m_pMessage, sizeof(m_aPendingText));
	m_IsRewriteRequest = false;
	m_RewriteCount = 0;
	m_aRewritePrevReply[0] = '\0';
	SendRequest(m_aPendingText, m_aPendingSpeaker, Dummy, pMsg->m_Team < 0 ? 0 : pMsg->m_Team);
}

void CAiClient::OnUpdate()
{
	// ranbi: do not send queued sentences after auto reply has been disabled
	if(!g_Config.m_RcAiAutoReply)
		m_ReplyQueue.clear();

	// rc_ai_max_sentence_length：按发送间隔分批发送队列中的句子
	if(g_Config.m_RcAiAutoReply && !m_ReplyQueue.empty() && time_get() >= m_aNextReplyTime[m_ReplyDummy])
	{
		char aMessage[4096];
		str_format(aMessage, sizeof(aMessage), "%s: %s", m_aPendingSpeaker, m_ReplyQueue[0].c_str());
		SendChatMessage(m_ReplyDummy, m_ReplyTeam, aMessage);
		m_ReplyQueue.erase(m_ReplyQueue.begin());
		m_aNextReplyTime[m_ReplyDummy] = time_get() + time_freq() * g_Config.m_RcAiReplyInterval;
	}
	if(!m_pRequest)
		return;
	if(!m_pRequest->Done())
		return;
	HandleResponse();
}
