/* ranbi: Bind Manager (按键管理) component for IntegratedClient. See bind_manager.h. */
#include "bind_manager.h"

#include <base/fs.h>
#include <base/io.h>
#include <base/log.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/components/binds.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <zlib.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <unordered_set>

#if defined(CONF_FAMILY_WINDOWS)
// NOGDI is defined for game-client targets; commdlg.h/prsht.h needs GDI types.
#undef NOGDI
// clang-format off
#include <windows.h>
#include <commdlg.h>
// clang-format on
#endif

namespace
{
	constexpr int BIND_MANAGER_NAME_LENGTH = 64;
	constexpr int BIND_MANAGER_CMD_LENGTH = 512;
	constexpr int BIND_MANAGER_PATH_LENGTH = 1024;
	// task-87 (verifier B1): hard limits checked before any allocation/inflate.
	constexpr size_t ZIP_MAX_ARCHIVE_BYTES = 256ull * 1024 * 1024;
	constexpr size_t ZIP_MAX_SINGLE_UNCOMPRESSED = 64ull * 1024 * 1024;
	constexpr size_t ZIP_MAX_TOTAL_UNCOMPRESSED = 256ull * 1024 * 1024;
	constexpr size_t ZIP_MAX_FILES = 4096;
	constexpr size_t ZIP_MAX_NAME_LENGTH = 1024;
	constexpr uint32_t ZIP_LOCAL_SIG = 0x04034b50;
	constexpr uint32_t ZIP_CENTRAL_SIG = 0x02014b50;
	constexpr uint32_t ZIP_EOCD_SIG = 0x06054b50;

	void BindManagerLog(const char *pFormat, ...)
	{
		char aBody[512];
		va_list Args;
		va_start(Args, pFormat);
		str_format_v(aBody, sizeof(aBody), pFormat, Args);
		va_end(Args);
		log_info("ranbi-bind-manager", "%s", aBody);
	}

	std::string EscapeCfgString(const std::string &Text)
	{
		std::string Out;
		Out.reserve(Text.size() + 8);
		for(const char Ch : Text)
		{
			switch(Ch)
			{
			case '\\': Out += "\\\\"; break;
			case '"': Out += "\\\""; break;
			case '\n': Out += "\\n"; break;
			case '\r': Out += "\\r"; break;
			case '\t': Out += "\\t"; break;
			default: Out += Ch; break;
			}
		}
		return Out;
	}

	void AppendQuoted(std::string &Out, const std::string &Text)
	{
		Out += '"';
		Out += EscapeCfgString(Text);
		Out += '"';
	}

	// Split a manager.txt line into tokens, honoring double quoted/escaped strings.
	bool TokenizeCfgLine(const std::string &Line, std::vector<std::string> &Tokens)
	{
		size_t Pos = 0;
		Tokens.clear();
		while(Pos < Line.size())
		{
			while(Pos < Line.size() && (Line[Pos] == ' ' || Line[Pos] == '\t'))
				Pos++;
			if(Pos >= Line.size())
				break;
			std::string Token;
			if(Line[Pos] == '"')
			{
				Pos++;
				bool Closed = false;
				while(Pos < Line.size())
				{
					const char Ch = Line[Pos++];
					if(Ch == '"')
					{
						Closed = true;
						break;
					}
					if(Ch == '\\' && Pos < Line.size())
					{
						const char Esc = Line[Pos++];
						switch(Esc)
						{
						case 'n': Token += '\n'; break;
						case 'r': Token += '\r'; break;
						case 't': Token += '\t'; break;
						case '\\': Token += '\\'; break;
						case '"': Token += '"'; break;
						default:
							Token += '\\';
							Token += Esc;
							break;
						}
						continue;
					}
					Token += Ch;
				}
				if(!Closed)
					return false;
			}
			else
			{
				while(Pos < Line.size() && Line[Pos] != ' ' && Line[Pos] != '\t')
					Token += Line[Pos++];
			}
			Tokens.push_back(Token);
		}
		return true;
	}

	std::string TrimSpaces(const std::string &Text)
	{
		size_t Start = 0;
		size_t End = Text.size();
		while(Start < End && (Text[Start] == ' ' || Text[Start] == '\t' || Text[Start] == '\r' || Text[Start] == '\n'))
			Start++;
		while(End > Start && (Text[End - 1] == ' ' || Text[End - 1] == '\t' || Text[End - 1] == '\r' || Text[End - 1] == '\n'))
			End--;
		return Text.substr(Start, End - Start);
	}

	unsigned int ParseManagerColor(const std::string &Text, unsigned int Default)
	{
		// task-93: manager.txt colors are packed HSLA values (decimal, same as config vars /
		// cl_message_client_color). Missing/invalid values fall back to the default.
		if(Text.empty())
			return Default;
		char *pEnd = nullptr;
		const unsigned long Value = strtoul(Text.c_str(), &pEnd, 10);
		if(!pEnd || *pEnd != '\0' || Value > 0xFFFFFFFFul)
			return Default;
		return (unsigned int)Value;
	}

	uint16_t ReadLE16(const unsigned char *pData)
	{
		return (uint16_t)(pData[0] | (pData[1] << 8));
	}

	uint32_t ReadLE32(const unsigned char *pData)
	{
		return (uint32_t)(pData[0] | (pData[1] << 8) | (pData[2] << 16) | ((uint32_t)pData[3] << 24));
	}

	void WriteLE16(std::vector<unsigned char> &Out, uint16_t Value)
	{
		Out.push_back((unsigned char)(Value & 0xff));
		Out.push_back((unsigned char)((Value >> 8) & 0xff));
	}

	void WriteLE32(std::vector<unsigned char> &Out, uint32_t Value)
	{
		Out.push_back((unsigned char)(Value & 0xff));
		Out.push_back((unsigned char)((Value >> 8) & 0xff));
		Out.push_back((unsigned char)((Value >> 16) & 0xff));
		Out.push_back((unsigned char)((Value >> 24) & 0xff));
	}

