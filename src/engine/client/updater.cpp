#include "updater.h"

#include <base/dbg.h>
#include <base/fs.h>
#include <base/hash.h>
#include <base/io.h>
#include <base/process.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/config.h>
#include <engine/external/json-parser/json.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/version.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

static constexpr const char *GITHUB_RELEASES_URL = "https://api.github.com/repos/Moistrocic/IntegratedClient/releases?per_page=10";
static constexpr const char *GITHUB_LATEST_RELEASE_URL = "https://github.com/Moistrocic/IntegratedClient/releases/latest";
static constexpr const char *FALLBACK_BASE_URL = "https://update.moiservice.top/IntegratedClient/";
static constexpr const char *FALLBACK_JSON_URL = "https://update.moiservice.top/IntegratedClient/update.json";
static constexpr const char *FALLBACK_VERSION_URL = "https://update.moiservice.top/IntegratedClient/version.txt";
#if defined(CONF_PLATFORM_LINUX)
static constexpr const char *UPDATE_ARCHIVE_PATH = "update/integratedclient-release.tar.xz";
#else
static constexpr const char *UPDATE_ARCHIVE_PATH = "update/integratedclient-release.zip";
#endif

bool RanbiVersionIsNewer(const char *pLatest, const char *pCurrent)
{
	auto Parse = [](const char *pVersion) {
		std::vector<int> v;
		int Cur = -1;
		if(pVersion == nullptr)
			return v;
		for(const unsigned char *p = (const unsigned char *)pVersion; *p != '\0'; ++p)
		{
			if(std::isdigit(*p))
			{
				if(Cur < 0)
					Cur = 0;
				Cur = Cur * 10 + (*p - '0');
			}
			else if(Cur >= 0)
			{
				v.push_back(Cur);
				Cur = -1;
			}
		}
		if(Cur >= 0)
			v.push_back(Cur);
		return v;
	};
	const std::vector<int> a = Parse(pLatest);
	const std::vector<int> b = Parse(pCurrent);
	const size_t Num = std::max(a.size(), b.size());
	for(size_t i = 0; i < Num; i++)
	{
		const int A = i < a.size() ? a[i] : 0;
		const int B = i < b.size() ? b[i] : 0;
		if(A != B)
			return A > B;
	}
	return false;
}

static void NormalizeVersion(const char *pVersion, char *pBuf, int BufSize)
{
	if(pVersion == nullptr)
	{
		if(BufSize > 0)
			pBuf[0] = '\0';
		return;
	}
	while(*pVersion != '\0' && std::isspace((unsigned char)*pVersion))
		pVersion++;
	if((pVersion[0] == 'v' || pVersion[0] == 'V') && std::isdigit((unsigned char)pVersion[1]))
		pVersion++;
	str_copy(pBuf, pVersion, BufSize);
}

static bool EndsWithNoCase(const char *pStr, const char *pSuffix)
{
	if(pStr == nullptr || pSuffix == nullptr)
		return false;
	const int Len = str_length(pStr);
	const int SuffixLen = str_length(pSuffix);
	if(SuffixLen > Len)
		return false;
	return str_comp_nocase(pStr + Len - SuffixLen, pSuffix) == 0;
}

static std::string Lower(const char *pStr)
{
	std::string Out;
	if(pStr == nullptr)
		return Out;
	for(const unsigned char *p = (const unsigned char *)pStr; *p != '\0'; ++p)
		Out.push_back((char)std::tolower(*p));
	return Out;
}

