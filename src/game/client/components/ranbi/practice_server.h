#ifndef GAME_CLIENT_COMPONENTS_RANBI_PRACTICE_SERVER_H
#define GAME_CLIENT_COMPONENTS_RANBI_PRACTICE_SERVER_H

#include <base/hash.h>
#include <base/process.h>
#include <base/types.h>

#include <engine/shared/config.h>

#include <game/client/component.h>

#include <chrono>

// task-84 (M0): lifecycle for the local DDNet-Server used by the planned dual-server
// practice mode. This component only starts/stops the local server, copies the current main
// map to a temporary `maps/ranbi_practice_*.map` and exposes the process/port/rcon/map
// information. It does not connect a practice client yet (that is M2).
class CRanbiFastPractice : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnStateChange(int NewState, int OldState) override;
	void OnReset() override;
	void OnUpdate() override;
	void OnShutdown() override;
	void OnMapLoad() override;

	// M2 API: a future practice client can connect to 127.0.0.1:Port(), validate the map with
	// MapSha256(), use MapName()/MapPath(), and authenticate rcon with RconPassword().
	bool IsServerRunning() const;
	bool IsServerStarting() const;
	int Port() const { return m_Port; }
	const char *MapName() const { return m_aMapName; }
	const char *MapPath() const { return m_aMapPath; }
	const char *RconPassword() const { return m_aRconPassword; }
	const char *LastError() const { return m_aLastError; }
	const SHA256_DIGEST &MapSha256() const { return m_MapSha; }

	void RelayChatLine(int ClientId, const char *pLine);

private:
	void TryStartServer();
	void CaptureGuestStartData();
	bool WriteGuestSyncFile();
	void CleanupGuestSyncFile();
	void StartClientB();
	void StopClientB();
	void InitChatFiles();
	void CleanupChatFiles();
	void PollHostChat();
	void ReapClientB();
	bool ClientBAlive() const;
	bool PrepareMapCopy();
	bool StartServerProcess();
	void StopServer(bool Shutdown = false);
	bool CleanupTempFiles(int Attempts);
	void RetryPendingCleanup();
	bool RemovePathWithRetry(const char *pPath, int Attempts);
	bool StopProcess();
	void ReapExitedProcess();
	void CleanupStalePidFiles();
	void UpdatePort();
	bool ProcessAlive() const;
	void SetError(const char *pError);
	void CloseJobObject();

	bool m_Online = false;
	bool m_WasEnabled = false;
	bool m_StartAttempted = false;
	bool m_WaitingForPort = false;
	bool m_KillFailed = false;
	bool m_CleanupPending = false;
	int m_Port = 0;
	std::chrono::nanoseconds m_StartTime{0};
	std::chrono::nanoseconds m_NextPortCheck{0};
	std::chrono::nanoseconds m_CleanupRetryTime{0};
	char m_aPortFile[128] = "";
	char m_aMapName[160] = "";
	char m_aMapPath[IO_MAX_PATH_LENGTH] = "";
	char m_aMapCfgPath[IO_MAX_PATH_LENGTH] = "";
	char m_aRconPassword[sizeof(g_Config.m_SvRconPassword)] = "";
	char m_aLastError[512] = "";
	SHA256_DIGEST m_MapSha = {};
	vec2 m_GuestBodyPos = vec2(0.0f, 0.0f);
	vec2 m_GuestDummyPos = vec2(0.0f, 0.0f);
	bool m_GuestHasDummy = false;
	bool m_ClientBStarted = false;
	bool m_StoppingClientB = false;
	char m_aChatA2B[IO_MAX_PATH_LENGTH] = "";
	char m_aChatB2A[IO_MAX_PATH_LENGTH] = "";
	std::chrono::nanoseconds m_NextChatPoll{0};
	char m_aGuestBodyState[768] = "";
	char m_aGuestDummyState[768] = "";
	char m_aGuestSyncFile[IO_MAX_PATH_LENGTH] = "";

#if !defined(CONF_PLATFORM_ANDROID)
	PROCESS m_Process = INVALID_PROCESS;
	PROCESS m_ClientBProcess = INVALID_PROCESS;
#endif
	void *m_pJobObject = nullptr;
	void *m_pClientBJobObject = nullptr;
};

#endif