	bool ZipDeflate(const unsigned char *pData, size_t Size, std::vector<unsigned char> &Out, std::string &Error)
	{
		z_stream Stream{};
		if(deflateInit2(&Stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
		{
			Error = "zlib deflateInit2 failed";
			return false;
		}
		Out.resize(compressBound((uLong)Size) + 64);
		Stream.next_in = const_cast<Bytef *>(pData);
		Stream.avail_in = (uInt)Size;
		Stream.next_out = Out.data();
		Stream.avail_out = (uInt)Out.size();
		const int Result = deflate(&Stream, Z_FINISH);
		if(Result != Z_STREAM_END)
		{
			deflateEnd(&Stream);
			Error = "zlib deflate failed";
			return false;
		}
		Out.resize(Stream.total_out);
		deflateEnd(&Stream);
		return true;
	}

	// task-95: split a console line into top-level commands on semicolons that are outside
	// double quotes. Backslash escapes the next character; '#' outside quotes starts a comment
	// (rest of the line ignored). Unterminated quotes/escapes make the line non-executable.
	bool SplitCommandSegments(const std::string &Line, std::vector<std::string> &Segments)
	{
		Segments.clear();
		bool InQuotes = false;
		bool Escape = false;
		std::string Current;
		for(const char Ch : Line)
		{
			if(Escape)
			{
				Current += Ch;
				Escape = false;
				continue;
			}
			if(Ch == '\\')
			{
				Current += Ch;
				Escape = true;
				continue;
			}
			if(Ch == '"')
			{
				InQuotes = !InQuotes;
				Current += Ch;
				continue;
			}
			if(!InQuotes && Ch == '#')
				break; // comment outside quotes: ignore the remainder of the line
			if(!InQuotes && Ch == ';')
			{
				Segments.push_back(Current);
				Current.clear();
				continue;
			}
			Current += Ch;
		}
		Segments.push_back(Current);
		return !InQuotes && !Escape;
	}

	bool ZipInflate(const unsigned char *pData, size_t Size, size_t ExpectedSize, std::vector<unsigned char> &Out, std::string &Error)
	{
		// task-91: a legal 0-byte file (our export creates empty init.cfg/bind.cfg/step files)
		// must be accepted even when another zip tool deflates it (empty deflate stream, csize 2).
		// Never call inflate with a null output buffer/avail_out=0 (Z_BUF_ERROR); validate the
		// empty stream with a one-byte dummy output buffer instead.
		if(ExpectedSize == 0)
		{
			Out.clear();
			if(Size == 0)
				return true; // stored-style empty entry (also tolerated for method 8)
			z_stream EmptyStream{};
			if(inflateInit2(&EmptyStream, -MAX_WBITS) != Z_OK)
			{
				Error = "zlib inflateInit2 failed";
				return false;
			}
			unsigned char Dummy = 0;
			EmptyStream.next_in = const_cast<Bytef *>(pData);
			EmptyStream.avail_in = (uInt)Size;
			EmptyStream.next_out = &Dummy;
			EmptyStream.avail_out = 1;
			const int EmptyResult = inflate(&EmptyStream, Z_FINISH);
			const bool EmptyOk = EmptyResult == Z_STREAM_END && EmptyStream.total_out == 0 && EmptyStream.avail_in == 0;
			inflateEnd(&EmptyStream);
			if(!EmptyOk)
			{
				Error = "zlib inflate failed (empty entry)";
				return false;
			}
			return true;
		}

		z_stream Stream{};
		if(inflateInit2(&Stream, -MAX_WBITS) != Z_OK)
		{
			Error = "zlib inflateInit2 failed";
			return false;
		}
		Out.assign(ExpectedSize, 0);
		Stream.next_in = const_cast<Bytef *>(pData);
		Stream.avail_in = (uInt)Size;
		Stream.next_out = Out.data();
		Stream.avail_out = (uInt)Out.size();
		const int Result = inflate(&Stream, Z_FINISH);
		if(Result != Z_STREAM_END && Result != Z_OK)
		{
			inflateEnd(&Stream);
			Error = "zlib inflate failed";
			return false;
		}
		if(Stream.total_out != ExpectedSize)
		{
			inflateEnd(&Stream);
			Error = "zip entry size mismatch";
			return false;
		}
		inflateEnd(&Stream);
		return true;
	}

	struct SDirListContext
	{
		std::vector<std::string> m_vNames;
		std::vector<int> m_vIsDir;
	};

	int ListDirCallback(const char *pName, int IsDir, int DirType, void *pUser)
	{
		// task-89: directory listings contain "." and ".."; they must never become entries
		// for recursion/deletion/export (recursing into them caused the Save filesystem errors).
		if(!pName || !pName[0] || str_comp(pName, ".") == 0 || str_comp(pName, "..") == 0)
			return 0;
		auto *pContext = static_cast<SDirListContext *>(pUser);
		pContext->m_vNames.emplace_back(pName);
		pContext->m_vIsDir.push_back(IsDir);
		return 0;
	}

	void ListDirectory(IStorage *pStorage, int Type, const std::string &Path, SDirListContext &Context)
	{
		pStorage->ListDirectory(Type, Path.c_str(), ListDirCallback, &Context);
	}

	int ListConfigDirsCallback(const char *pName, int IsDir, int DirType, void *pUser)
	{
		if(!IsDir || !pName || pName[0] == '\0')
			return 0;
		// task-86: skip "."/".." and hidden dot-directories.
		if(pName[0] == '.')
			return 0;
		static_cast<std::vector<std::string> *>(pUser)->emplace_back(pName);
		return 0;
	}

#if defined(CONF_FAMILY_WINDOWS)
	bool Utf8ToWide(const char *pUtf8, wchar_t *pOut, int OutSize)
	{
		if(!pUtf8 || !pOut || OutSize <= 0)
			return false;
		const int Result = MultiByteToWideChar(CP_UTF8, 0, pUtf8, -1, pOut, OutSize);
		return Result > 0;
	}

	bool WideToUtf8(const wchar_t *pWide, char *pOut, int OutSize)
	{
		if(!pWide || !pOut || OutSize <= 0)
			return false;
		const int Result = WideCharToMultiByte(CP_UTF8, 0, pWide, -1, pOut, OutSize, nullptr, nullptr);
		return Result > 0;
	}
#endif
} // namespace

void CBindManager::SetLastError(const char *pFormat, ...)
{
	char aBody[512];
	va_list Args;
	va_start(Args, pFormat);
	str_format_v(aBody, sizeof(aBody), pFormat, Args);
	va_end(Args);
	m_LastError = aBody;
	BindManagerLog("error: %s", aBody);
}

int CBindManager::LastError(char *pBuf, int Size) const
{
	if(m_LastError.empty() || !pBuf || Size <= 0)
	{
		if(pBuf && Size > 0)
			pBuf[0] = '\0';
		return 0;
	}
	str_copy(pBuf, m_LastError.c_str(), Size);
	return 1;
}

void CBindManager::OnReset()
{
	m_LastError.clear();
}

void CBindManager::OnStateChange(int NewState, int OldState)
{
	// 每次进入服务器前初始化：只有从非 ONLINE 进入 ONLINE 时执行一次。
	if(NewState != IClient::STATE_ONLINE || OldState == IClient::STATE_ONLINE)
		return;
	if(g_Config.m_RcBindManagerInit == 0)
	{
		BindManagerLog("auto init disabled, skip cfg execution");
		return;
	}
	if(g_Config.m_RcBindManagerConfig[0] == '\0')
	{
		BindManagerLog("no config selected, skip cfg execution");
		return;
	}
	ExecuteCurrentConfig();
}

void CBindManager::ExecuteCurrentConfig()
{
	// task-86: this applies the cfg unconditionally. The auto-init toggle is checked by
	// OnStateChange only; the manual Load button intentionally applies once even when the
	// automatic join initialization is disabled (report explains the semantics).
	const std::string Config = g_Config.m_RcBindManagerConfig;
	if(Config.empty())
		return;
	if(!LoadConfigFromDisk(Config))
	{
		BindManagerLog("cannot load config '%s' for execution", Config.c_str());
		return;
	}
	UnbindManagedKeys(Config);
	int NumRun = 0;
	std::vector<std::pair<int, int>> vApplied;
	for(const SEntry &Entry : m_vEntries)
	{
		if(!Entry.m_Enabled || !IsValidName(Entry.m_Name.c_str()))
			continue;
		// task-92: initialization only runs the init entry command and binds the key to step1.
		// step1.cfg (command1) must NOT be executed here: command1 belongs to the first key
		// press, followed by command2..N and wrap back to command1.
		const std::string InitPath = EntryCfgPath(Config, Entry.m_Name, "init.cfg");
		Console()->ExecuteFile(InitPath.c_str(), IConsole::CLIENT_ID_UNSPECIFIED, true, IStorage::TYPE_SAVE);
		const std::string BindLine = BuildBindLine(Config, Entry);
		if(!BindLine.empty())
		{
			Console()->ExecuteLine(BindLine.c_str(), IConsole::CLIENT_ID_UNSPECIFIED);
			if(Entry.m_Key != KEY_UNKNOWN && IsValidBinding(Entry.m_Key, Entry.m_ModifierMask))
				vApplied.emplace_back(Entry.m_Key, Entry.m_ModifierMask);
			BindManagerLog("init executed and key bound to step1: entry='%s' key=%d mod=%d", Entry.m_Name.c_str(), Entry.m_Key, Entry.m_ModifierMask);
		}
		else
		{
			BindManagerLog("init executed (no key bound): entry='%s'", Entry.m_Name.c_str());
		}
		NumRun++;
	}
	WriteAppliedKeys(vApplied);
	BindManagerLog("init entry done (no command1 executed): config='%s' entries=%d applied_keys=%d", Config.c_str(), NumRun, (int)vApplied.size());
}

std::string CBindManager::ConfigDir(const std::string &Config) const
{
	return "binds/" + Config;
}

std::string CBindManager::EntryDir(const std::string &Config, const std::string &Entry) const
{
	return ConfigDir(Config) + "/" + Entry;
}

std::string CBindManager::ManagerPath(const std::string &Config) const
{
	return ConfigDir(Config) + "/manager.txt";
}

std::string CBindManager::EntryCfgPath(const std::string &Config, const std::string &Entry, const char *pCfg) const
{
	return EntryDir(Config, Entry) + "/" + pCfg;
}

std::string CBindManager::AbsoluteSavePath(const std::string &Relative) const
{
	char aPath[BIND_MANAGER_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, Relative.c_str(), aPath, sizeof(aPath));
	return aPath;
}

void CBindManager::RefreshConfigs()
{
	m_vConfigs.clear();
	Storage()->ListDirectory(IStorage::TYPE_SAVE, "binds", ListConfigDirsCallback, &m_vConfigs);
	std::sort(m_vConfigs.begin(), m_vConfigs.end(), [](const std::string &A, const std::string &B) {
		return str_comp_filenames(A.c_str(), B.c_str()) < 0;
	});
	// If the persisted selection no longer exists, keep it as the current GUI selection so the
	// user can still save/create it; otherwise the list simply has no highlight.
	if(m_CurrentConfig.empty() && g_Config.m_RcBindManagerConfig[0] != '\0')
		m_CurrentConfig = g_Config.m_RcBindManagerConfig;
	m_Initialized = true;
}

bool CBindManager::SelectConfig(const char *pName, bool LoadFromDisk)
{
	if(!pName || !pName[0])
		return false;
	if(!IsValidName(pName))
	{
		SetLastError("invalid config name '%s'", pName);
		return false;
	}
	m_CurrentConfig = pName;
	str_copy(g_Config.m_RcBindManagerConfig, pName, sizeof(g_Config.m_RcBindManagerConfig));
	m_vEntries.clear();
	m_Dirty = false;
	if(LoadFromDisk)
		return LoadConfigFromDisk(m_CurrentConfig);
	return true;
}

bool CBindManager::ReadTextFile(const std::string &Path, std::string &Out)
{
	char *pData = Storage()->ReadFileStr(Path.c_str(), IStorage::TYPE_SAVE);
	if(!pData)
		return false;
	Out = pData;
	free(pData);
	return true;
}

bool CBindManager::WriteTextFile(const std::string &Path, const std::string &Text)
{
	IOHANDLE File = Storage()->OpenFile(Path.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		SetLastError("cannot write '%s'", Path.c_str());
		return false;
	}
	if(!Text.empty())
	{
		const unsigned Length = (unsigned)Text.size();
		if(io_write(File, Text.data(), Length) != Length)
		{
			io_close(File);
			SetLastError("write failed: '%s'", Path.c_str());
			return false;
		}
	}
	if(io_close(File) != 0)
	{
		SetLastError("close failed: '%s'", Path.c_str());
		return false;
	}
	return true;
}

bool CBindManager::EnsureSaveFolder(const std::string &Relative)
{
	std::string Path;
	size_t Pos = 0;
	while(Pos <= Relative.size())
	{
		const size_t Slash = Relative.find('/', Pos);
		const std::string Part = Relative.substr(0, Slash == std::string::npos ? Relative.size() : Slash);
		if(!Part.empty() && !Storage()->FolderExists(Part.c_str(), IStorage::TYPE_SAVE))
			Storage()->CreateFolder(Part.c_str(), IStorage::TYPE_SAVE);
		if(Slash == std::string::npos)
			break;
		Pos = Slash + 1;
	}
	return Storage()->FolderExists(Relative.c_str(), IStorage::TYPE_SAVE);
}

bool CBindManager::IsValidName(const char *pName)
{
	if(!pName || !pName[0] || str_length(pName) > BIND_MANAGER_NAME_LENGTH)
		return false;
	const size_t Length = str_length(pName);
	// task-88 (verifier B1-R): reject Windows-invalid file names up front so a failed import
	// cannot create unusable/leftover paths.
	if(pName[0] == '.' || pName[Length - 1] == ' ' || pName[Length - 1] == '.')
		return false;
	if(str_find(pName, "/") || str_find(pName, "\\") || str_find(pName, "..") || str_find(pName, "\"") ||
		str_find(pName, "\n") || str_find(pName, "\r") || str_find(pName, "\t") || str_find(pName, ":") ||
		str_find(pName, "*") || str_find(pName, "?") || str_find(pName, "<") || str_find(pName, ">") || str_find(pName, "|"))
		return false;
	// Reserved Windows device names (case-insensitive, also reserved with an extension).
	const std::string Stem = [&]() {
		const char *pDot = str_find(pName, ".");
		return std::string(pName, pDot ? (size_t)(pDot - pName) : Length);
	}();
	static const char *const s_apReserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
	for(const char *pReserved : s_apReserved)
	{
		if(str_comp_nocase(Stem.c_str(), pReserved) == 0)
			return false;
	}
	const char *pCursor = pName;
	while(*pCursor)
	{
		const int Codepoint = str_utf8_decode(&pCursor);
		if(Codepoint <= 0 || Codepoint == 0xFFFFFFFF || Codepoint < 0x20 || Codepoint == 0x7F)
			return false;
	}
	return true;
}

bool CBindManager::IsCommandLineExecutable(const char *pLine)
{
	// task-95: split on top-level semicolons only (not inside quotes), handle escapes and '#'
	// comments; each top-level command is checked by its first token. Example:
	//   echo 开启EdgeJump; bind a "+left; +jump"; bind d "+right; +jump"
	// is valid (the semicolons inside the quoted binds are not command separators).
	const std::string Line = TrimSpaces(pLine ? pLine : "");
	if(Line.empty() || Line[0] == '#' || (Line.size() > 1 && Line[0] == '/' && Line[1] == '/'))
		return true; // empty/comment line is harmless
	std::vector<std::string> Segments;
	if(!SplitCommandSegments(Line, Segments))
		return false; // unterminated quote/escape: not a valid executable line
	for(const std::string &RawSegment : Segments)
	{
		const std::string Segment = TrimSpaces(RawSegment);
		if(Segment.empty() || Segment[0] == '#' || (Segment.size() > 1 && Segment[0] == '/' && Segment[1] == '/'))
			continue;
		char aToken[BIND_MANAGER_CMD_LENGTH];
		size_t TokenLen = 0;
		size_t Pos = 0;
		while(Pos < Segment.size() && (Segment[Pos] == ' ' || Segment[Pos] == '\t'))
			Pos++;
		while(Pos < Segment.size() && Segment[Pos] != ' ' && Segment[Pos] != '\t' && TokenLen + 1 < sizeof(aToken))
			aToken[TokenLen++] = Segment[Pos++];
		aToken[TokenLen] = '\0';
		if(aToken[0] == '\0')
			continue;
		if(str_comp(aToken, "bind") != 0)
		{
			const IConsole::ICommandInfo *pInfo = Console()->GetCommandInfo(aToken, CFGFLAG_CLIENT, false);
			if(!pInfo)
				return false;
		}
	}
	return true;
}

bool CBindManager::IsCommandLineWritable(const char *pLine)
{
	if(!pLine)
		return true;
	const std::string Line(pLine);
	return Line.find('\n') == std::string::npos && Line.find('\r') == std::string::npos;
}

bool CBindManager::ReadManagerMetadata(const std::string &Config)
{
	// task-93: version 2 stores init/command colors; version 1 (old) has no colors and defaults
	// every color to DEFAULT_COMMAND_COLOR. Unknown/invalid color tokens fall back as well.
	m_vEntries.clear();
	std::string Data;
	if(!ReadTextFile(ManagerPath(Config), Data))
		return false;

	int Version = 1;
	{
		size_t Pos = 0;
		while(Pos <= Data.size())
		{
			size_t End = Data.find('\n', Pos);
			if(End == std::string::npos)
				End = Data.size();
			std::string Line = Data.substr(Pos, End - Pos);
			if(!Line.empty() && Line.back() == '\r')
				Line.pop_back();
			Pos = End + 1;
			std::vector<std::string> Tokens;
			if(!TokenizeCfgLine(Line, Tokens) || Tokens.empty())
				continue;
			if(str_comp(Tokens[0].c_str(), "version") == 0 && Tokens.size() >= 2)
			{
				Version = str_toint(Tokens[1].c_str()) >= 2 ? 2 : 1;
				break;
			}
		}
	}

	size_t Pos = 0;
	while(Pos <= Data.size())
	{
		size_t End = Data.find('\n', Pos);
		if(End == std::string::npos)
			End = Data.size();
		std::string Line = Data.substr(Pos, End - Pos);
		if(!Line.empty() && Line.back() == '\r')
			Line.pop_back();
		Pos = End + 1;
		if(TrimSpaces(Line).empty())
			continue;
		std::vector<std::string> Tokens;
		if(!TokenizeCfgLine(Line, Tokens))
		{
			BindManagerLog("manager.txt: cannot parse line '%s'", Line.c_str());
			continue;
		}
		if(Tokens.empty() || str_comp(Tokens[0].c_str(), "entry") != 0)
			continue;
		if(Tokens.size() < 5)
			continue;
		SEntry Entry;
		Entry.m_Name = Tokens[1];
		Entry.m_Key = str_toint(Tokens[2].c_str());
		Entry.m_ModifierMask = str_toint(Tokens[3].c_str());
		Entry.m_Enabled = str_toint(Tokens[4].c_str()) != 0;
		if(!ValidateEntryBinding(Entry))
			SetLastError("条目 '%s' 的键位/修饰键非法，已清除绑定", Entry.m_Name.c_str());
		if(Version >= 2)
		{
			if(Tokens.size() < 7)
				continue;
			Entry.m_InitColor = ParseManagerColor(Tokens[5], DEFAULT_COMMAND_COLOR);
			Entry.m_Init = Tokens[6];
			for(size_t i = 7; i + 1 < Tokens.size(); i += 2)
			{
				const unsigned int Color = ParseManagerColor(Tokens[i], DEFAULT_COMMAND_COLOR);
				Entry.m_vCommandColors.push_back(Color);
				Entry.m_vCommands.push_back(Tokens[i + 1]);
			}
		}
		else
		{
			if(Tokens.size() >= 6)
				Entry.m_Init = Tokens[5];
			for(size_t i = 6; i < Tokens.size(); i++)
				Entry.m_vCommands.push_back(Tokens[i]);
		}
		if(Entry.m_vCommands.empty())
			Entry.m_vCommands.emplace_back();
		Entry.m_vCommandColors.resize(Entry.m_vCommands.size(), DEFAULT_COMMAND_COLOR);
		m_vEntries.push_back(std::move(Entry));
	}
	return true;
}

bool CBindManager::LoadConfigFromDisk(const std::string &Config)
{
	m_LastError.clear();
	if(!IsValidName(Config.c_str()))
	{
		SetLastError("invalid config name '%s'", Config.c_str());
		return false;
	}
	m_CurrentConfig = Config;
	str_copy(g_Config.m_RcBindManagerConfig, Config.c_str(), sizeof(g_Config.m_RcBindManagerConfig));
	m_vEntries.clear();
	if(!ReadManagerMetadata(Config))
	{
		// Directory may exist without metadata (fresh/manual config): keep empty GUI state.
		m_vEntries.clear();
		BindManagerLog("config '%s' has no manager.txt yet", Config.c_str());
	}
	m_Dirty = false;
	return true;
}

bool CBindManager::LoadCurrent()
{
	if(m_CurrentConfig.empty())
	{
		if(g_Config.m_RcBindManagerConfig[0] == '\0')
			return false;
		m_CurrentConfig = g_Config.m_RcBindManagerConfig;
	}
	const bool Ok = LoadConfigFromDisk(m_CurrentConfig);
	if(Ok)
	{
		// task-86: manual Load applies the loaded config like joining a server:
		// init.cfg + step1.cfg for all enabled entries (restores the key cycle binding).
		ExecuteCurrentConfig();
	}
	return Ok;
}

bool CBindManager::CreateConfig()
{
	// task-90: create a unique empty config (config1, config2, ...) with an initial
	// manager.txt; never overwrite an existing folder.
	m_LastError.clear();
	if(!EnsureSaveFolder("binds"))
	{
		SetLastError("无法创建 binds 目录");
		return false;
	}
	std::string Name;
	int Suffix = 1;
	while(true)
	{
		Name = "config" + std::to_string(Suffix++);
		if(!Storage()->FolderExists(ConfigDir(Name).c_str(), IStorage::TYPE_SAVE))
			break;
	}
	if(!EnsureSaveFolder(ConfigDir(Name)))
	{
		SetLastError("无法创建配置目录: %s", ConfigDir(Name).c_str());
		return false;
	}
	if(!WriteManagerMetadata(Name))
	{
		(void)fs_removedir(AbsoluteSavePath(ConfigDir(Name)).c_str());
		SetLastError("无法写入初始 manager.txt");
		return false;
	}
	m_CurrentConfig = Name;
	m_vEntries.clear();
	m_Dirty = false;
	str_copy(g_Config.m_RcBindManagerConfig, Name.c_str(), sizeof(g_Config.m_RcBindManagerConfig));
	// The new empty config is not applied yet; remove previously managed keys so the old
	// config cannot keep executing while the new one is selected (same as config switching).
	ClearManagedBindings();
	RefreshConfigs();
	BindManagerLog("created config '%s'", Name.c_str());
	return true;
}

bool CBindManager::DeleteConfig(const char *pName, std::string &Error)
{
	// task-49: delete a config directory with a confirmation flow in the GUI. To avoid leaving a
	// visibly half-deleted config on IO errors, first atomically rename the folder to a hidden
	// ".tmp-delete-*" name (fails => nothing changed), then recursively clean the hidden temp.
	m_LastError.clear();
	Error.clear();
	if(!pName || !pName[0] || !IsValidName(pName))
	{
		Error = "配置名不合法";
		SetLastError("%s", Error.c_str());
		return false;
	}
	const std::string Name = pName;
	const std::string Dir = ConfigDir(Name);
	if(!Storage()->FolderExists(Dir.c_str(), IStorage::TYPE_SAVE))
	{
		Error = "配置不存在: " + Name;
		SetLastError("%s", Error.c_str());
		return false;
	}
	const bool WasCurrent = str_comp(m_CurrentConfig.c_str(), Name.c_str()) == 0 ||
				str_comp(g_Config.m_RcBindManagerConfig, Name.c_str()) == 0;

	std::string Temp = "binds/.tmp-delete-" + std::to_string((long long)time_get()) + "-" + std::to_string((int)Name.size());
	while(Storage()->FolderExists(Temp.c_str(), IStorage::TYPE_SAVE))
		Temp += "-x";
	const std::string OldAbsolute = AbsoluteSavePath(Dir);
	const std::string TempAbsolute = AbsoluteSavePath(Temp);
	if(fs_rename(OldAbsolute.c_str(), TempAbsolute.c_str()) != 0)
	{
		Error = "无法删除配置目录（重命名失败）: " + Name;
		SetLastError("%s", Error.c_str());
		return false;
	}
	const bool Cleaned = RemoveDirectoryRecursively(Temp);
	if(!Cleaned)
		BindManagerLog("warning: deleted config temp cleanup incomplete: %s", Temp.c_str());

	if(WasCurrent)
	{
		// Remove managed bindings belonging to the applied config; user-owned bindings are
		// untouched (ClearManagedBindings only unbinds keys in applied.txt).
		ClearManagedBindings();
	}
	RefreshConfigs();
	if(WasCurrent)
	{
		m_vEntries.clear();
		m_Dirty = false;
		if(!m_vConfigs.empty())
		{
			const std::string Next = m_vConfigs.front();
			if(!SelectConfig(Next.c_str(), true))
			{
				Error = "已删除配置，但无法加载下一个配置: " + Next;
				SetLastError("%s", Error.c_str());
			}
		}
		else
		{
			m_CurrentConfig.clear();
			g_Config.m_RcBindManagerConfig[0] = '\0';
			m_vEntries.clear();
		}
	}
	BindManagerLog("deleted config '%s' (current=%d temp_cleanup=%d)", Name.c_str(), WasCurrent ? 1 : 0, Cleaned ? 1 : 0);
	return true;
}

bool CBindManager::SaveMetadataOnly()
{
	if(m_CurrentConfig.empty())
		return false;
	if(!EnsureSaveFolder(ConfigDir(m_CurrentConfig)))
		return false;
	return WriteManagerMetadata(m_CurrentConfig);
}

bool CBindManager::WriteManagerMetadata(const std::string &Config)
{
	// task-93: version 2 adds init/command message colors. Old version 1 files stay readable.
	// entry "<name>" <key> <modifier> <enabled> <initColor> "<init>" <cmd1Color> "<cmd1>" ...
	std::string Text = "version 2\n";
	for(const SEntry &Entry : m_vEntries)
	{
		Text += "entry ";
		AppendQuoted(Text, Entry.m_Name);
		Text += ' ';
		Text += std::to_string(Entry.m_Key);
		Text += ' ';
		Text += std::to_string(Entry.m_ModifierMask);
		Text += ' ';
		Text += Entry.m_Enabled ? '1' : '0';
		Text += ' ';
		Text += std::to_string(Entry.m_InitColor);
		Text += ' ';
		AppendQuoted(Text, Entry.m_Init);
		for(size_t i = 0; i < Entry.m_vCommands.size(); i++)
		{
			const unsigned int Color = i < Entry.m_vCommandColors.size() ? Entry.m_vCommandColors[i] : DEFAULT_COMMAND_COLOR;
			Text += ' ';
			Text += std::to_string(Color);
			Text += ' ';
			AppendQuoted(Text, Entry.m_vCommands[i]);
		}
		Text += '\n';
	}
	return WriteTextFile(ManagerPath(Config), Text);
}

size_t CBindManager::EffectiveCommandCount(const SEntry &Entry)
{
	size_t Count = 0;
	for(const std::string &Command : Entry.m_vCommands)
	{
		if(!TrimSpaces(Command).empty())
			Count++;
	}
	return Count;
}

bool CBindManager::IsValidBinding(int Key, int ModifierMask) const
{
	if(Key == KEY_UNKNOWN)
		return true; // unbound entry is valid (no bind generated)
	if(Key < KEY_FIRST || Key >= KEY_LAST)
		return false;
	if(ModifierMask < 0 || ModifierMask >= KeyModifier::COMBINATION_COUNT)
		return false;
	return true;
}

bool CBindManager::ValidateEntryBinding(SEntry &Entry)
{
	// task-87 (verifier B2): manager.txt/imported values must never reach GetKeyBindName
	// with out-of-range keys/modifiers (assert/out-of-bounds). Clear and report.
	if(!IsValidBinding(Entry.m_Key, Entry.m_ModifierMask))
	{
		BindManagerLog("entry '%s' has invalid key/modifier %d/%d; clearing bind", Entry.m_Name.c_str(), Entry.m_Key, Entry.m_ModifierMask);
		Entry.m_Key = KEY_UNKNOWN;
		Entry.m_ModifierMask = 0;
		return false;
	}
	if(Entry.m_Key == KEY_UNKNOWN)
		Entry.m_ModifierMask = 0;
	return true;
}

std::string CBindManager::BuildBindCommand(const std::string &Config, const SEntry &Entry, const char *pStepFile) const
{
	if(Entry.m_Key == KEY_UNKNOWN)
		return "";
	if(!IsValidBinding(Entry.m_Key, Entry.m_ModifierMask))
	{
		BindManagerLog("skip bind for '%s': invalid key/modifier %d/%d", Entry.m_Name.c_str(), Entry.m_Key, Entry.m_ModifierMask);
		return "";
	}
	char aKeyName[128];
	GameClient()->m_Binds.GetKeyBindName(Entry.m_Key, Entry.m_ModifierMask, aKeyName, sizeof(aKeyName));
	char aExecCommand[BIND_MANAGER_PATH_LENGTH];
	str_format(aExecCommand, sizeof(aExecCommand), "exec \"%s/%s\"", EntryDir(Config, Entry.m_Name).c_str(), pStepFile);
	char aEscaped[BIND_MANAGER_PATH_LENGTH * 2];
	char *pDst = aEscaped;
	str_escape(&pDst, aExecCommand, aEscaped + sizeof(aEscaped));
	char aLine[BIND_MANAGER_PATH_LENGTH * 2 + 128];
	str_format(aLine, sizeof(aLine), "bind %s \"%s\"", aKeyName, aEscaped);
	return aLine;
}

std::string CBindManager::BuildBindLine(const std::string &Config, const SEntry &Entry) const
{
	// task-95: single effective command binds the command directly (no exec/step1); multiple
	// commands keep the existing exec step cycle bound via step1.cfg.
	if(Entry.m_Key == KEY_UNKNOWN || !IsValidBinding(Entry.m_Key, Entry.m_ModifierMask))
		return "";
	char aKeyName[128];
	GameClient()->m_Binds.GetKeyBindName(Entry.m_Key, Entry.m_ModifierMask, aKeyName, sizeof(aKeyName));

	std::string Command;
	unsigned int CommandColor = DEFAULT_COMMAND_COLOR;
	size_t NumCommands = 0;
	for(size_t i = 0; i < Entry.m_vCommands.size(); i++)
	{
		if(TrimSpaces(Entry.m_vCommands[i]).empty())
			continue;
		if(NumCommands == 0)
		{
			Command = Entry.m_vCommands[i];
			CommandColor = i < Entry.m_vCommandColors.size() ? Entry.m_vCommandColors[i] : DEFAULT_COMMAND_COLOR;
		}
		NumCommands++;
	}
	if(NumCommands == 0)
		return "";
	if(NumCommands > 1)
		return BuildBindCommand(Config, Entry, "step1.cfg");

	// Single command: bind it directly with the same color prefix/suffix used by step files.
	const std::string Inner = "cl_message_client_color " + std::to_string(CommandColor) + "; " + Command + "; cl_message_client_color " + std::to_string(DEFAULT_COMMAND_COLOR);
	char aEscaped[BIND_MANAGER_PATH_LENGTH * 2];
	char *pDst = aEscaped;
	str_escape(&pDst, Inner.c_str(), aEscaped + sizeof(aEscaped));
	char aLine[BIND_MANAGER_PATH_LENGTH * 2 + 128];
	str_format(aLine, sizeof(aLine), "bind %s \"%s\"", aKeyName, aEscaped);
	return aLine;
}

bool CBindManager::WriteEntryCfgFiles(const std::string &Config, const SEntry &Entry, std::string &Error)
{
	const std::string Dir = EntryDir(Config, Entry.m_Name);
	if(!EnsureSaveFolder(Dir))
	{
		Error = "无法创建条目目录: " + Dir;
		return false;
	}
	std::string InitText;
	if(!TrimSpaces(Entry.m_Init).empty())
	{
		// task-93: wrap the whole init block with the configured message color, reset afterwards.
		InitText += "cl_message_client_color " + std::to_string(Entry.m_InitColor) + "\n";
		InitText += Entry.m_Init;
		if(InitText.back() != '\n')
			InitText += '\n';
		InitText += "cl_message_client_color " + std::to_string(DEFAULT_COMMAND_COLOR) + "\n";
	}
	if(!WriteTextFile(Dir + "/init.cfg", InitText))
	{
		Error = "无法写入 init.cfg";
		return false;
	}

	// task-86: empty/whitespace-only command rows (e.g. from "extend/add command") must not
	// become loop steps; task-95: a single effective command is bound directly, no step files.
	std::vector<std::string> vCommands;
	std::vector<unsigned int> vCommandColors;
	for(size_t i = 0; i < Entry.m_vCommands.size(); i++)
	{
		if(TrimSpaces(Entry.m_vCommands[i]).empty())
			continue;
		vCommands.push_back(Entry.m_vCommands[i]);
		vCommandColors.push_back(i < Entry.m_vCommandColors.size() ? Entry.m_vCommandColors[i] : DEFAULT_COMMAND_COLOR);
	}
	const size_t NumCommands = vCommands.size();
	const std::string BindLine = BuildBindLine(Config, Entry);
	if(!WriteTextFile(Dir + "/bind.cfg", BindLine.empty() ? std::string() : BindLine + "\n"))
	{
		Error = "无法写入 bind.cfg";
		return false;
	}
	if(NumCommands < 2)
	{
		// No command or exactly one command: direct bind (or none) -> remove all step files.
		RemoveStaleStepFiles(Config, Entry, 0);
		return true;
	}
	for(size_t Step = 0; Step < NumCommands; Step++)
	{
		std::string Text;
		if(Step < vCommands.size())
		{
			// task-93: only non-empty commands are wrapped; bind line stays outside the wrapper.
			Text += "cl_message_client_color " + std::to_string(vCommandColors[Step]) + "\n";
			Text += vCommands[Step] + "\n";
			Text += "cl_message_client_color " + std::to_string(DEFAULT_COMMAND_COLOR) + "\n";
		}
		const size_t NextStep = (Step + 1) % NumCommands;
		char aNextFile[64];
		str_format(aNextFile, sizeof(aNextFile), "step%zu.cfg", NextStep + 1);
		const std::string NextBind = BuildBindCommand(Config, Entry, aNextFile);
		if(!NextBind.empty())
			Text += NextBind + "\n";
		char aStepFile[64];
		str_format(aStepFile, sizeof(aStepFile), "step%zu.cfg", Step + 1);
		if(g_Config.m_Debug)
			BindManagerLog("cfg step entry=%s step=%zu next=%s commands=%zu", Entry.m_Name.c_str(), Step + 1, aNextFile, vCommands.size());
		if(!WriteTextFile(Dir + "/" + aStepFile, Text))
		{
			Error = "无法写入 " + std::string(aStepFile);
			return false;
		}
	}
	RemoveStaleStepFiles(Config, Entry, NumCommands);
	return true;
}

void CBindManager::DeleteEntryContents(const std::string &Config, const std::string &Entry)
{
	SDirListContext Context;
	ListDirectory(Storage(), IStorage::TYPE_SAVE, EntryDir(Config, Entry), Context);
	for(const std::string &Name : Context.m_vNames)
	{
		if(str_comp(Name.c_str(), ".") == 0 || str_comp(Name.c_str(), "..") == 0)
			continue;
		if(Context.m_vIsDir[&Name - Context.m_vNames.data()])
			continue; // unexpected nested dir: leave to fs_removedir failure (no file-delete noise)
		Storage()->RemoveFile((EntryDir(Config, Entry) + "/" + Name).c_str(), IStorage::TYPE_SAVE);
	}
	const std::string Absolute = AbsoluteSavePath(EntryDir(Config, Entry));
	(void)fs_removedir(Absolute.c_str());
}

void CBindManager::DeleteConfigContents(const std::string &Config)
{
	SDirListContext Context;
	ListDirectory(Storage(), IStorage::TYPE_SAVE, ConfigDir(Config), Context);
	for(size_t i = 0; i < Context.m_vNames.size(); i++)
	{
		const std::string &Name = Context.m_vNames[i];
		if(Context.m_vIsDir[i])
			DeleteEntryContents(Config, Name);
		else
			Storage()->RemoveFile((ConfigDir(Config) + "/" + Name).c_str(), IStorage::TYPE_SAVE);
	}
	const std::string Absolute = AbsoluteSavePath(ConfigDir(Config));
	(void)fs_removedir(Absolute.c_str());
}

std::string CBindManager::AppliedKeysPath() const
{
	return "binds/applied.txt";
}

std::vector<std::pair<int, int>> CBindManager::ReadAppliedKeys()
{
	// task-87 (verifier B3): global record of keys last bound by the Bind Manager (across
	// configs), so stale bindings from disabled/deleted/renamed entries can be removed.
	std::vector<std::pair<int, int>> vKeys;
	std::string Data;
	if(!ReadTextFile(AppliedKeysPath(), Data))
		return vKeys;
	size_t Pos = 0;
	while(Pos < Data.size())
	{
		size_t End = Data.find('\n', Pos);
		if(End == std::string::npos)
			End = Data.size();
		const std::string Line = TrimSpaces(Data.substr(Pos, End - Pos));
		Pos = End + 1;
		if(Line.empty())
			continue;
		// task-88 (verifier B3): write format is "<key> <modifier>"; parse both tokens
		// separately (str_toint on the whole line always fails because of the trailing space).
		const size_t Space = Line.find(' ');
		if(Space == std::string::npos)
			continue;
		const std::string KeyText = TrimSpaces(Line.substr(0, Space));
		const std::string ModifierText = TrimSpaces(Line.substr(Space + 1));
		int Key = KEY_UNKNOWN;
		int Modifier = 0;
		if(!str_toint(KeyText.c_str(), &Key) || !str_toint(ModifierText.c_str(), &Modifier))
			continue;
		if(Key != KEY_UNKNOWN && IsValidBinding(Key, Modifier))
			vKeys.emplace_back(Key, Modifier);
	}
	return vKeys;
}

bool CBindManager::WriteAppliedKeys(const std::vector<std::pair<int, int>> &Keys)
{
	if(!EnsureSaveFolder("binds"))
		return false;
	std::string Text;
	for(const auto &[Key, Modifier] : Keys)
	{
		Text += std::to_string(Key);
		Text += ' ';
		Text += std::to_string(Modifier);
		Text += '\n';
	}
	return WriteTextFile(AppliedKeysPath(), Text);
}

void CBindManager::UnbindKey(int Key, int ModifierMask)
{
	if(Key == KEY_UNKNOWN || !IsValidBinding(Key, ModifierMask))
		return;
	char aKeyName[128];
	GameClient()->m_Binds.GetKeyBindName(Key, ModifierMask, aKeyName, sizeof(aKeyName));
	char aLine[192];
	str_format(aLine, sizeof(aLine), "unbind %s", aKeyName);
	Console()->ExecuteLine(aLine, IConsole::CLIENT_ID_UNSPECIFIED);
	BindManagerLog("unbind %s", aKeyName);
}

void CBindManager::CleanupEntryBinding(const SEntry &Entry)
{
	if(Entry.m_Key == KEY_UNKNOWN || !IsValidBinding(Entry.m_Key, Entry.m_ModifierMask))
		return;
	auto vApplied = ReadAppliedKeys();
	const bool WasApplied = std::any_of(vApplied.begin(), vApplied.end(), [&](const auto &Item) {
		return Item.first == Entry.m_Key && Item.second == Entry.m_ModifierMask;
	});
	if(!WasApplied)
		return; // task-88 (verifier B3): never unbind a user-owned bind
	UnbindKey(Entry.m_Key, Entry.m_ModifierMask);
	std::erase_if(vApplied, [&](const auto &Item) { return Item.first == Entry.m_Key && Item.second == Entry.m_ModifierMask; });
	WriteAppliedKeys(vApplied);
}

void CBindManager::UnbindManagedKeys(const std::string &Config)
{
	// Unbind both the previously applied keys and every key currently present in the config:
	// this clears old step bindings for disabled/deleted/renamed entries; enabled entries are
	// bound again afterwards by step1.cfg/bind step1.
	// task-88 (verifier B3): only the applied record and the enabled entries that are about to
	// be rebound are managed. A disabled-only key that is not in applied.txt is a user-owned
	// bind and must be left untouched.
	std::vector<std::pair<int, int>> vKeys = ReadAppliedKeys();
	for(const SEntry &Entry : m_vEntries)
	{
		if(Entry.m_Enabled && Entry.m_Key != KEY_UNKNOWN && IsValidBinding(Entry.m_Key, Entry.m_ModifierMask))
			vKeys.emplace_back(Entry.m_Key, Entry.m_ModifierMask);
	}
	std::sort(vKeys.begin(), vKeys.end());
	vKeys.erase(std::unique(vKeys.begin(), vKeys.end()), vKeys.end());
	for(const auto &[Key, Modifier] : vKeys)
		UnbindKey(Key, Modifier);
	(void)Config;
}

void CBindManager::RefreshBindings()
{
	// task-87 (verifier B3/B4): after Save/rename/command changes, remove all managed keys and
	// bind enabled entries directly to step1 (without executing command1), so the current key
	// can never point at a stale/deleted step and disabled/deleted/renamed entries are cleared.
	const std::string Config = m_CurrentConfig;
	if(Config.empty())
		return;
	UnbindManagedKeys(Config); // task-88 (verifier B3): clear old managed keys first
	std::vector<std::pair<int, int>> vApplied;
	for(const SEntry &Entry : m_vEntries)
	{
		if(!Entry.m_Enabled || Entry.m_Key == KEY_UNKNOWN)
			continue;
		const std::string BindLine = BuildBindLine(Config, Entry);
		if(BindLine.empty())
			continue;
		Console()->ExecuteLine(BindLine.c_str(), IConsole::CLIENT_ID_UNSPECIFIED);
		vApplied.emplace_back(Entry.m_Key, Entry.m_ModifierMask);
	}
	WriteAppliedKeys(vApplied);
	BindManagerLog("bindings refreshed: config='%s' enabled=%d", Config.c_str(), (int)vApplied.size());
}

void CBindManager::ClearManagedBindings()
{
	// task-88 (verifier B3): switching config unbinds the previously managed keys so the old
	// config cannot keep executing while another config is selected in the GUI.
	const auto vApplied = ReadAppliedKeys();
	for(const auto &[Key, Modifier] : vApplied)
		UnbindKey(Key, Modifier);
	WriteAppliedKeys({});
}

bool CBindManager::RemoveDirectoryRecursively(const std::string &RelativeDir)
{
	SDirListContext Context;
	ListDirectory(Storage(), IStorage::TYPE_SAVE, RelativeDir, Context);
	for(size_t i = 0; i < Context.m_vNames.size(); i++)
	{
		const std::string &Name = Context.m_vNames[i];
		if(str_comp(Name.c_str(), ".") == 0 || str_comp(Name.c_str(), "..") == 0)
			continue;
		const std::string Child = RelativeDir + "/" + Name;
		if(Context.m_vIsDir[i])
			RemoveDirectoryRecursively(Child);
		else
			Storage()->RemoveFile(Child.c_str(), IStorage::TYPE_SAVE);
	}
	(void)fs_removedir(AbsoluteSavePath(RelativeDir).c_str());
	return !Storage()->FolderExists(RelativeDir.c_str(), IStorage::TYPE_SAVE);
}

void CBindManager::RemoveStaleStepFiles(const std::string &Config, const SEntry &Entry, size_t NumCommands)
{
	// task-87 (verifier B4): delete only this feature's step<number>.cfg files outside
	// step1..NumCommands; keep init.cfg/bind.cfg/manager.txt and any other files.
	const std::string Dir = EntryDir(Config, Entry.m_Name);
	SDirListContext Context;
	ListDirectory(Storage(), IStorage::TYPE_SAVE, Dir, Context);
	for(size_t i = 0; i < Context.m_vNames.size(); i++)
	{
		if(Context.m_vIsDir[i])
			continue;
		const std::string &Name = Context.m_vNames[i];
		if(str_comp(Name.c_str(), ".") == 0 || str_comp(Name.c_str(), "..") == 0)
			continue;
		if(Name.rfind("step", 0) != 0 || Name.size() < 9 || Name.compare(Name.size() - 4, 4, ".cfg") != 0)
			continue;
		const std::string Number = Name.substr(4, Name.size() - 8);
		if(Number.empty() || !std::all_of(Number.begin(), Number.end(), [](char Ch) { return Ch >= '0' && Ch <= '9'; }))
			continue;
		const size_t StepNumber = (size_t)str_toint(Number.c_str());
		if(StepNumber >= 1 && StepNumber <= NumCommands)
			continue;
		Storage()->RemoveFile((Dir + "/" + Name).c_str(), IStorage::TYPE_SAVE);
		BindManagerLog("removed stale step file %s/%s", Entry.m_Name.c_str(), Name.c_str());
	}
}

bool CBindManager::SaveCurrentToDisk()
{
	m_LastError.clear();
	std::string Config = m_CurrentConfig;
	if(Config.empty())
	{
		Config = g_Config.m_RcBindManagerConfig[0] != '\0' ? g_Config.m_RcBindManagerConfig : "default";
		if(!IsValidName(Config.c_str()))
			Config = "default";
		int Suffix = 1;
		std::string Candidate = Config;
		char aAbsolute[BIND_MANAGER_PATH_LENGTH];
		while(true)
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, ConfigDir(Candidate).c_str(), aAbsolute, sizeof(aAbsolute));
			if(!fs_is_dir(aAbsolute))
				break;
			// Existing directory: reuse it only if it is empty, otherwise create a suffix.
			SDirListContext Context;
			ListDirectory(Storage(), IStorage::TYPE_SAVE, ConfigDir(Candidate), Context);
			if(Context.m_vNames.empty())
				break;
			Candidate = Config + "-" + std::to_string(Suffix++);
		}
		Config = Candidate;
		m_CurrentConfig = Config;
	}
	if(!IsValidName(Config.c_str()))
	{
		SetLastError("配置名不合法: '%s'", Config.c_str());
		return false;
	}
	if(!EnsureSaveFolder(ConfigDir(Config)))
	{
		SetLastError("无法创建配置目录: %s", ConfigDir(Config).c_str());
		return false;
	}