static int ScoreArchiveAsset(const char *pName)
{
	if(pName == nullptr)
		return -1;
	const std::string L = Lower(pName);
	if(L.find("integratedclient") == std::string::npos)
		return -1;
#if defined(CONF_FAMILY_WINDOWS)
	if(!EndsWithNoCase(pName, ".zip"))
		return -1;
	if(L.find("windows") == std::string::npos && L.find("win") == std::string::npos)
		return -1;
#elif defined(CONF_PLATFORM_LINUX)
	if(!EndsWithNoCase(pName, ".tar.xz"))
		return -1;
	if(L.find("linux") == std::string::npos)
		return -1;
#else
	return -1;
#endif
	if(L.find("debug") != std::string::npos || L.find("symbols") != std::string::npos || L.find("source") != std::string::npos)
		return -1;
	int Score = 100;
#if defined(CONF_FAMILY_WINDOWS)
	if(L.find("windows") != std::string::npos)
		Score += 50;
	if(L.find("x64") != std::string::npos || L.find("win64") != std::string::npos || L.find("amd64") != std::string::npos)
		Score += 20;
#elif defined(CONF_PLATFORM_LINUX)
	if(L.find("linux") != std::string::npos)
		Score += 50;
#endif
	return Score;
}

static const char *JsonString(const json_value *pJson, const char *pKey)
{
	if(pJson == nullptr || pJson->type != json_object)
		return nullptr;
	return json_string_get(json_object_get(pJson, pKey));
}

static bool ParseRelease(const json_value *pJson, char *pVersion, int VersionSize, char *pName, int NameSize, char *pUrl, int UrlSize, char *pSha, int ShaSize, long long *pSize)
{
	if(pJson == nullptr)
		return false;
	const json_value *pRelease = pJson;
	if(pJson->type == json_array)
	{
		const json_value *pBest = nullptr;
		for(int i = 0; i < json_array_length(pJson); i++)
		{
			const json_value *pCur = json_array_get(pJson, i);
			const json_value *pDraft = json_object_get(pCur, "draft");
			const json_value *pPrerelease = json_object_get(pCur, "prerelease");
			if((pDraft != nullptr && pDraft->type == json_boolean && json_boolean_get(pDraft) != 0) ||
				(pPrerelease != nullptr && pPrerelease->type == json_boolean && json_boolean_get(pPrerelease) != 0))
				continue;
			const char *pTag = JsonString(pCur, "tag_name");
			if(pTag == nullptr)
				continue;
			if(pBest == nullptr || RanbiVersionIsNewer(pTag, JsonString(pBest, "tag_name")))
				pBest = pCur;
		}
		pRelease = pBest;
	}
	if(pRelease == nullptr || pRelease->type != json_object)
		return false;
	{
		const json_value *pDraft = json_object_get(pRelease, "draft");
		const json_value *pPrerelease = json_object_get(pRelease, "prerelease");
		if((pDraft != nullptr && pDraft->type == json_boolean && json_boolean_get(pDraft) != 0) ||
			(pPrerelease != nullptr && pPrerelease->type == json_boolean && json_boolean_get(pPrerelease) != 0))
			return false;
	}
	const char *pTag = JsonString(pRelease, "tag_name");
	if(pTag == nullptr)
		pTag = JsonString(pRelease, "name");
	if(pTag == nullptr)
		return false;
	const json_value *pAssets = json_object_get(pRelease, "assets");
	if(pAssets == nullptr || pAssets->type != json_array)
		return false;
	int Best = -1;
	char aBestName[128] = "";
	char aBestUrl[2048] = "";
	char aBestSha[65] = "";
	long long BestSize = 0;
	for(int i = 0; i < json_array_length(pAssets); i++)
	{
		const json_value *pAsset = json_array_get(pAssets, i);
		const char *pAssetName = JsonString(pAsset, "name");
		const char *pAssetUrl = JsonString(pAsset, "browser_download_url");
		const int Score = ScoreArchiveAsset(pAssetName);
		if(pAssetUrl != nullptr && Score > Best)
		{
			Best = Score;
			str_copy(aBestName, pAssetName, sizeof(aBestName));
			str_copy(aBestUrl, pAssetUrl, sizeof(aBestUrl));
			aBestSha[0] = '\0';
			const char *pDigest = JsonString(pAsset, "digest");
			if(pDigest != nullptr && str_startswith(pDigest, "sha256:"))
				str_copy(aBestSha, pDigest + str_length("sha256:"), sizeof(aBestSha));
			const json_value *pSize = json_object_get(pAsset, "size");
			BestSize = pSize != nullptr && pSize->type == json_integer ? (long long)json_int_get(pSize) : 0;
		}
	}
	if(Best < 0)
		return false;
	NormalizeVersion(pTag, pVersion, VersionSize);
	str_copy(pName, aBestName, NameSize);
	str_copy(pUrl, aBestUrl, UrlSize);
	if(pSha != nullptr)
		str_copy(pSha, aBestSha, ShaSize);
	if(pSize != nullptr)
		*pSize = BestSize;
	return true;
}

