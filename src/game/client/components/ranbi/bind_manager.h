#ifndef GAME_CLIENT_COMPONENTS_RANBI_BIND_MANAGER_H
#define GAME_CLIENT_COMPONENTS_RANBI_BIND_MANAGER_H

/* ranbi: Bind Manager (按键管理) component for IntegratedClient.
 *
 * Data (in the game save directory):
 *   binds/<config>/manager.txt          entry order/enabled/key/init/commands metadata
 *   binds/<config>/<entry>/init.cfg     initialization commands
 *   binds/<config>/<entry>/bind.cfg     initial bind (executed manually when auto-init is off)
 *   binds/<config>/<entry>/step1..N.cfg command i + bind to step i+1 (last binds step1)
 *
 * On entering a server with rc_bind_manager_init=1 the enabled entries are executed in
 * manager.txt order: init.cfg then step1.cfg (command 1 runs immediately, key is bound to
 * step2; each key press advances until the last step binds back to step1).
 */

#include <engine/console.h>
#include <engine/keys.h>
#include <engine/storage.h>

#include <game/client/component.h>

#include <string>
#include <utility>
#include <vector>

class CBindManager : public CComponent
{
public:
	// task-93: default message color (same value used by cl_message_client_color / task).
	static constexpr unsigned int DEFAULT_COMMAND_COLOR = 14417855U;

	struct SEntry
	{
		std::string m_Name; // also the entry folder name (备注)
		bool m_Enabled = true;
		bool m_Expanded = false;
		int m_Key = KEY_UNKNOWN;
		int m_ModifierMask = 0;
		std::string m_Init;
		std::vector<std::string> m_vCommands;
		// task-93: per-command message colors (packed HSLA, as used by DoLine_ColorPicker and
		// cl_message_client_color); version 1 manager.txt entries default to DEFAULT_COMMAND_COLOR.
		unsigned int m_InitColor = DEFAULT_COMMAND_COLOR;
		std::vector<unsigned int> m_vCommandColors;
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnStateChange(int NewState, int OldState) override;
	void OnReset() override;

	// Configuration list / selection.
	void RefreshConfigs();
	const std::vector<std::string> &Configs() const { return m_vConfigs; }
	const char *CurrentConfig() const { return m_CurrentConfig.c_str(); }
	bool SelectConfig(const char *pName, bool LoadFromDisk);
	bool LoadCurrent();
	bool CreateConfig();
	bool DeleteConfig(const char *pName, std::string &Error);
	bool SaveCurrent();
	bool RenameConfig(const char *pNewName);

	// Entry list (manager.txt order).
	std::vector<SEntry> &Entries() { return m_vEntries; }
	const std::vector<SEntry> &Entries() const { return m_vEntries; }
	int EntriesNamingIndex() const { return -1; }
	bool RenameEntry(int Index, const char *pNewName);
	void AddEntry();
	void RemoveEntry(int Index);
	void MoveEntry(int From, int To, bool SaveMetadata);

	bool IsDirty() const { return m_Dirty; }

	// task-87 (verifier B2/B3/B4): binding/key sanitation + applied-key cleanup helpers.
	static size_t EffectiveCommandCount(const SEntry &Entry);
	bool IsValidBinding(int Key, int ModifierMask) const;
	bool ValidateEntryBinding(SEntry &Entry);
	void UnbindKey(int Key, int ModifierMask);
	void CleanupEntryBinding(const SEntry &Entry);
	void RefreshBindings();
	void ClearManagedBindings();
	std::vector<std::pair<int, int>> ReadAppliedKeys();
	bool WriteAppliedKeys(const std::vector<std::pair<int, int>> &Keys);
	void UnbindManagedKeys(const std::string &Config);
	void RemoveStaleStepFiles(const std::string &Config, const SEntry &Entry, size_t NumCommands);
	bool RemoveDirectoryRecursively(const std::string &RelativeDir);
	std::string AppliedKeysPath() const;
	void SetDirty() { m_Dirty = true; }
	void ClearDirty() { m_Dirty = false; }
	int LastError(char *pBuf, int Size) const;

	// Import/export of a single config as one zip archive with a unique top-level directory.
	bool ImportZip(const char *pZipPath, std::string &Error);
	bool ExportZip(const char *pZipPath, std::string &Error);

	// Native file dialog on Windows; on other platforms returns false and the page uses a
	// text input path instead.
	bool PickFile(bool Save, const char *pDefaultName, char *pOutPath, size_t OutSize);

	// GUI helpers.
	static bool IsValidName(const char *pName);
	bool IsCommandLineExecutable(const char *pLine);
	static bool IsCommandLineWritable(const char *pLine);
	bool SaveMetadataOnly();

	// Run cfg files for the current config (also used by OnStateChange).
	void ExecuteCurrentConfig();

private:
	struct SZipFile
	{
		std::string m_Name;
		std::vector<unsigned char> m_Data;
	};

	std::vector<std::string> m_vConfigs;
	std::string m_CurrentConfig;
	std::vector<SEntry> m_vEntries;
	bool m_Dirty = false;
	bool m_Initialized = false;
	std::string m_LastError;

	std::string ConfigDir(const std::string &Config) const;
	std::string EntryDir(const std::string &Config, const std::string &Entry) const;
	std::string ManagerPath(const std::string &Config) const;
	std::string EntryCfgPath(const std::string &Config, const std::string &Entry, const char *pCfg) const;
	std::string AbsoluteSavePath(const std::string &Relative) const;

	bool LoadConfigFromDisk(const std::string &Config);
	bool SaveCurrentToDisk();
	bool WriteTextFile(const std::string &Path, const std::string &Text);
	bool ReadTextFile(const std::string &Path, std::string &Out);
	bool EnsureSaveFolder(const std::string &Relative);
	void DeleteConfigContents(const std::string &Config);
	void DeleteEntryContents(const std::string &Config, const std::string &Entry);

	bool WriteManagerMetadata(const std::string &Config);
	bool ReadManagerMetadata(const std::string &Config);
	bool WriteEntryCfgFiles(const std::string &Config, const SEntry &Entry, std::string &Error);
	std::string BuildBindCommand(const std::string &Config, const SEntry &Entry, const char *pStepFile) const;
	std::string BuildBindLine(const std::string &Config, const SEntry &Entry) const;

	void SetLastError(const char *pFormat, ...);

	bool ZipRead(const std::vector<unsigned char> &ZipData, std::vector<SZipFile> &Files, std::string &Error);
	bool ZipWrite(const std::vector<SZipFile> &Files, std::vector<unsigned char> &ZipData, std::string &Error);
	bool CollectConfigFiles(const std::string &Config, std::vector<SZipFile> &Files, std::string &Error);
	bool ExtractZipToConfig(const std::string &TopLevel, const std::vector<SZipFile> &Files, std::string &Error);
};

#endif