	// Sanitize entry names + ensure per-config uniqueness before writing folders.
	std::unordered_set<std::string> UsedNames;
	for(SEntry &Entry : m_vEntries)
	{
		std::string Base = TrimSpaces(Entry.m_Name);
		if(!IsValidName(Base.c_str()))
			Base = "entry";
		std::string Name = Base;
		int Suffix = 1;
		while(UsedNames.count(Name))
			Name = Base + "-" + std::to_string(Suffix++);
		Entry.m_Name = Name;
		UsedNames.insert(Name);
	}
	for(SEntry &Entry : m_vEntries)
	{
		if(Entry.m_vCommands.empty())
			Entry.m_vCommands.emplace_back();
		Entry.m_vCommandColors.resize(Entry.m_vCommands.size(), DEFAULT_COMMAND_COLOR);
		ValidateEntryBinding(Entry);
	}

	// Remove entry directories that are no longer part of the manager (renames/removals).
	SDirListContext Existing;
	ListDirectory(Storage(), IStorage::TYPE_SAVE, ConfigDir(Config), Existing);
	for(size_t i = 0; i < Existing.m_vNames.size(); i++)
	{
		const std::string &Name = Existing.m_vNames[i];
		if(str_comp(Name.c_str(), ".") == 0 || str_comp(Name.c_str(), "..") == 0)
			continue;
		if(!Existing.m_vIsDir[i])
			continue;
		if(!UsedNames.count(Name))
			DeleteEntryContents(Config, Name);
	}