CUpdater::CUpdater()
{
	m_pClient = nullptr;
	m_pStorage = nullptr;
	m_pHttp = nullptr;
	m_State = CLEAN;
	m_aStatus[0] = '\0';
	m_Percent = 0;
	m_aLatestVersion[0] = '\0';
	m_aArchiveName[0] = '\0';
	m_aArchiveUrl[0] = '\0';
	m_aArchiveSha256[0] = '\0';
	m_aGithubArchiveName[0] = '\0';
	m_aGithubArchiveUrl[0] = '\0';
	m_aGithubArchiveSha256[0] = '\0';
	str_copy(m_aArchivePath, UPDATE_ARCHIVE_PATH, sizeof(m_aArchivePath));
}

void CUpdater::Init()
{
	m_pClient = Kernel()->RequestInterface<IClient>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pHttp = Kernel()->RequestInterface<IHttp>();
#if !defined(CONF_HEADLESS_CLIENT)
	m_AutoCheckPending = true;
#endif
}

void CUpdater::SetCurrentState(EUpdaterState NewState)
{
	const CLockScope Lock(m_Lock);
	m_State = NewState;
}

void CUpdater::SetStatus(const char *pStatus)
{
	const CLockScope Lock(m_Lock);
	str_copy(m_aStatus, pStatus ? pStatus : "", sizeof(m_aStatus));
}

void CUpdater::SetPercent(int Percent)
{
	const CLockScope Lock(m_Lock);
	m_Percent = std::clamp(Percent, 0, 100);
}

IUpdater::EUpdaterState CUpdater::GetCurrentState()
{
	const CLockScope Lock(m_Lock);
	return m_State;
}

void CUpdater::GetCurrentFile(char *pBuf, int BufSize)
{
	const CLockScope Lock(m_Lock);
	str_copy(pBuf, m_aStatus, BufSize);
}

const char *CUpdater::GetDownloadSource()
{
	return m_UsingFallbackDownload ? "update.moiservice.top" : "GitHub";
}

bool CUpdater::IsUsingFallbackDownload()
{
	return m_UsingFallbackDownload;
}

bool CUpdater::IsDownloadFailed()
{
	return m_DownloadFailed;
}

bool CUpdater::CanSwitchDownloadSource()
{
	return m_aGithubArchiveUrl[0] != '\0' && (GetCurrentState() == DOWNLOADING || GetCurrentState() == FAIL);
}

void CUpdater::RetryDownload()
{
	const EUpdaterState State = GetCurrentState();
	if(State != FAIL || !m_DownloadFailed)
		return;
	m_DownloadFailed = false;
	ResetTask();
	if(m_aArchiveUrl[0] == '\0')
	{
		CheckForUpdate();
		return;
	}
	StartArchiveDownload();
}

void CUpdater::SwitchDownloadSource()
{
	const EUpdaterState State = GetCurrentState();
	if(State != DOWNLOADING && State != FAIL)
		return;
	if(m_aGithubArchiveUrl[0] == '\0')
		return;
	ResetTask();
	SetDownloadSource(!m_UsingFallbackDownload);
	SetStatus(m_UsingFallbackDownload ? "Trying fallback download" : "Downloading from GitHub");
	StartArchiveDownload();
}

int CUpdater::GetCurrentPercent()
{
	const CLockScope Lock(m_Lock);
	return m_Percent;
}

const char *CUpdater::GetLatestVersionString()
{
	return m_aLatestVersion;
}

