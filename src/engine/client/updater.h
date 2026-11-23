#ifndef ENGINE_CLIENT_UPDATER_H
#define ENGINE_CLIENT_UPDATER_H

#include <base/detect.h>

#define CLIENT_EXEC "DDNet"
#define SERVER_EXEC "DDNet-Server"

#if defined(CONF_FAMILY_WINDOWS)
#define PLAT_EXT ".exe"
#define PLAT_NAME CONF_PLATFORM_STRING
#elif defined(CONF_FAMILY_UNIX)
#define PLAT_EXT ""
#if defined(CONF_ARCH_IA32)
#define PLAT_NAME CONF_PLATFORM_STRING "-x86"
#elif defined(CONF_ARCH_AMD64)
#define PLAT_NAME CONF_PLATFORM_STRING "-x86_64"
#else
#define PLAT_NAME CONF_PLATFORM_STRING "-unsupported"
#endif
#else
#define PLAT_EXT ""
#define PLAT_NAME "unsupported-unsupported"
#endif

#define PLAT_CLIENT_DOWN CLIENT_EXEC "-" PLAT_NAME PLAT_EXT
#define PLAT_SERVER_DOWN SERVER_EXEC "-" PLAT_NAME PLAT_EXT

#define PLAT_CLIENT_EXEC CLIENT_EXEC PLAT_EXT
#define PLAT_SERVER_EXEC SERVER_EXEC PLAT_EXT

#include <base/lock.h>
#include <base/types.h>

#include <engine/updater.h>

#include <memory>

class IClient;
class IStorage;
class IHttp;
class IHttpRequest;

class CUpdater : public IUpdater
{
	enum class ETaskKind
	{
		NONE,
		FETCH_RELEASE,
		FETCH_FALLBACK,
		DOWNLOAD_ARCHIVE,
	};

	class IClient *m_pClient;
	class IStorage *m_pStorage;
	class IHttp *m_pHttp;

	CLock m_Lock;

	EUpdaterState m_State GUARDED_BY(m_Lock);
	char m_aStatus[256] GUARDED_BY(m_Lock);
	int m_Percent GUARDED_BY(m_Lock);

	std::shared_ptr<IHttpRequest> m_pCurrentTask;
	ETaskKind m_TaskKind = ETaskKind::NONE;
	bool m_AutoCheckPending = false;
	bool m_FallbackJsonTried = false;
	bool m_UsingFallbackDownload = false;
	long long m_ArchiveExpectedSize = 0;

	char m_aLatestVersion[64];
	char m_aArchiveName[128];
	char m_aArchiveUrl[2048];
	char m_aArchiveSha256[65];
	char m_aArchivePath[IO_MAX_PATH_LENGTH];

	void ResetTask();
	void StartReleaseFetch();
	void StartFallbackFetch();
	void ParseReleaseTask();
	void ParseFallbackTask();
	void StartArchiveDownload();
	void StartFallbackArchiveDownload();
	bool ValidateArchive();
	bool LaunchApplyScriptAndQuit();
	void SetCurrentState(EUpdaterState NewState);
	void SetStatus(const char *pStatus);
	void SetPercent(int Percent);
	bool MainMenuOpen();

public:
	CUpdater();

	EUpdaterState GetCurrentState() override;
	void GetCurrentFile(char *pBuf, int BufSize) override;
	int GetCurrentPercent() override;
	const char *GetLatestVersionString() override;

	void CheckForUpdate() override;
	void InitiateUpdate() override;
	void ApplyUpdateAndRestart() override;
	void Init();
	void Update() override;
};

bool RanbiVersionIsNewer(const char *pLatest, const char *pCurrent);

#endif