	std::string Error;
	for(const SEntry &Entry : m_vEntries)
	{
		if(!WriteEntryCfgFiles(Config, Entry, Error))
		{
			SetLastError("%s", Error.c_str());
			return false;
		}
	}
	if(!WriteManagerMetadata(Config))
	{
		SetLastError("无法写入 manager.txt");
		return false;
	}
	str_copy(g_Config.m_RcBindManagerConfig, Config.c_str(), sizeof(g_Config.m_RcBindManagerConfig));
	RefreshConfigs();
	RefreshBindings();
	m_Dirty = false;
	BindManagerLog("saved config '%s' entries=%d", Config.c_str(), (int)m_vEntries.size());
	return true;
}

bool CBindManager::SaveCurrent()
{
	return SaveCurrentToDisk();
}

bool CBindManager::RenameConfig(const char *pNewName)
{
	m_LastError.clear();
	if(m_CurrentConfig.empty() || !pNewName || !IsValidName(pNewName))
	{
		SetLastError("配置名不合法");
		return false;
	}
	const std::string NewName = pNewName;
	if(NewName == m_CurrentConfig)
		return true;
	const std::string OldAbsolute = AbsoluteSavePath(ConfigDir(m_CurrentConfig));
	const std::string NewAbsolute = AbsoluteSavePath(ConfigDir(NewName));
	const bool OldExists = fs_is_dir(OldAbsolute.c_str());
	if(!OldExists)
	{
		SetLastError("当前配置目录不存在，无法重命名");
		return false;
	}
	if(fs_is_dir(NewAbsolute.c_str()))
	{
		SetLastError("目标配置已存在: %s", NewName.c_str());
		return false;
	}
	if(fs_rename(OldAbsolute.c_str(), NewAbsolute.c_str()) == 0)
	{
		m_CurrentConfig = NewName;
		str_copy(g_Config.m_RcBindManagerConfig, NewName.c_str(), sizeof(g_Config.m_RcBindManagerConfig));
		// The old config name is baked into every bind/step path: regenerate the cfg files for
		// the renamed folder and refresh bindings so keys point at the new paths.
		if(!SaveCurrentToDisk())
			return false;
		return true;
	}
	// Fallback: copy all files (including entry subdirectories) and delete the old contents
	// (works when fs_rename fails, e.g. when the target is on another filesystem).
	if(!EnsureSaveFolder(ConfigDir(NewName)))
	{
		SetLastError("无法创建配置目录");
		return false;
	}
	std::vector<SZipFile> Files;
	std::string CopyError;
	if(!CollectConfigFiles(m_CurrentConfig, Files, CopyError))
	{
		SetLastError("%s", CopyError.c_str());
		return false;
	}
	for(const SZipFile &File : Files)
	{
		std::string Relative = File.m_Name;
		if(Relative.rfind(m_CurrentConfig + "/", 0) != 0)
			continue;
		Relative = NewName + Relative.substr(m_CurrentConfig.size());
		const size_t LastSlash = Relative.rfind('/');
		if(LastSlash != std::string::npos)
			EnsureSaveFolder("binds/" + Relative.substr(0, LastSlash));
		IOHANDLE FileOut = Storage()->OpenFile(("binds/" + Relative).c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(FileOut)
		{
			if(!File.m_Data.empty())
				io_write(FileOut, File.m_Data.data(), (unsigned)File.m_Data.size());
			io_close(FileOut);
		}
	}
	DeleteConfigContents(m_CurrentConfig);
	m_CurrentConfig = NewName;
	str_copy(g_Config.m_RcBindManagerConfig, NewName.c_str(), sizeof(g_Config.m_RcBindManagerConfig));
	if(!SaveCurrentToDisk())
		return false;
	return true;
}

bool CBindManager::RenameEntry(int Index, const char *pNewName)
{
	if(Index < 0 || Index >= (int)m_vEntries.size() || !pNewName || !IsValidName(pNewName))
		return false;
	for(size_t i = 0; i < m_vEntries.size(); i++)
	{
		if((int)i != Index && str_comp(m_vEntries[i].m_Name.c_str(), pNewName) == 0)
			return false;
	}
	m_vEntries[Index].m_Name = pNewName;
	m_Dirty = true;
	return true;
}

void CBindManager::AddEntry()
{
	int Suffix = 1;
	std::string Name;
	do
	{
		Name = "entry" + std::to_string((int)m_vEntries.size() + Suffix);
		Suffix++;
	} while(std::any_of(m_vEntries.begin(), m_vEntries.end(), [&Name](const SEntry &Entry) { return Entry.m_Name == Name; }));
	SEntry Entry;
	Entry.m_Name = Name;
	Entry.m_vCommands.emplace_back();
	Entry.m_vCommandColors.push_back(DEFAULT_COMMAND_COLOR);
	m_vEntries.push_back(std::move(Entry));
	m_Dirty = true;
}

void CBindManager::RemoveEntry(int Index)
{
	if(Index < 0 || Index >= (int)m_vEntries.size())
		return;
	m_vEntries.erase(m_vEntries.begin() + Index);
	m_Dirty = true;
}

void CBindManager::MoveEntry(int From, int To, bool SaveMetadata)
{
	if(From < 0 || To < 0 || From >= (int)m_vEntries.size() || To >= (int)m_vEntries.size() || From == To)
		return;
	const SEntry Entry = m_vEntries[From];
	m_vEntries.erase(m_vEntries.begin() + From);
	m_vEntries.insert(m_vEntries.begin() + To, Entry);
	m_Dirty = true;
	if(SaveMetadata && !m_CurrentConfig.empty())
		WriteManagerMetadata(m_CurrentConfig);
}

bool CBindManager::PickFile(bool Save, const char *pDefaultName, char *pOutPath, size_t OutSize)
{
#if defined(CONF_FAMILY_WINDOWS)
	if(!pOutPath || OutSize == 0)
		return false;
	wchar_t aFile[MAX_PATH] = {};
	if(pDefaultName && pDefaultName[0])
		Utf8ToWide(pDefaultName, aFile, (int)(sizeof(aFile) / sizeof(aFile[0])));
	char aInitialDir[BIND_MANAGER_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, "binds", aInitialDir, sizeof(aInitialDir));
	wchar_t aWideDir[MAX_PATH] = {};
	Utf8ToWide(aInitialDir, aWideDir, (int)(sizeof(aWideDir) / sizeof(aWideDir[0])));
	OPENFILENAMEW Dialog = {};
	Dialog.lStructSize = sizeof(Dialog);
	Dialog.lpstrFilter = Save ? L"Zip files (*.zip)\0*.zip\0All files (*.*)\0*.*\0" : L"Zip files (*.zip)\0*.zip\0All files (*.*)\0*.*\0";
	Dialog.lpstrFile = aFile;
	Dialog.nMaxFile = (DWORD)(sizeof(aFile) / sizeof(aFile[0]));
	Dialog.lpstrDefExt = L"zip";
	Dialog.lpstrInitialDir = aWideDir;
	Dialog.Flags = Save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR);
	const BOOL Ok = Save ? GetSaveFileNameW(&Dialog) : GetOpenFileNameW(&Dialog);
	if(!Ok)
		return false;
	return WideToUtf8(aFile, pOutPath, (int)OutSize);
#else
	(void)Save;
	(void)pDefaultName;
	(void)pOutPath;
	(void)OutSize;
	return false;
#endif
}