void CUpdater::ResetTask()
{
	if(m_pCurrentTask)
	{
		m_pCurrentTask->Abort();
		m_pCurrentTask = nullptr;
	}
	m_TaskKind = ETaskKind::NONE;
}

bool CUpdater::MainMenuOpen()
{
	IGameClient *pGameClient = Kernel()->RequestInterface<IGameClient>();
	if(pGameClient == nullptr)
		return false;
	return static_cast<CGameClient *>(pGameClient)->m_Menus.IsMainMenuOpen();
}

void CUpdater::StartReleaseFetch()
{
	ResetTask();
	SetStatus("Checking GitHub release");
	SetPercent(0);
	SetCurrentState(GETTING_MANIFEST);
	m_FallbackJsonTried = false;
	char aUrl[320];
	str_format(aUrl, sizeof(aUrl), "%s&t=%lld", GITHUB_RELEASES_URL, (long long)time_timestamp());
	m_TaskKind = ETaskKind::FETCH_RELEASE;
	m_pCurrentTask = HttpGet(aUrl);
	m_pCurrentTask->HeaderString("Accept", "application/vnd.github+json");
	m_pCurrentTask->HeaderString("User-Agent", INTEGRATEDCLIENT_NAME);
	m_pCurrentTask->HeaderString("X-GitHub-Api-Version", "2022-11-28");
	m_pCurrentTask->HeaderString("Cache-Control", "no-cache");
	m_pCurrentTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pCurrentTask->IpResolve(IPRESOLVE::V4);
	m_pHttp->Run(m_pCurrentTask);
}

void CUpdater::StartFallbackFetch()
{
	ResetTask();
	SetStatus(m_FallbackJsonTried ? "Checking fallback version.txt" : "Checking fallback update.json");
	SetPercent(0);
	SetCurrentState(GETTING_MANIFEST);
	m_TaskKind = ETaskKind::FETCH_FALLBACK;
	m_pCurrentTask = HttpGet(m_FallbackJsonTried ? FALLBACK_VERSION_URL : FALLBACK_JSON_URL);
	m_pCurrentTask->HeaderString("User-Agent", INTEGRATEDCLIENT_NAME);
	m_pCurrentTask->HeaderString("Cache-Control", "no-cache");
	m_pCurrentTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pCurrentTask->IpResolve(IPRESOLVE::V4);
	m_pHttp->Run(m_pCurrentTask);
}

void CUpdater::ParseReleaseTask()
{
	json_value *pJson = m_pCurrentTask ? m_pCurrentTask->ResultJson() : nullptr;
	char aVersion[64] = "";
	char aName[128] = "";
	char aUrl[2048] = "";
	char aSha[65] = "";
	long long AssetSize = 0;
	const bool Parsed = pJson != nullptr && ParseRelease(pJson, aVersion, sizeof(aVersion), aName, sizeof(aName), aUrl, sizeof(aUrl), aSha, sizeof(aSha), &AssetSize);
	if(pJson != nullptr)
		json_value_free(pJson);
	if(!Parsed)
	{
		StartFallbackFetch();
		return;
	}
	if(!RanbiVersionIsNewer(aVersion, INTEGRATEDCLIENT_VERSION))
	{
		m_aLatestVersion[0] = '\0';
		m_aArchiveName[0] = '\0';
		m_aArchiveUrl[0] = '\0';
		SetStatus("No update available");
		SetCurrentState(CLEAN);
		return;
	}
	str_copy(m_aLatestVersion, aVersion, sizeof(m_aLatestVersion));
	str_copy(m_aGithubArchiveName, aName, sizeof(m_aGithubArchiveName));
	str_copy(m_aGithubArchiveUrl, aUrl, sizeof(m_aGithubArchiveUrl));
	str_copy(m_aGithubArchiveSha256, aSha, sizeof(m_aGithubArchiveSha256));
	m_GithubArchiveExpectedSize = AssetSize;
	SetDownloadSource(false);
	SetStatus("Update available");
	SetCurrentState(VERSION_AVAILABLE);
}

void CUpdater::ParseFallbackTask()
{
	const bool VersionTxt = m_FallbackJsonTried;
	char aVersion[64] = "";
	char aName[128] = "";
	char aUrl[2048] = "";
	char aSha[65] = "";
	bool Parsed = false;
	if(!VersionTxt)
	{
		json_value *pJson = m_pCurrentTask ? m_pCurrentTask->ResultJson() : nullptr;
		if(pJson != nullptr)
		{
			const char *pVersion = JsonString(pJson, "version");
			const char *pName = JsonString(pJson, "asset_name");
			const char *pUrl = JsonString(pJson, "asset_url");
			const char *pSha = JsonString(pJson, "sha256");
			const json_value *pSizeJson = json_object_get(pJson, "size");
			const long long AssetSize = pSizeJson != nullptr && pSizeJson->type == json_integer ? (long long)json_int_get(pSizeJson) : 0;
			if(pVersion != nullptr && pUrl != nullptr)
			{
				NormalizeVersion(pVersion, aVersion, sizeof(aVersion));
				str_copy(aName, pName != nullptr ? pName : "IntegratedClient-windows.zip", sizeof(aName));
				str_copy(aUrl, pUrl, sizeof(aUrl));
				if(pSha != nullptr)
					str_copy(aSha, pSha, sizeof(aSha));
				m_ArchiveExpectedSize = AssetSize;
				Parsed = true;
			}
			json_value_free(pJson);
		}
		if(!Parsed)
		{
			m_FallbackJsonTried = true;
			StartFallbackFetch();
			return;
		}
	}
	else
	{
		unsigned char *pData = nullptr;
		size_t DataSize = 0;
		if(m_pCurrentTask)
			m_pCurrentTask->Result(&pData, &DataSize);
		if(pData != nullptr && DataSize > 0)
		{
			char aText[256];
			const size_t Copy = std::min(DataSize, sizeof(aText) - 1);
			mem_copy(aText, pData, Copy);
			aText[Copy] = '\0';
			char aFirstLine[128];
			const char *pNL = str_find(aText, "\n");
			const int Len = pNL ? (int)(pNL - aText) : str_length(aText);
			if(Len > 0 && Len < (int)sizeof(aFirstLine))
			{
				mem_copy(aFirstLine, aText, Len);
				aFirstLine[Len] = '\0';
				NormalizeVersion(aFirstLine, aVersion, sizeof(aVersion));
				str_copy(aName, "IntegratedClient-windows.zip", sizeof(aName));
				str_format(aUrl, sizeof(aUrl), "%sIntegratedClient-windows.zip", FALLBACK_BASE_URL);
				m_ArchiveExpectedSize = 0;
				Parsed = true;
			}
		}
	}
	if(!Parsed)
	{
		SetStatus("Update check failed");
		SetCurrentState(FAIL);
		return;
	}
	if(!RanbiVersionIsNewer(aVersion, INTEGRATEDCLIENT_VERSION))
	{
		m_aLatestVersion[0] = '\0';
		m_aArchiveName[0] = '\0';
		m_aArchiveUrl[0] = '\0';
		SetStatus("No update available");
		SetCurrentState(CLEAN);
		return;
	}
	str_copy(m_aLatestVersion, aVersion, sizeof(m_aLatestVersion));
	str_copy(m_aArchiveName, aName, sizeof(m_aArchiveName));
	str_copy(m_aArchiveUrl, aUrl, sizeof(m_aArchiveUrl));
	str_copy(m_aArchiveSha256, aSha, sizeof(m_aArchiveSha256));
	m_UsingFallbackDownload = true;
	m_DownloadFailed = false;
	SetStatus("Update available (fallback)");
	SetCurrentState(VERSION_AVAILABLE);
}