bool CBindManager::CollectConfigFiles(const std::string &Config, std::vector<SZipFile> &Files, std::string &Error)
{
	// The zip keeps the top-level config directory, then the entry folders and their cfg
	// files: <config>/manager.txt, <config>/<entry>/init.cfg, ...
	std::function<bool(const std::string &, const std::string &)> CollectDir;
	CollectDir = [&](const std::string &RelativeDir, const std::string &ZipPrefix) -> bool {
		SDirListContext Context;
		ListDirectory(Storage(), IStorage::TYPE_SAVE, RelativeDir, Context);
		std::vector<size_t> vOrder(Context.m_vNames.size());
		for(size_t i = 0; i < vOrder.size(); i++)
			vOrder[i] = i;
		std::sort(vOrder.begin(), vOrder.end(), [&](size_t A, size_t B) {
			return str_comp_filenames(Context.m_vNames[A].c_str(), Context.m_vNames[B].c_str()) < 0;
		});
		for(const size_t i : vOrder)
		{
			const std::string &Name = Context.m_vNames[i];
			const std::string Relative = RelativeDir + "/" + Name;
			if(Context.m_vIsDir[i])
			{
				if(!CollectDir(Relative, ZipPrefix + "/" + Name))
					return false;
				continue;
			}
			void *pData = nullptr;
			unsigned Size = 0;
			if(!Storage()->ReadFile(Relative.c_str(), IStorage::TYPE_SAVE, &pData, &Size))
			{
				Error = "cannot read " + Relative;
				return false;
			}
			SZipFile File;
			File.m_Name = ZipPrefix + "/" + Name;
			File.m_Data.assign(static_cast<unsigned char *>(pData), static_cast<unsigned char *>(pData) + Size);
			free(pData);
			Files.push_back(std::move(File));
		}
		return true;
	};
	return CollectDir(ConfigDir(Config), Config);
}