void CUpdater::SetDownloadSource(bool Fallback)
{
	m_UsingFallbackDownload = Fallback;
	m_DownloadFailed = false;
	if(Fallback)
	{
		str_copy(m_aArchiveName, "IntegratedClient-windows.zip", sizeof(m_aArchiveName));
		str_format(m_aArchiveUrl, sizeof(m_aArchiveUrl), "%s%s", FALLBACK_BASE_URL, "IntegratedClient-windows.zip");
		m_ArchiveExpectedSize = 0;
		m_aArchiveSha256[0] = '\0';
	}
	else
	{
		str_copy(m_aArchiveName, m_aGithubArchiveName, sizeof(m_aArchiveName));
		str_copy(m_aArchiveUrl, m_aGithubArchiveUrl, sizeof(m_aArchiveUrl));
		str_copy(m_aArchiveSha256, m_aGithubArchiveSha256, sizeof(m_aArchiveSha256));
		m_ArchiveExpectedSize = m_GithubArchiveExpectedSize;
	}
}

void CUpdater::StartFallbackArchiveDownload()
{
	if(m_UsingFallbackDownload)
		return;
	SetDownloadSource(true);
	SetStatus("Trying fallback download");
	StartArchiveDownload();
}

bool CUpdater::ValidateArchive()
{
	IOHANDLE File = io_open(m_aArchivePath, IOFLAG_READ);
	if(!File)
	{
		dbg_msg("updater", "validate: archive missing: %s", m_aArchivePath);
		SetStatus("Downloaded archive missing");
		return false;
	}
	const long long Size = io_length(File);
	io_close(File);
	dbg_msg("updater", "validate: archive=%s size=%lld expected=%lld sha=%s fallback=%d", m_aArchivePath, Size, m_ArchiveExpectedSize, m_aArchiveSha256[0] ? m_aArchiveSha256 : "-", m_UsingFallbackDownload ? 1 : 0);
	// GitHub reports both digest and size. Prefer the digest: release assets can be
	// replaced under the same name while the API's size metadata is cached for a while.
	if(Size <= 0 || (m_aArchiveSha256[0] == '\0' && m_ArchiveExpectedSize > 0 && Size != m_ArchiveExpectedSize))
	{
		dbg_msg("updater", "validate: size mismatch actual=%lld expected=%lld sha=%s", Size, m_ArchiveExpectedSize, m_aArchiveSha256[0] ? m_aArchiveSha256 : "-");
		SetStatus("Downloaded archive size mismatch");
		return false;
	}
	if(m_aArchiveSha256[0] != '\0')
	{
		SHA256_DIGEST Sha;
		SHA256_CTX Ctx;
		sha256_init(&Ctx);
		File = io_open(m_aArchivePath, IOFLAG_READ);
		if(!File)
			return false;
		unsigned char aBuffer[64 * 1024];
		while(true)
		{
			unsigned Bytes = io_read(File, aBuffer, sizeof(aBuffer));
			if(Bytes == 0)
				break;
			sha256_update(&Ctx, aBuffer, Bytes);
		}
		io_close(File);
		Sha = sha256_finish(&Ctx);
		char aSha[SHA256_MAXSTRSIZE];
		sha256_str(Sha, aSha, sizeof(aSha));
		if(str_comp_nocase(aSha, m_aArchiveSha256) != 0)
		{
			dbg_msg("updater", "validate: sha256 mismatch actual=%s expected=%s", aSha, m_aArchiveSha256);
			SetStatus("Downloaded archive sha256 mismatch");
			return false;
		}
	}
	return true;
}