bool CBindManager::ZipWrite(const std::vector<SZipFile> &Files, std::vector<unsigned char> &ZipData, std::string &Error)
{
	struct SCentral
	{
		std::string m_Name;
		uint32_t m_Crc;
		uint32_t m_CompSize;
		uint32_t m_UncompSize;
		uint32_t m_LocalOffset;
		uint16_t m_Method;
	};
	std::vector<SCentral> vCentral;
	ZipData.clear();
	for(const SZipFile &File : Files)
	{
		std::vector<unsigned char> Compressed;
		std::string DeflateError;
		if(!File.m_Data.empty() && !ZipDeflate(File.m_Data.data(), File.m_Data.size(), Compressed, DeflateError))
		{
			Error = DeflateError;
			return false;
		}
		const bool UseStore = File.m_Data.empty() || Compressed.size() >= File.m_Data.size();
		if(UseStore)
			Compressed = File.m_Data;
		SCentral Central;
		Central.m_Name = File.m_Name;
		Central.m_Crc = (uint32_t)crc32(0L, File.m_Data.empty() ? Z_NULL : File.m_Data.data(), (uInt)File.m_Data.size());
		Central.m_CompSize = (uint32_t)Compressed.size();
		Central.m_UncompSize = (uint32_t)File.m_Data.size();
		Central.m_LocalOffset = (uint32_t)ZipData.size();
		Central.m_Method = UseStore ? 0 : 8;
		if(Central.m_LocalOffset > 0xFFFFFFFFu || Central.m_CompSize > 0xFFFFFFFFu)
		{
			Error = "zip too large";
			return false;
		}
		WriteLE32(ZipData, ZIP_LOCAL_SIG);
		WriteLE16(ZipData, 20);
		WriteLE16(ZipData, 0);
		WriteLE16(ZipData, Central.m_Method);
		WriteLE16(ZipData, 0);
		WriteLE16(ZipData, 0);
		WriteLE32(ZipData, Central.m_Crc);
		WriteLE32(ZipData, Central.m_CompSize);
		WriteLE32(ZipData, Central.m_UncompSize);
		WriteLE16(ZipData, (uint16_t)Central.m_Name.size());
		WriteLE16(ZipData, 0);
		ZipData.insert(ZipData.end(), Central.m_Name.begin(), Central.m_Name.end());
		ZipData.insert(ZipData.end(), Compressed.begin(), Compressed.end());
		vCentral.push_back(std::move(Central));
	}

	const uint32_t CentralOffset = (uint32_t)ZipData.size();
	for(const SCentral &Central : vCentral)
	{
		WriteLE32(ZipData, ZIP_CENTRAL_SIG);
		WriteLE16(ZipData, 20);
		WriteLE16(ZipData, 20);
		WriteLE16(ZipData, 0);
		WriteLE16(ZipData, Central.m_Method);
		WriteLE16(ZipData, 0);
		WriteLE16(ZipData, 0);
		WriteLE32(ZipData, Central.m_Crc);
		WriteLE32(ZipData, Central.m_CompSize);
		WriteLE32(ZipData, Central.m_UncompSize);
		WriteLE16(ZipData, (uint16_t)Central.m_Name.size());
		WriteLE16(ZipData, 0);
		WriteLE16(ZipData, 0);
		WriteLE16(ZipData, 0);
		WriteLE16(ZipData, 0);
		WriteLE32(ZipData, 0);
		WriteLE32(ZipData, Central.m_LocalOffset);
		ZipData.insert(ZipData.end(), Central.m_Name.begin(), Central.m_Name.end());
	}
	const uint32_t CentralSize = (uint32_t)ZipData.size() - CentralOffset;
	WriteLE32(ZipData, ZIP_EOCD_SIG);
	WriteLE16(ZipData, 0);
	WriteLE16(ZipData, 0);
	WriteLE16(ZipData, (uint16_t)vCentral.size());
	WriteLE16(ZipData, (uint16_t)vCentral.size());
	WriteLE32(ZipData, CentralSize);
	WriteLE32(ZipData, CentralOffset);
	WriteLE16(ZipData, 0);
	return true;
}

bool CBindManager::ZipRead(const std::vector<unsigned char> &ZipData, std::vector<SZipFile> &Files, std::string &Error)
{
	Files.clear();
	if(ZipData.size() < 22)
	{
		Error = "zip 文件太小";
		return false;
	}
	size_t Eocd = SIZE_MAX;
	const size_t SearchStart = ZipData.size() > (22 + 65535) ? ZipData.size() - 22 - 65535 : 0;
	for(size_t i = ZipData.size() - 22;; i--)
	{
		if(ReadLE32(ZipData.data() + i) == ZIP_EOCD_SIG)
		{
			Eocd = i;
			break;
		}
		if(i == SearchStart)
			break;
	}
	if(Eocd == SIZE_MAX)
	{
		Error = "zip 缺少中央目录";
		return false;
	}
	const uint16_t NumEntries = ReadLE16(ZipData.data() + Eocd + 10);
	const uint32_t CentralSize = ReadLE32(ZipData.data() + Eocd + 12);
	const uint32_t CentralOffset = ReadLE32(ZipData.data() + Eocd + 16);
	if(NumEntries == 0xFFFF || CentralOffset == 0xFFFFFFFFu || CentralSize == 0xFFFFFFFFu)
	{
		Error = "不支持 Zip64";
		return false;
	}
	if(NumEntries > ZIP_MAX_FILES)
	{
		Error = "zip 文件数超过上限（4096）";
		return false;
	}
	if((size_t)CentralOffset + CentralSize > ZipData.size())
	{
		Error = "zip 中央目录越界";
		return false;
	}
	size_t Pos = CentralOffset;
	size_t TotalUncompressed = 0;
	for(uint16_t EntryIdx = 0; EntryIdx < NumEntries; EntryIdx++)
	{
		if(Pos + 46 > ZipData.size() || ReadLE32(ZipData.data() + Pos) != ZIP_CENTRAL_SIG)
		{
			Error = "zip 中央目录损坏";
			return false;
		}
		const uint16_t Flags = ReadLE16(ZipData.data() + Pos + 8);
		const uint16_t Method = ReadLE16(ZipData.data() + Pos + 10);
		const uint32_t Crc = ReadLE32(ZipData.data() + Pos + 16);
		const uint32_t CompSize = ReadLE32(ZipData.data() + Pos + 20);
		const uint32_t UncompSize = ReadLE32(ZipData.data() + Pos + 24);
		const uint16_t NameLen = ReadLE16(ZipData.data() + Pos + 28);
		if(UncompSize > ZIP_MAX_SINGLE_UNCOMPRESSED)
		{
			Error = "zip 单文件解压大小超过上限（64MB）";
			return false;
		}
		if(NameLen > ZIP_MAX_NAME_LENGTH)
		{
			Error = "zip 条目名过长";
			return false;
		}
		TotalUncompressed += UncompSize;
		if(TotalUncompressed > ZIP_MAX_TOTAL_UNCOMPRESSED)
		{
			Error = "zip 总解压大小超过上限（256MB）";
			return false;
		}
		const uint16_t ExtraLen = ReadLE16(ZipData.data() + Pos + 30);
		const uint16_t CommentLen = ReadLE16(ZipData.data() + Pos + 32);
		const uint32_t LocalOffset = ReadLE32(ZipData.data() + Pos + 42);
		if(Pos + 46 + NameLen + ExtraLen + CommentLen > ZipData.size())
		{
			Error = "zip 中央目录越界";
			return false;
		}
		std::string Name(reinterpret_cast<const char *>(ZipData.data() + Pos + 46), NameLen);
		Pos += 46 + NameLen + ExtraLen + CommentLen;
		if(Flags & 1)
		{
			Error = "不支持加密 zip";
			return false;
		}
		// task-91: common zip tools add directory entries (name ending in '/'); they are not
		// config files and must be skipped instead of being inflated/extracted.
		if(Name.empty() || Name.back() == '/')
			continue;
		if(LocalOffset + 30 > ZipData.size() || ReadLE32(ZipData.data() + LocalOffset) != ZIP_LOCAL_SIG)
		{
			Error = "zip 本地文件头损坏";
			return false;
		}
		const uint16_t LocalNameLen = ReadLE16(ZipData.data() + LocalOffset + 26);
		const uint16_t LocalExtraLen = ReadLE16(ZipData.data() + LocalOffset + 28);
		const size_t DataOffset = (size_t)LocalOffset + 30 + LocalNameLen + LocalExtraLen;
		if(DataOffset + CompSize > ZipData.size())
		{
			Error = "zip 文件数据越界";
			return false;
		}
		SZipFile File;
		File.m_Name = Name;
		if(Method == 0)
		{
			if(CompSize != UncompSize)
			{
				Error = "zip stored 条目大小不一致: " + Name;
				return false;
			}
			File.m_Data.assign(ZipData.begin() + DataOffset, ZipData.begin() + DataOffset + CompSize);
		}
		else if(Method == 8)
		{
			std::string InflateError;
			if(!ZipInflate(ZipData.data() + DataOffset, CompSize, UncompSize, File.m_Data, InflateError))
			{
				Error = InflateError + " (" + Name + ")";
				return false;
			}
			if(UncompSize == 0)
				BindManagerLog("zip entry accepted: empty file name=%s method=8", Name.c_str());
		}
		else
		{
			Error = "不支持的 zip 压缩方式";
			return false;
		}
		const uint32_t ActualCrc = (uint32_t)crc32(0L, File.m_Data.empty() ? Z_NULL : File.m_Data.data(), (uInt)File.m_Data.size());
		if(ActualCrc != Crc)
		{
			Error = "zip CRC 校验失败: " + Name;
			return false;
		}
		Files.push_back(std::move(File));
	}
	return true;
}