void CUpdater::StartArchiveDownload()
{
	ResetTask();
	// Resolve the archive against the executable directory (not the process CWD)
	// and make sure the update folder exists before the HTTP writer opens it.
	// Include the process ID: quick-practice/dummy clients share the same install
	// directory, and a shared archive path lets them delete/truncate each other's
	// downloads (observed as a 0-byte archive after a successful HTTP request).
	char aRelativeArchive[IO_MAX_PATH_LENGTH];
#if defined(CONF_PLATFORM_LINUX)
	str_format(aRelativeArchive, sizeof(aRelativeArchive), "update/integratedclient-release.%d.tar.xz", (int)process_id());
#else
	str_format(aRelativeArchive, sizeof(aRelativeArchive), "update/integratedclient-release.%d.zip", (int)process_id());
#endif
	m_pStorage->GetBinaryPath(aRelativeArchive, m_aArchivePath, sizeof(m_aArchivePath));
	char aUpdateDir[IO_MAX_PATH_LENGTH];
	m_pStorage->GetBinaryPath("update", aUpdateDir, sizeof(aUpdateDir));
	if(fs_makedir(aUpdateDir) != 0)
	{
		// Directory likely already exists; HTTP open reports any real failure.
	}
	if(fs_remove(m_aArchivePath) != 0)
	{
		// No previous archive to remove.
	}
	SetStatus(m_aArchiveName[0] != '\0' ? m_aArchiveName : "update");
	SetPercent(0);
	m_DownloadFailed = false;
	SetCurrentState(DOWNLOADING);
	m_TaskKind = ETaskKind::DOWNLOAD_ARCHIVE;
	dbg_msg("updater", "download: url=%s path=%s fallback=%d", m_aArchiveUrl, m_aArchivePath, m_UsingFallbackDownload ? 1 : 0);
	// TYPE_ABSOLUTE (-2) makes the HTTP writer resolve the path against the binary
	// directory, so it must receive the relative archive path even though
	// m_aArchivePath is stored as an absolute path for validation/apply.
	m_pCurrentTask = HttpGetFile(m_aArchiveUrl, m_pStorage, aRelativeArchive, IStorage::TYPE_ABSOLUTE);
	m_pCurrentTask->HeaderString("User-Agent", INTEGRATEDCLIENT_NAME);
	m_pCurrentTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pCurrentTask->IpResolve(IPRESOLVE::V4);
	m_pHttp->Run(m_pCurrentTask);
}

bool CUpdater::LaunchApplyScriptAndQuit()
{
#if defined(CONF_FAMILY_WINDOWS) || defined(CONF_PLATFORM_LINUX)
	char aUpdaterPath[IO_MAX_PATH_LENGTH];
	char aInstallDir[IO_MAX_PATH_LENGTH];
	char aExePath[IO_MAX_PATH_LENGTH];
	char aPid[32];
	if(!m_pStorage->FileExists(m_aArchivePath, IStorage::TYPE_ABSOLUTE))
	{
		SetStatus("Downloaded archive missing");
		return false;
	}
#if defined(CONF_FAMILY_WINDOWS)
	m_pStorage->GetBinaryPathAbsolute("integratedclient-updater.exe", aUpdaterPath, sizeof(aUpdaterPath));
#else
	m_pStorage->GetBinaryPathAbsolute("integratedclient-updater", aUpdaterPath, sizeof(aUpdaterPath));
#endif
	m_pStorage->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, aExePath, sizeof(aExePath));
	str_copy(aInstallDir, aExePath, sizeof(aInstallDir));
	for(int i = str_length(aInstallDir) - 1; i >= 0; i--)
	{
		if(aInstallDir[i] == '/' || aInstallDir[i] == '\\')
		{
			aInstallDir[i] = '\0';
			break;
		}
	}
	str_format(aPid, sizeof(aPid), "%d", process_id());
	const char *apArgs[] = {aPid, m_aArchivePath, aInstallDir, aExePath};
	dbg_msg("updater", "apply: updater=%s archive=%s install=%s exe=%s", aUpdaterPath, m_aArchivePath, aInstallDir, aExePath);
	if(process_execute(aUpdaterPath, EShellExecuteWindowState::FOREGROUND, apArgs, 4) == INVALID_PROCESS)
	{
		dbg_msg("updater", "apply: process_execute failed for %s", aUpdaterPath);
		SetStatus("Failed to launch updater");
		return false;
	}
	m_pClient->Quit();
	return true;
#else
	SetStatus("Updater unavailable on this platform");
	return false;
#endif
}

void CUpdater::CheckForUpdate()
{
	const EUpdaterState State = GetCurrentState();
	if(State == GETTING_MANIFEST || State == DOWNLOADING)
		return;
	m_aLatestVersion[0] = '\0';
	m_aArchiveName[0] = '\0';
	m_aArchiveUrl[0] = '\0';
	m_aArchiveSha256[0] = '\0';
	m_aGithubArchiveName[0] = '\0';
	m_aGithubArchiveUrl[0] = '\0';
	m_aGithubArchiveSha256[0] = '\0';
	m_GithubArchiveExpectedSize = 0;
	m_ArchiveExpectedSize = 0;
	m_UsingFallbackDownload = false;
	m_DownloadFailed = false;
	StartReleaseFetch();
}

void CUpdater::InitiateUpdate()
{
	const EUpdaterState State = GetCurrentState();
	if(State == GETTING_MANIFEST || State == DOWNLOADING)
		return;
	if(State == VERSION_AVAILABLE || State == FAIL)
	{
		if(m_aArchiveUrl[0] != '\0')
		{
			StartArchiveDownload();
			return;
		}
		CheckForUpdate();
		return;
	}
	if(State == NEED_RESTART)
		ApplyUpdateAndRestart();
}

void CUpdater::ApplyUpdateAndRestart()
{
	if(GetCurrentState() != NEED_RESTART || !MainMenuOpen())
		return;
	if(!LaunchApplyScriptAndQuit())
		SetCurrentState(FAIL);
}

void CUpdater::Update()
{
	if(g_Config.m_RcAutoUpdate != 0)
	{
		const EUpdaterState State = GetCurrentState();
		if(State == VERSION_AVAILABLE)
			InitiateUpdate();
		else if(State == NEED_RESTART && MainMenuOpen())
			ApplyUpdateAndRestart();
	}
	if(m_AutoCheckPending && m_pHttp && GetCurrentState() == CLEAN)
	{
		m_AutoCheckPending = false;
		CheckForUpdate();
	}
	if(!m_pCurrentTask)
		return;
	if(!m_pCurrentTask->Done())
	{
		if(GetCurrentState() == DOWNLOADING)
			SetPercent(m_pCurrentTask->Progress());
		return;
	}
	if(m_pCurrentTask->State() != EHttpState::DONE || m_pCurrentTask->StatusCode() >= 400)
	{
		const ETaskKind TaskKind = m_TaskKind;
		ResetTask();
		if(TaskKind == ETaskKind::FETCH_RELEASE)
		{
			StartFallbackFetch();
			return;
		}
		if(TaskKind == ETaskKind::FETCH_FALLBACK && !m_FallbackJsonTried)
		{
			m_FallbackJsonTried = true;
			StartFallbackFetch();
			return;
		}
		if(TaskKind == ETaskKind::DOWNLOAD_ARCHIVE)
		{
			// Do not silently switch sources: the user can retry or choose the
			// fallback server from the update dialog.
			m_DownloadFailed = true;
			SetStatus("Download failed. Retry or switch download source.");
			SetCurrentState(FAIL);
			return;
		}
		SetStatus("Update check failed");
		SetCurrentState(FAIL);
		return;
	}
	if(m_TaskKind == ETaskKind::FETCH_RELEASE)
	{
		ParseReleaseTask();
		ResetTask();
		return;
	}
	if(m_TaskKind == ETaskKind::FETCH_FALLBACK)
	{
		ParseFallbackTask();
		ResetTask();
		return;
	}
	if(m_TaskKind == ETaskKind::DOWNLOAD_ARCHIVE)
	{
		if(!ValidateArchive())
		{
			ResetTask();
			m_DownloadFailed = true;
			SetStatus("Downloaded update package is invalid. Retry or switch download source.");
			SetCurrentState(FAIL);
			return;
		}
		ResetTask();
		m_DownloadFailed = false;
		SetPercent(100);
		SetStatus(m_aArchiveName[0] != '\0' ? m_aArchiveName : "update");
		SetCurrentState(NEED_RESTART);
	}
}