bool CBindManager::ExtractZipToConfig(const std::string &TopLevel, const std::vector<SZipFile> &Files, std::string &Error)
{
	// task-88 (verifier B1-R): extract into a temporary dot-directory (ignored by the config
	// list), check every IO result, and rename it to binds/<config> only on full success. Any
	// failure recursively removes the temp directory, so no partial config/files are left.
	std::string Config = TopLevel;
	if(!IsValidName(Config.c_str()))
	{
		Error = "zip 顶层目录名不合法: " + TopLevel;
		return false;
	}
	std::string Candidate = Config;
	int Suffix = 1;
	while(Storage()->FolderExists(ConfigDir(Candidate).c_str(), IStorage::TYPE_SAVE))
		Candidate = Config + "-" + std::to_string(Suffix++);
	Config = Candidate;

	std::string Temp = "binds/.tmp-import-" + std::to_string((long long)time_get()) + "-" + std::to_string(Suffix);
	while(Storage()->FolderExists(Temp.c_str(), IStorage::TYPE_SAVE))
		Temp += "-x";
	if(!EnsureSaveFolder(Temp))
	{
		Error = "无法创建临时配置目录";
		return false;
	}
	auto Fail = [&](const std::string &Message) -> bool {
		RemoveDirectoryRecursively(Temp);
		Error = Message;
		return false;
	};

	for(const SZipFile &File : Files)
	{
		const size_t Slash = File.m_Name.find('/');
		if(Slash == std::string::npos || Slash == 0)
			return Fail("zip 条目不在顶层目录内: " + File.m_Name);
		const std::string Relative = File.m_Name.substr(Slash + 1);
		if(Relative.empty())
			return Fail("zip 条目层级不合法: " + File.m_Name);
		std::vector<std::string> vParts;
		size_t PartStart = 0;
		while(true)
		{
			const size_t Next = Relative.find('/', PartStart);
			vParts.push_back(Relative.substr(PartStart, Next == std::string::npos ? std::string::npos : Next - PartStart));
			if(Next == std::string::npos)
				break;
			PartStart = Next + 1;
		}
		if(vParts.size() > 2 || vParts[0].empty() || (vParts.size() == 2 && vParts[1].empty()))
			return Fail("zip 条目层级不合法: " + File.m_Name);
		for(const std::string &Part : vParts)
		{
			if(!IsValidName(Part.c_str()))
				return Fail("zip 条目名不合法（Windows 非法字符/保留名）: " + File.m_Name);
		}
		std::string Target = Temp;
		if(vParts.size() == 2)
		{
			Target += "/" + vParts[0];
			if(!EnsureSaveFolder(Target))
				return Fail("无法创建条目目录: " + Target);
		}
		Target += (vParts.size() == 2 ? "/" : "/") + vParts.back();
		IOHANDLE Output = Storage()->OpenFile(Target.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!Output)
			return Fail("无法写入 " + Target);
		if(!File.m_Data.empty())
		{
			const unsigned Size = (unsigned)File.m_Data.size();
			if(io_write(Output, File.m_Data.data(), Size) != Size)
			{
				io_close(Output);
				return Fail("写入 zip 文件内容失败: " + File.m_Name);
			}
		}
		if(io_close(Output) != 0)
			return Fail("关闭 zip 文件失败: " + File.m_Name);
	}

	const std::string TempAbsolute = AbsoluteSavePath(Temp);
	const std::string TargetAbsolute = AbsoluteSavePath(ConfigDir(Config));
	if(fs_rename(TempAbsolute.c_str(), TargetAbsolute.c_str()) != 0)
		return Fail("无法将临时配置目录重命名为: " + Config);

	m_CurrentConfig = Config;
	str_copy(g_Config.m_RcBindManagerConfig, Config.c_str(), sizeof(g_Config.m_RcBindManagerConfig));
	RefreshConfigs();
	return true;
}

bool CBindManager::ImportZip(const char *pZipPath, std::string &Error)
{
	m_LastError.clear();
	if(!pZipPath || !pZipPath[0])
	{
		Error = "未选择 zip 文件";
		return false;
	}
	// task-87 (verifier B1): refuse oversized archives before loading them into memory.
	IOHANDLE Probe = Storage()->OpenFile(pZipPath, IOFLAG_READ, IStorage::TYPE_SAVE_OR_ABSOLUTE);
	if(!Probe)
	{
		Error = "无法打开 zip 文件";
		return false;
	}
	const int64_t ArchiveLength = io_length(Probe);
	io_close(Probe);
	if(ArchiveLength <= 0 || (uint64_t)ArchiveLength > ZIP_MAX_ARCHIVE_BYTES)
	{
		Error = "zip 文件过大或为空（上限 256MB）";
		return false;
	}
	void *pRaw = nullptr;
	unsigned Size = 0;
	if(!Storage()->ReadFile(pZipPath, IStorage::TYPE_SAVE_OR_ABSOLUTE, &pRaw, &Size))
	{
		Error = "无法读取 zip 文件";
		return false;
	}
	if(Size > ZIP_MAX_ARCHIVE_BYTES)
	{
		free(pRaw);
		Error = "zip 文件过大（上限 256MB）";
		return false;
	}
	std::vector<unsigned char> Data(static_cast<unsigned char *>(pRaw), static_cast<unsigned char *>(pRaw) + Size);
	free(pRaw);
	std::vector<SZipFile> Files;
	if(!ZipRead(Data, Files, Error))
		return false;
	if(Files.empty())
	{
		Error = "zip 为空";
		return false;
	}
	// Require exactly one top-level directory containing everything.
	std::string TopLevel;
	for(const SZipFile &File : Files)
	{
		const size_t Slash = File.m_Name.find('/');
		if(Slash == std::string::npos || Slash == 0)
		{
			Error = "zip 必须包含唯一顶层配置目录: " + File.m_Name;
			return false;
		}
		const std::string Top = File.m_Name.substr(0, Slash);
		if(TopLevel.empty())
			TopLevel = Top;
		else if(TopLevel != Top)
		{
			Error = "zip 包含多个顶层目录: " + TopLevel + ", " + Top;
			return false;
		}
		if(File.m_Name.find("..") != std::string::npos || File.m_Name.find('\\') != std::string::npos || File.m_Name.find(':') != std::string::npos)
		{
			Error = "zip 路径不合法: " + File.m_Name;
			return false;
		}
		// task-88 (verifier B1-R): reject Windows-invalid / reserved names before extracting.
		size_t PartStart = 0;
		while(true)
		{
			const size_t Next = File.m_Name.find('/', PartStart);
			const std::string Part = File.m_Name.substr(PartStart, Next == std::string::npos ? std::string::npos : Next - PartStart);
			if(Part.empty() || !IsValidName(Part.c_str()))
			{
				Error = "zip 条目名不合法（Windows 非法字符/保留名）: " + File.m_Name;
				return false;
			}
			if(Next == std::string::npos)
				break;
			PartStart = Next + 1;
		}
	}
	if(TopLevel.empty() || !IsValidName(TopLevel.c_str()))
	{
		Error = "zip 顶层目录名不合法";
		return false;
	}
	if(!ExtractZipToConfig(TopLevel, Files, Error))
		return false;
	if(!LoadConfigFromDisk(m_CurrentConfig))
		return false;
	return true;
}

bool CBindManager::ExportZip(const char *pZipPath, std::string &Error)
{
	m_LastError.clear();
	if(!pZipPath || !pZipPath[0])
	{
		Error = "未选择导出路径";
		return false;
	}
	if(m_CurrentConfig.empty())
	{
		Error = "没有当前配置";
		return false;
	}
	// Save GUI state first so the zip contains the visible entries/order.
	if(!SaveCurrentToDisk())
	{
		char aError[512];
		LastError(aError, sizeof(aError));
		Error = aError[0] ? aError : "保存配置失败";
		return false;
	}
	std::vector<SZipFile> Files;
	if(!CollectConfigFiles(m_CurrentConfig, Files, Error))
		return false;
	if(Files.empty())
	{
		Error = "配置目录为空";
		return false;
	}
	std::vector<unsigned char> ZipData;
	if(!ZipWrite(Files, ZipData, Error))
		return false;
	IOHANDLE Output = Storage()->OpenFile(pZipPath, IOFLAG_WRITE, IStorage::TYPE_SAVE_OR_ABSOLUTE);
	if(!Output)
	{
		Error = "无法写入 zip 文件";
		return false;
	}
	const unsigned ZipSize = (unsigned)ZipData.size();
	if(io_write(Output, ZipData.data(), ZipSize) != ZipSize)
	{
		io_close(Output);
		Error = "写入 zip 文件失败";
		return false;
	}
	if(io_close(Output) != 0)
	{
		Error = "关闭 zip 文件失败";
		return false;
	}
	return true;
}
