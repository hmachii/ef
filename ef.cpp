
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wvariadic-macros"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define TB_IMPL
#include "termbox2.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

struct Entry {
	std::string name;
	std::string path;
	struct stat info;
	bool is_dir;
	bool is_exec;
	bool is_archive;
};

struct Clipboard {
	int mode;
	std::vector<Entry> items;
};

enum PromptType { PROMPT_NONE, PROMPT_DELETE, PROMPT_OVERWRITE };

struct PromptState {
	PromptType type;
	bool active;
	bool selected_yes;
	std::string message;
	std::string target_dir;
	std::string target_path;
};

struct RenameState {
	bool active;
	std::string original_name;
	std::string original_path;
	std::string edit_name;
	size_t cursor;
};

struct CommandState {
	bool active;
	std::string command;
	size_t cursor;
};

struct SearchState {
	bool active;
	std::string query;
	size_t cursor;
};

struct GrepState {
	bool active;
	int focus_index;
	std::string pattern;
	bool word_match;
	bool case_sensitive;
	bool regex_mode;
	std::string search_path;
	bool recursive;
	std::string include_pattern;
	std::string exclude_pattern;
	std::string exclude_dir_pattern;
	size_t cursor;
};

enum CommandId {
	COMMAND_NONE,
	COMMAND_SEARCH_INPUT,
	COMMAND_GREP_INPUT,
	COMMAND_COMMAND_INPUT,
	COMMAND_RENAME_INPUT,
	COMMAND_QUIT,
	COMMAND_GO_UP,
	COMMAND_HISTORY_BACK,
	COMMAND_HISTORY_FORWARD,
	COMMAND_OPEN_OR_ENTER,
	COMMAND_ENTER_DIRECTORY,
	COMMAND_MOVE_UP,
	COMMAND_MOVE_DOWN,
	COMMAND_MARK_COPY,
	COMMAND_MARK_CUT,
	COMMAND_PASTE,
	COMMAND_DUPLICATE,
	COMMAND_REFRESH,
	COMMAND_DELETE
};

struct KeyBinding {
	bool match_key;
	uint16_t key;
	uint32_t ch;
	CommandId command_id;
};

struct AppState {
	std::string current_dir;
	std::vector<std::string> parent_dirs;
	size_t parent_selected_index;
	std::vector<Entry> entries;
	std::vector<std::string> preview_lines;
	std::string preview_image_path;
	bool preview_is_image;
	bool image_drawn;
	std::string image_drawn_path;
	Clipboard clipboard;
	PromptState prompt;
	RenameState rename;
	CommandState command;
	SearchState search;
	GrepState grep;
	std::string status_message;
	size_t selected_index;
	size_t current_scroll;
	std::vector<std::string> directory_history;
	std::vector<KeyBinding> key_bindings;
	size_t history_index;
	bool navigating_history;
	bool quit;
};

typedef void (*CommandHandler)(AppState* state);

struct NamedCommand {
	CommandId id;
	const char* name;
	CommandHandler handler;
};

static const int CLIPBOARD_NONE = 0;
static const int CLIPBOARD_COPY = 1;
static const int CLIPBOARD_CUT = 2;

static std::string ToLower(const std::string& value);
static std::string ToUpper(const std::string& value);
static std::string Trim(const std::string& value);
static std::string JoinPath(const std::string& left, const std::string& right);
static std::string BaseName(const std::string& path);
static std::string DirName(const std::string& path);
static bool PathExists(const std::string& path);
static bool IsDirectoryPath(const std::string& path);
static bool IsArchiveName(const std::string& name);
static bool IsImageName(const std::string& name);
static bool IsLikelyImageFile(const std::string& path);
static bool IsLikelyTextFile(const std::string& path);
static bool ReadDirectoryEntries(const std::string& path, std::vector<Entry>* entries);
static bool EntryLess(const Entry& left, const Entry& right);
static void BuildParentList(AppState* state);
static void BuildPreview(AppState* state);
static void SetStatus(AppState* state, const std::string& message);
static void SetStatusError(AppState* state, const std::string& action, const std::string& path);
static void ClearPrompt(AppState* state);
static void StartDeletePrompt(AppState* state, const std::string& path);
static void StartOverwritePrompt(AppState* state,
                                 const std::string& target_dir,
                                 const std::string& target_path);
static Entry* GetSelectedEntry(AppState* state);
static const Entry* GetSelectedEntry(const AppState* state);
static void MoveSelection(AppState* state, int delta);
static bool SelectEntryByName(AppState* state, const std::string& name);
static void EnsureCurrentScroll(AppState* state, int visible_rows);
static std::string ReplaceTabs(const std::string& line);
static void PushDirectoryHistory(AppState* state, const std::string& path);
static bool SetCurrentDirectory(AppState* state, const std::string& path);
static bool RefreshDirectory(AppState* state);
static bool GoUpDirectory(AppState* state);
static bool EnterSelectedDirectory(AppState* state);
static bool NavigateHistory(AppState* state, int delta);
static void MarkClipboard(AppState* state, int mode);
static void CopyRow(int x, int y, int width, const std::string& text, uintattr_t fg, uintattr_t bg);
static void DrawPanel(int x,
                      int y,
                      int width,
                      int height,
                      const std::vector<std::string>& lines,
                      size_t selected_index,
                      bool highlight_selected,
                      uintattr_t normal_fg,
                      uintattr_t selected_fg,
                      uintattr_t selected_bg);
static std::string EntryTypeLabel(const Entry* entry);
static uintattr_t EntryForeground(const Entry* entry);
static std::string BuildHeaderPrefix(void);
static std::string FormatPermissions(mode_t mode);
static std::string BuildSelectedInfo(const AppState* state);
static bool IsImg2SixelAvailable(void);
static std::string EscapeForSingleQuotedShell(const std::string& value);
static void AppendGrepOptionList(std::ostringstream* command,
                                 const std::string& option,
                                 const std::string& patterns);
static void DrawImagePreview(const AppState* state, int x, int y, int width, int height);
static void DrawInterface(AppState* state);
static bool RemovePathRecursive(const std::string& path);
static bool CopyFileData(const std::string& src, const std::string& dst, mode_t mode);
static bool CopyPathRecursive(const std::string& src, const std::string& dst);
static bool MovePathRecursive(const std::string& src, const std::string& dst);
static bool EnsureParentDirectories(const std::string& path);
static void SplitNameAndExtension(const std::string& name, std::string* stem, std::string* ext);
static std::string MakeUniqueName(const std::string& directory, const std::string& name);
static std::string MakeDuplicateName(const std::string& directory, const std::string& name);
static std::string CurrentDateStamp(void);
static bool PasteClipboard(AppState* state, const std::string& target_dir, bool allow_overwrite);
static bool DuplicateSelectedEntry(AppState* state);
static void HandlePasteRequest(AppState* state);
static void HandleDuplicateRequest(AppState* state);
static void HandleDeleteRequest(AppState* state);
static bool InitializeTermbox(void);
static bool LaunchVimForPath(AppState* state,
                             const std::string& path,
                             const std::string& return_selection_name);
static bool LaunchVimQuickfix(AppState* state, const std::string& quickfix_path);
static bool LaunchVimForSelected(AppState* state);
static void StartCommandInput(AppState* state);
static bool ExecuteCommand(AppState* state);
static void HandleCommandInput(AppState* state, const struct tb_event& event);
static void StartSearchInput(AppState* state);
static bool ExecuteSearch(AppState* state);
static void HandleSearchInput(AppState* state, const struct tb_event& event);
static void StartGrepInput(AppState* state);
static bool ExecuteGrep(AppState* state);
static void HandleGrepInput(AppState* state, const struct tb_event& event);
static void StartRenameInput(AppState* state);
static bool ApplyRename(AppState* state);
static void HandleRenameInput(AppState* state, const struct tb_event& event);
static void HandlePromptInput(AppState* state, const struct tb_event& event);
static void HandleNormalInput(AppState* state, const struct tb_event& event);
static void CommandStartSearchInput(AppState* state);
static void CommandStartGrepInput(AppState* state);
static void CommandStartCommandInput(AppState* state);
static void CommandStartRenameInput(AppState* state);
static void CommandQuit(AppState* state);
static void CommandGoUpDirectory(AppState* state);
static void CommandNavigateHistoryBack(AppState* state);
static void CommandNavigateHistoryForward(AppState* state);
static void CommandOpenOrEnter(AppState* state);
static void CommandEnterDirectory(AppState* state);
static void CommandMoveUp(AppState* state);
static void CommandMoveDown(AppState* state);
static void CommandMarkCopy(AppState* state);
static void CommandMarkCut(AppState* state);
static void CommandPaste(AppState* state);
static void CommandDuplicate(AppState* state);
static void CommandRefresh(AppState* state);
static void CommandDelete(AppState* state);
static const NamedCommand* FindNamedCommand(CommandId command_id);
static const KeyBinding* ResolveNormalKeyBinding(const AppState* state,
                                                 const struct tb_event& event);
static bool ExecuteNamedCommand(AppState* state, CommandId command_id);
static void BuildDefaultKeyBindings(std::vector<KeyBinding>* bindings);
static std::string GetExecutableDirectory(void);
static std::string GetKeymapConfigPath(void);
static const char* KeyNameFromCode(uint16_t key);
static bool ParseUnsigned(const std::string& text, unsigned long* value);
static bool ParseCtrlKeyName(const std::string& name, uint16_t* key);
static bool KeyCodeFromName(const std::string& name, uint16_t* key);
static std::string KeyTokenFromBinding(const KeyBinding& binding);
static bool KeyTokenToBinding(const std::string& token, KeyBinding* binding);
static bool WriteKeymapFile(const std::string& path, const std::vector<KeyBinding>& bindings);
static bool ReadKeymapFile(const std::string& path, std::vector<KeyBinding>* bindings);
static bool InitializeKeyBindings(AppState* state);
static bool StartApplicationDirectory(AppState* state, int argc, char** argv);
static int RunApplication(int argc, char** argv);

static const NamedCommand kNamedCommands[]
        = { { COMMAND_SEARCH_INPUT, "search.input.start", CommandStartSearchInput },
	        { COMMAND_GREP_INPUT, "grep.input.start", CommandStartGrepInput },
	        { COMMAND_COMMAND_INPUT, "command.input.start", CommandStartCommandInput },
	        { COMMAND_RENAME_INPUT, "rename.input.start", CommandStartRenameInput },
	        { COMMAND_QUIT, "app.quit", CommandQuit },
	        { COMMAND_GO_UP, "navigation.up", CommandGoUpDirectory },
	        { COMMAND_HISTORY_BACK, "history.back", CommandNavigateHistoryBack },
	        { COMMAND_HISTORY_FORWARD, "history.forward", CommandNavigateHistoryForward },
	        { COMMAND_OPEN_OR_ENTER, "entry.open_or_enter", CommandOpenOrEnter },
	        { COMMAND_ENTER_DIRECTORY, "directory.enter", CommandEnterDirectory },
	        { COMMAND_MOVE_UP, "cursor.up", CommandMoveUp },
	        { COMMAND_MOVE_DOWN, "cursor.down", CommandMoveDown },
	        { COMMAND_MARK_COPY, "clipboard.mark_copy", CommandMarkCopy },
	        { COMMAND_MARK_CUT, "clipboard.mark_cut", CommandMarkCut },
	        { COMMAND_PASTE, "clipboard.paste", CommandPaste },
	        { COMMAND_DUPLICATE, "entry.duplicate", CommandDuplicate },
	        { COMMAND_REFRESH, "directory.refresh", CommandRefresh },
	        { COMMAND_DELETE, "entry.delete", CommandDelete } };

static const KeyBinding kNormalKeyBindings[]
        = { { false, 0, '/', COMMAND_SEARCH_INPUT },
	        { false, 0, 'g', COMMAND_GREP_INPUT },
	        { false, 0, ':', COMMAND_COMMAND_INPUT },
	        { true, TB_KEY_F2, 0, COMMAND_RENAME_INPUT },
	        { true, TB_KEY_ESC, 0, COMMAND_QUIT },
	        { true, TB_KEY_BACKSPACE, 0, COMMAND_GO_UP },
	        { true, TB_KEY_BACKSPACE2, 0, COMMAND_GO_UP },
	        { false, 0, 'h', COMMAND_GO_UP },
	        { true, TB_KEY_ARROW_LEFT, 0, COMMAND_HISTORY_BACK },
	        { true, TB_KEY_ARROW_RIGHT, 0, COMMAND_HISTORY_FORWARD },
	        { true, TB_KEY_ENTER, 0, COMMAND_OPEN_OR_ENTER },
	        { false, 0, 'l', COMMAND_ENTER_DIRECTORY },
	        { true, TB_KEY_ARROW_UP, 0, COMMAND_MOVE_UP },
	        { false, 0, 'k', COMMAND_MOVE_UP },
	        { true, TB_KEY_ARROW_DOWN, 0, COMMAND_MOVE_DOWN },
	        { false, 0, 'j', COMMAND_MOVE_DOWN },
	        { false, 0, 'c', COMMAND_MARK_COPY },
	        { false, 0, 'x', COMMAND_MARK_CUT },
	        { false, 0, 'v', COMMAND_PASTE },
	        { false, 0, 'b', COMMAND_DUPLICATE },
	        { false, 0, 'r', COMMAND_REFRESH },
	        { true, TB_KEY_DELETE, 0, COMMAND_DELETE } };

static const char* kKeymapFileName = "ef_keymap.conf";

static std::string ToLower(const std::string& value) {
	std::string lowered;
	lowered.reserve(value.size());
	for (size_t i = 0; i < value.size(); ++i) {
		lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value[i]))));
	}
	return lowered;
}

static std::string ToUpper(const std::string& value) {
	std::string uppered;
	uppered.reserve(value.size());
	for (size_t i = 0; i < value.size(); ++i) {
		uppered.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(value[i]))));
	}
	return uppered;
}

static std::string Trim(const std::string& value) {
	std::string::size_type start = 0;
	while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
		++start;
	}
	std::string::size_type end = value.size();
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
		--end;
	}
	return value.substr(start, end - start);
}

static std::string JoinPath(const std::string& left, const std::string& right) {
	if (left.empty()) {
		return right;
	}
	if (left == "/") {
		return "/" + right;
	}
	return left + "/" + right;
}

static std::string BaseName(const std::string& path) {
	if (path.empty()) {
		return path;
	}
	if (path == "/") {
		return "/";
	}
	std::string::size_type pos = path.find_last_of('/');
	if (pos == std::string::npos) {
		return path;
	}
	if (pos + 1 >= path.size()) {
		return "/";
	}
	return path.substr(pos + 1);
}

static std::string DirName(const std::string& path) {
	if (path.empty()) {
		return ".";
	}
	if (path == "/") {
		return "/";
	}
	std::string::size_type pos = path.find_last_of('/');
	if (pos == std::string::npos) {
		return ".";
	}
	if (pos == 0) {
		return "/";
	}
	return path.substr(0, pos);
}

static bool PathExists(const std::string& path) {
	struct stat info;
	return stat(path.c_str(), &info) == 0;
}

static bool IsDirectoryPath(const std::string& path) {
	struct stat info;
	if (stat(path.c_str(), &info) != 0) {
		return false;
	}
	return S_ISDIR(info.st_mode);
}

static bool IsArchiveName(const std::string& name) {
	std::string lowered = ToLower(name);
	if (lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, ".zip") == 0) {
		return true;
	}
	if (lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, ".rar") == 0) {
		return true;
	}
	if (lowered.size() >= 3 && lowered.compare(lowered.size() - 3, 3, ".7z") == 0) {
		return true;
	}
	if (lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, ".tar") == 0) {
		return true;
	}
	if (lowered.size() >= 3 && lowered.compare(lowered.size() - 3, 3, ".gz") == 0) {
		return true;
	}
	if (lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, ".bz2") == 0) {
		return true;
	}
	if (lowered.size() >= 3 && lowered.compare(lowered.size() - 3, 3, ".xz") == 0) {
		return true;
	}
	if (lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, ".tgz") == 0) {
		return true;
	}
	if (lowered.size() >= 7 && lowered.compare(lowered.size() - 7, 7, ".tar.gz") == 0) {
		return true;
	}
	if (lowered.size() >= 8 && lowered.compare(lowered.size() - 8, 8, ".tar.bz2") == 0) {
		return true;
	}
	if (lowered.size() >= 7 && lowered.compare(lowered.size() - 7, 7, ".tar.xz") == 0) {
		return true;
	}
	return false;
}

static bool IsImageName(const std::string& name) {
	std::string lowered = ToLower(name);
	if (lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, ".png") == 0) {
		return true;
	}
	if (lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, ".jpg") == 0) {
		return true;
	}
	if (lowered.size() >= 5 && lowered.compare(lowered.size() - 5, 5, ".jpeg") == 0) {
		return true;
	}
	if (lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, ".gif") == 0) {
		return true;
	}
	if (lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, ".bmp") == 0) {
		return true;
	}
	if (lowered.size() >= 5 && lowered.compare(lowered.size() - 5, 5, ".webp") == 0) {
		return true;
	}
	return false;
}

static bool IsLikelyImageFile(const std::string& path) {
	if (!IsImageName(path)) {
		return false;
	}
	FILE* file = fopen(path.c_str(), "rb");
	if (file == NULL) {
		return false;
	}
	unsigned char buffer[16];
	size_t count = fread(buffer, 1, sizeof(buffer), file);
	fclose(file);
	if (count >= 8 && buffer[0] == 0x89 && buffer[1] == 'P' && buffer[2] == 'N' && buffer[3] == 'G'
	    && buffer[4] == 0x0d && buffer[5] == 0x0a && buffer[6] == 0x1a && buffer[7] == 0x0a) {
		return true;
	}
	if (count >= 3 && buffer[0] == 0xff && buffer[1] == 0xd8 && buffer[2] == 0xff) {
		return true;
	}
	if (count >= 6
	    && ((buffer[0] == 'G' && buffer[1] == 'I' && buffer[2] == 'F' && buffer[3] == '8'
	         && buffer[4] == '7' && buffer[5] == 'a')
	        || (buffer[0] == 'G' && buffer[1] == 'I' && buffer[2] == 'F' && buffer[3] == '8'
	            && buffer[4] == '9' && buffer[5] == 'a'))) {
		return true;
	}
	if (count >= 2 && buffer[0] == 'B' && buffer[1] == 'M') {
		return true;
	}
	if (count >= 12 && buffer[0] == 'R' && buffer[1] == 'I' && buffer[2] == 'F' && buffer[8] == 'W'
	    && buffer[9] == 'E' && buffer[10] == 'B' && buffer[11] == 'P') {
		return true;
	}
	return false;
}

static bool IsLikelyTextFile(const std::string& path) {
	FILE* file = fopen(path.c_str(), "rb");
	if (file == NULL) {
		return false;
	}
	unsigned char buffer[4096];
	size_t count = fread(buffer, 1, sizeof(buffer), file);
	fclose(file);
	for (size_t i = 0; i < count; ++i) {
		unsigned char ch = buffer[i];
		if (ch == 0) {
			return false;
		}
		if (ch < 9 || (ch > 13 && ch < 32)) {
			return false;
		}
	}
	return true;
}

static void SplitNameAndExtension(const std::string& name, std::string* stem, std::string* ext) {
	std::string::size_type pos = name.find_last_of('.');
	if (pos == std::string::npos || pos == 0) {
		*stem = name;
		*ext = "";
		return;
	}
	*stem = name.substr(0, pos);
	*ext = name.substr(pos);
}

static bool EntryLess(const Entry& left, const Entry& right) {
	if (left.is_dir != right.is_dir) {
		return left.is_dir > right.is_dir;
	}
	return left.name < right.name;
}

static bool ReadDirectoryEntries(const std::string& path, std::vector<Entry>* entries) {
	DIR* dir = opendir(path.c_str());
	if (dir == NULL) {
		return false;
	}
	entries->clear();
	struct dirent* dent = NULL;
	while ((dent = readdir(dir)) != NULL) {
		if (std::strcmp(dent->d_name, ".") == 0 || std::strcmp(dent->d_name, "..") == 0) {
			continue;
		}
		Entry entry;
		entry.name = dent->d_name;
		entry.path = JoinPath(path, entry.name);
		if (stat(entry.path.c_str(), &entry.info) != 0) {
			continue;
		}
		entry.is_dir = S_ISDIR(entry.info.st_mode);
		entry.is_exec = !entry.is_dir && (entry.info.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH));
		entry.is_archive = !entry.is_dir && IsArchiveName(entry.name);
		entries->push_back(entry);
	}
	closedir(dir);
	std::sort(entries->begin(), entries->end(), EntryLess);
	return true;
}

static void BuildParentList(AppState* state) {
	state->parent_dirs.clear();
	state->parent_selected_index = static_cast<size_t>(-1);
	std::string parent_path = DirName(state->current_dir);
	std::string current_name = BaseName(state->current_dir);
	std::vector<Entry> parent_entries;
	if (!ReadDirectoryEntries(parent_path, &parent_entries)) {
		return;
	}
	for (size_t i = 0; i < parent_entries.size(); ++i) {
		if (!parent_entries[i].is_dir) {
			continue;
		}
		if (parent_entries[i].name == current_name) {
			state->parent_selected_index = state->parent_dirs.size();
		}
		state->parent_dirs.push_back(parent_entries[i].name);
	}
}

static const Entry* GetSelectedEntry(const AppState* state) {
	if (state->entries.empty() || state->selected_index >= state->entries.size()) {
		return NULL;
	}
	return &state->entries[state->selected_index];
}

static Entry* GetSelectedEntry(AppState* state) {
	if (state->entries.empty() || state->selected_index >= state->entries.size()) {
		return NULL;
	}
	return &state->entries[state->selected_index];
}

static void BuildPreview(AppState* state) {
	state->preview_lines.clear();
	state->preview_is_image = false;
	state->preview_image_path.clear();
	const Entry* entry = GetSelectedEntry(state);
	if (entry == NULL) {
		state->preview_lines.push_back("[empty]");
		return;
	}
	if (entry->is_dir) {
		std::vector<Entry> directory_entries;
		if (!ReadDirectoryEntries(entry->path, &directory_entries)) {
			state->preview_lines.push_back("[cannot read directory]");
			return;
		}
		if (directory_entries.empty()) {
			state->preview_lines.push_back("[empty directory]");
			return;
		}
		for (size_t i = 0; i < directory_entries.size(); ++i) {
			state->preview_lines.push_back(directory_entries[i].name);
		}
		return;
	}
	if (IsLikelyImageFile(entry->path)) {
		state->preview_is_image = true;
		state->preview_image_path = entry->path;
		if (!IsImg2SixelAvailable()) {
			state->preview_lines.push_back("[image preview unsupported]");
		}
		return;
	}
	if (!IsLikelyTextFile(entry->path)) {
		state->preview_lines.push_back("[binary file]");
		return;
	}
	std::ifstream input(entry->path.c_str());
	if (!input) {
		state->preview_lines.push_back("[cannot read]");
		return;
	}
	std::string line;
	int count = 0;
	while (count < 200 && std::getline(input, line)) {
		if (!line.empty() && line[line.size() - 1] == '\r') {
			line.erase(line.size() - 1, 1);
		}
		state->preview_lines.push_back(ReplaceTabs(line));
		++count;
	}
	if (state->preview_lines.empty()) {
		state->preview_lines.push_back("[empty file]");
	}
}

static std::string ReplaceTabs(const std::string& line) {
	std::string output;
	output.reserve(line.size());
	for (size_t i = 0; i < line.size(); ++i) {
		if (line[i] == '\t') {
			output.append("  ");
		} else {
			output.push_back(line[i]);
		}
	}
	return output;
}

static void SetStatus(AppState* state, const std::string& message) {
	state->status_message = message;
}

static void SetStatusError(AppState* state, const std::string& action, const std::string& path) {
	std::ostringstream stream;
	stream << action << ": " << path << " (" << std::strerror(errno) << ")";
	state->status_message = stream.str();
}

static void ClearPrompt(AppState* state) {
	state->prompt.type = PROMPT_NONE;
	state->prompt.active = false;
	state->prompt.selected_yes = true;
	state->prompt.message.clear();
	state->prompt.target_dir.clear();
	state->prompt.target_path.clear();
}

static void StartDeletePrompt(AppState* state, const std::string& path) {
	state->prompt.type = PROMPT_DELETE;
	state->prompt.active = true;
	state->prompt.selected_yes = true;
	state->prompt.message = "削除しますか？";
	state->prompt.target_path = path;
	state->prompt.target_dir.clear();
}

static void StartOverwritePrompt(AppState* state,
                                 const std::string& target_dir,
                                 const std::string& target_path) {
	state->prompt.type = PROMPT_OVERWRITE;
	state->prompt.active = true;
	state->prompt.selected_yes = true;
	state->prompt.message = "上書きしますか？";
	state->prompt.target_dir = target_dir;
	state->prompt.target_path = target_path;
}

static void MoveSelection(AppState* state, int delta) {
	size_t previous_index = state->selected_index;
	if (state->entries.empty()) {
		state->selected_index = 0;
		state->current_scroll = 0;
		BuildPreview(state);
		return;
	}
	int next = static_cast<int>(state->selected_index) + delta;
	if (next < 0) {
		next = 0;
	}
	if (next >= static_cast<int>(state->entries.size())) {
		next = static_cast<int>(state->entries.size()) - 1;
	}
	state->selected_index = static_cast<size_t>(next);
	if (state->selected_index != previous_index) {
		state->status_message.clear();
	}
	BuildPreview(state);
}

static bool SelectEntryByName(AppState* state, const std::string& name) {
	for (size_t i = 0; i < state->entries.size(); ++i) {
		if (state->entries[i].name == name) {
			size_t previous_index = state->selected_index;
			state->selected_index = i;
			if (state->selected_index != previous_index) {
				state->status_message.clear();
			}
			BuildPreview(state);
			return true;
		}
	}
	return false;
}

static void EnsureCurrentScroll(AppState* state, int visible_rows) {
	if (visible_rows <= 0 || state->entries.empty()) {
		state->current_scroll = 0;
		return;
	}
	if (state->current_scroll >= state->entries.size()) {
		state->current_scroll = state->entries.size() - 1;
	}
	if (state->selected_index < state->current_scroll) {
		state->current_scroll = state->selected_index;
	}
	if (state->selected_index >= state->current_scroll + static_cast<size_t>(visible_rows)) {
		state->current_scroll = state->selected_index - static_cast<size_t>(visible_rows - 1);
	}
}

static void PushDirectoryHistory(AppState* state, const std::string& path) {
	if (state->navigating_history) {
		return;
	}
	if (!state->directory_history.empty()
	    && state->directory_history[state->history_index] == path) {
		return;
	}
	if (!state->directory_history.empty()
	    && state->history_index + 1 < state->directory_history.size()) {
		state->directory_history.erase(
		        state->directory_history.begin()
		                + static_cast<std::vector<std::string>::difference_type>(
		                        state->history_index + 1),
		        state->directory_history.end());
	}
	state->directory_history.push_back(path);
	state->history_index = state->directory_history.size() - 1;
}

static bool SetCurrentDirectory(AppState* state, const std::string& path) {
	if (chdir(path.c_str()) != 0) {
		SetStatusError(state, "chdir", path);
		return false;
	}
	char buffer[PATH_MAX];
	if (getcwd(buffer, sizeof(buffer)) == NULL) {
		SetStatusError(state, "getcwd", path);
		return false;
	}
	state->current_dir = buffer;
	PushDirectoryHistory(state, state->current_dir);
	return true;
}

static bool RefreshDirectory(AppState* state) {
	std::string previous_name;
	if (!state->entries.empty() && state->selected_index < state->entries.size()) {
		previous_name = state->entries[state->selected_index].name;
	}
	if (!ReadDirectoryEntries(state->current_dir, &state->entries)) {
		SetStatusError(state, "opendir", state->current_dir);
		state->entries.clear();
		state->selected_index = 0;
		state->current_scroll = 0;
		BuildParentList(state);
		BuildPreview(state);
		return false;
	}
	state->selected_index = 0;
	state->current_scroll = 0;
	if (!previous_name.empty()) {
		SelectEntryByName(state, previous_name);
	}
	BuildParentList(state);
	BuildPreview(state);
	return true;
}

static bool GoUpDirectory(AppState* state) {
	std::string previous_dir = state->current_dir;
	std::string child_name = BaseName(previous_dir);
	std::string parent = DirName(state->current_dir);
	if (parent == state->current_dir) {
		SetStatus(state, "これ以上上に移動できません");
		return false;
	}
	if (!SetCurrentDirectory(state, parent)) {
		return false;
	}
	RefreshDirectory(state);
	if (!child_name.empty() && child_name != "/") {
		SelectEntryByName(state, child_name);
	}
	return true;
}

static bool EnterSelectedDirectory(AppState* state) {
	Entry* entry = GetSelectedEntry(state);
	if (entry == NULL || !entry->is_dir) {
		SetStatus(state, "ディレクトリを選択してください");
		return false;
	}
	if (!SetCurrentDirectory(state, entry->path)) {
		return false;
	}
	RefreshDirectory(state);
	return true;
}

static bool NavigateHistory(AppState* state, int delta) {
	if (state->directory_history.empty()) {
		return false;
	}
	int next = static_cast<int>(state->history_index) + delta;
	if (next < 0 || next >= static_cast<int>(state->directory_history.size())) {
		return false;
	}
	state->navigating_history = true;
	bool ok = SetCurrentDirectory(state, state->directory_history[static_cast<size_t>(next)]);
	state->navigating_history = false;
	if (!ok) {
		return false;
	}
	state->history_index = static_cast<size_t>(next);
	RefreshDirectory(state);
	return true;
}

static void MarkClipboard(AppState* state, int mode) {
	Entry* entry = GetSelectedEntry(state);
	if (entry == NULL) {
		SetStatus(state, "選択項目がありません");
		return;
	}
	state->clipboard.mode = mode;
	state->clipboard.items.clear();
	state->clipboard.items.push_back(*entry);
	if (mode == CLIPBOARD_COPY) {
		SetStatus(state, entry->name + " をコピーしました");
	} else {
		SetStatus(state, entry->name + " を切り取りました");
	}
}

static void
CopyRow(int x, int y, int width, const std::string& text, uintattr_t fg, uintattr_t bg) {
	for (int i = 0; i < width; ++i) {
		tb_set_cell(x + i, y, ' ', fg, bg);
	}
	if (!text.empty()) {
		tb_print(x, y, fg, bg, text.c_str());
	}
}

static std::string EntryTypeLabel(const Entry* entry) {
	if (entry == NULL) {
		return "";
	}
	if (entry->is_dir) {
		return "DIR";
	}
	if (entry->is_archive) {
		return "ARC";
	}
	if (entry->is_exec) {
		return "EXE";
	}
	return "FILE";
}

static uintattr_t EntryForeground(const Entry* entry) {
	if (entry == NULL) {
		return TB_WHITE;
	}
	if (entry->is_dir) {
		return TB_BLUE | TB_BRIGHT;
	}
	if (entry->is_archive) {
		return TB_RED | TB_BRIGHT;
	}
	if (entry->is_exec) {
		return TB_GREEN | TB_BRIGHT;
	}
	return TB_WHITE;
}

static std::string BuildHeaderPrefix(void) {
	const char* user = std::getenv("USER");
	if (user == NULL || *user == '\0') {
		user = std::getenv("LOGNAME");
	}
	if (user == NULL || *user == '\0') {
		user = "unknown";
	}
	char host[256];
	host[0] = '\0';
	if (gethostname(host, sizeof(host) - 1) != 0) {
		std::strcpy(host, "unknown");
	} else {
		host[sizeof(host) - 1] = '\0';
	}
	std::ostringstream stream;
	stream << user << "@" << host;
	return stream.str();
}

static std::string FormatPermissions(mode_t mode) {
	char buffer[11];
	buffer[0] = S_ISDIR(mode) ? 'd' : '-';
	buffer[1] = (mode & S_IRUSR) ? 'r' : '-';
	buffer[2] = (mode & S_IWUSR) ? 'w' : '-';
	buffer[3] = (mode & S_IXUSR) ? 'x' : '-';
	buffer[4] = (mode & S_IRGRP) ? 'r' : '-';
	buffer[5] = (mode & S_IWGRP) ? 'w' : '-';
	buffer[6] = (mode & S_IXGRP) ? 'x' : '-';
	buffer[7] = (mode & S_IROTH) ? 'r' : '-';
	buffer[8] = (mode & S_IWOTH) ? 'w' : '-';
	buffer[9] = (mode & S_IXOTH) ? 'x' : '-';
	buffer[10] = '\0';
	return buffer;
}

static std::string BuildSelectedInfo(const AppState* state) {
	const Entry* entry = GetSelectedEntry(state);
	std::ostringstream stream;
	stream << state->current_dir;
	if (entry != NULL) {
		stream << " | " << EntryTypeLabel(entry) << " " << entry->name;
		stream << " size=" << static_cast<long>(entry->info.st_size);
		stream << " perm=" << FormatPermissions(entry->info.st_mode);
	}
	if (!state->status_message.empty()) {
		stream << " | " << state->status_message;
	}
	return stream.str();
}

static bool IsImg2SixelAvailable(void) {
	static int cached = -1;
	if (cached != -1) {
		return cached != 0;
	}
	int result = std::system("command -v img2sixel >/dev/null 2>&1");
	cached = result == 0 ? 1 : 0;
	return cached != 0;
}

static std::string EscapeForSingleQuotedShell(const std::string& value) {
	std::string escaped;
	escaped.reserve(value.size() + 8);
	for (size_t i = 0; i < value.size(); ++i) {
		if (value[i] == '\'') {
			escaped.append("'\\''");
		} else {
			escaped.push_back(value[i]);
		}
	}
	return escaped;
}

static void AppendGrepOptionList(std::ostringstream* command,
                                 const std::string& option,
                                 const std::string& patterns) {
	if (command == NULL) {
		return;
	}
	std::istringstream stream(patterns);
	std::string token;
	while (stream >> token) {
		*command << " " << option << "='" << EscapeForSingleQuotedShell(token) << "'";
	}
}

static void DrawImagePreview(const AppState* state, int x, int y, int width, int height) {
	if (!state->preview_is_image || state->preview_image_path.empty()) {
		return;
	}
	if (!IsImg2SixelAvailable()) {
		return;
	}
	if (width <= 0 || height <= 0) {
		return;
	}
	const int px_width = width * 8;
	const int px_height = height * 16;
	std::string escaped_path = EscapeForSingleQuotedShell(state->preview_image_path);
	std::ostringstream command;
	command << "img2sixel -w " << px_width << " -h " << px_height << " '" << escaped_path
	        << "' 2>/dev/null";
	printf("\0337");
	printf("\033[%d;%dH", y + 1, x + 1);
	fflush(stdout);
	std::system(command.str().c_str());
	printf("\0338");
	fflush(stdout);
}

static void DrawPanel(int x,
                      int y,
                      int width,
                      int height,
                      const std::vector<std::string>& lines,
                      size_t selected_index,
                      bool highlight_selected,
                      uintattr_t normal_fg,
                      uintattr_t selected_fg,
                      uintattr_t selected_bg) {
	if (width <= 0 || height <= 0) {
		return;
	}
	int row = 0;
	for (size_t i = 0; i < lines.size() && row < height; ++i) {
		bool selected = highlight_selected && i == selected_index;
		uintattr_t fg = selected ? selected_fg : normal_fg;
		uintattr_t bg = selected ? selected_bg : TB_DEFAULT;
		CopyRow(x, y + row, width, lines[i], fg, bg);
		++row;
	}
	for (; row < height; ++row) {
		CopyRow(x, y + row, width, "", normal_fg, TB_DEFAULT);
	}
}

static void DrawInterface(AppState* state) {
	tb_clear();
	int width = tb_width();
	int height = tb_height();
	if (width <= 0 || height <= 0) {
		tb_present();
		return;
	}
	int top_height = 1;
	int bottom_height = 1;
	int body_height = height - top_height - bottom_height;
	if (body_height < 1) {
		body_height = 1;
	}
	int left_width = width / 5;
	if (left_width < 18) {
		left_width = 18;
	}
	int right_width = width / 3;
	if (right_width < 20) {
		right_width = 20;
	}
	if (left_width + right_width > width - 2) {
		right_width = width - left_width - 2;
	}
	if (right_width < 10) {
		right_width = 10;
	}
	int middle_width = width - left_width - right_width;
	if (middle_width < 10) {
		middle_width = 10;
	}
	if (left_width + middle_width + right_width > width) {
		middle_width = width - left_width - right_width;
	}
	if (middle_width < 1) {
		middle_width = 1;
	}
	std::string header_prefix = BuildHeaderPrefix();
	const Entry* selected_entry = GetSelectedEntry(state);
	std::string header_path = selected_entry == NULL ? state->current_dir : selected_entry->path;
	CopyRow(0, 0, width, header_prefix, TB_GREEN | TB_BRIGHT, TB_DEFAULT);
	int header_path_x = static_cast<int>(header_prefix.size());
	if (header_path_x < width) {
		CopyRow(header_path_x, 0, 1, ":", TB_WHITE, TB_DEFAULT);
		++header_path_x;
	}
	if (header_path_x < width) {
		CopyRow(header_path_x,
		        0,
		        width - header_path_x,
		        header_path,
		        TB_WHITE | TB_BOLD,
		        TB_DEFAULT);
	}

	std::vector<std::string> left_lines;
	for (size_t i = 0; i < state->parent_dirs.size(); ++i) {
		left_lines.push_back(state->parent_dirs[i]);
	}
	DrawPanel(0,
	          1,
	          left_width,
	          body_height,
	          left_lines,
	          state->parent_selected_index,
	          true,
	          TB_BLUE | TB_BRIGHT,
	          TB_WHITE | TB_BRIGHT,
	          TB_REVERSE);

	int visible_rows = body_height;
	EnsureCurrentScroll(state, visible_rows);
	for (int row = 0; row < visible_rows; ++row) {
		size_t index = state->current_scroll + static_cast<size_t>(row);
		if (index >= state->entries.size()) {
			CopyRow(left_width, 1 + row, middle_width, "", TB_WHITE, TB_DEFAULT);
			continue;
		}
		uintattr_t fg = EntryForeground(&state->entries[index]);
		uintattr_t bg = TB_DEFAULT;
		if (index == state->selected_index) {
			bg = TB_REVERSE;
		}
		CopyRow(left_width, 1 + row, middle_width, state->entries[index].name, fg, bg);
	}

	int right_x = left_width + middle_width;
	DrawPanel(right_x,
	          1,
	          right_width,
	          body_height,
	          state->preview_lines,
	          static_cast<size_t>(-1),
	          false,
	          TB_WHITE,
	          TB_WHITE,
	          TB_DEFAULT);
	bool needs_image_clear = state->image_drawn
	        && (!state->preview_is_image || state->preview_image_path != state->image_drawn_path);

	if (state->rename.active) {
		std::string prefix = "RENAME: ";
		int editable_width = width - static_cast<int>(prefix.size());
		if (editable_width < 0) {
			editable_width = 0;
		}
		size_t view_start = 0;
		if (editable_width > 0 && state->rename.cursor >= static_cast<size_t>(editable_width)) {
			view_start = state->rename.cursor - static_cast<size_t>(editable_width - 1);
		}
		std::string visible_name
		        = state->rename.edit_name.substr(view_start, static_cast<size_t>(editable_width));
		CopyRow(0, height - 1, width, prefix + visible_name, TB_WHITE, TB_DEFAULT);
		int cursor_x = static_cast<int>(prefix.size())
		        + static_cast<int>(state->rename.cursor - view_start);
		if (cursor_x < 0) {
			cursor_x = 0;
		}
		if (cursor_x >= width) {
			cursor_x = width - 1;
		}
		tb_set_cursor(cursor_x, height - 1);
	} else if (state->command.active) {
		std::string prefix = ":";
		int editable_width = width - static_cast<int>(prefix.size());
		if (editable_width < 0) {
			editable_width = 0;
		}
		size_t view_start = 0;
		if (editable_width > 0 && state->command.cursor >= static_cast<size_t>(editable_width)) {
			view_start = state->command.cursor - static_cast<size_t>(editable_width - 1);
		}
		std::string visible_command
		        = state->command.command.substr(view_start, static_cast<size_t>(editable_width));
		CopyRow(0, height - 1, width, prefix + visible_command, TB_WHITE, TB_DEFAULT);
		int cursor_x = static_cast<int>(prefix.size())
		        + static_cast<int>(state->command.cursor - view_start);
		if (cursor_x < 0) {
			cursor_x = 0;
		}
		if (cursor_x >= width) {
			cursor_x = width - 1;
		}
		tb_set_cursor(cursor_x, height - 1);
	} else if (state->search.active) {
		std::string prefix = "/";
		int editable_width = width - static_cast<int>(prefix.size());
		if (editable_width < 0) {
			editable_width = 0;
		}
		size_t view_start = 0;
		if (editable_width > 0 && state->search.cursor >= static_cast<size_t>(editable_width)) {
			view_start = state->search.cursor - static_cast<size_t>(editable_width - 1);
		}
		std::string visible_query
		        = state->search.query.substr(view_start, static_cast<size_t>(editable_width));
		CopyRow(0, height - 1, width, prefix + visible_query, TB_WHITE, TB_DEFAULT);
		int cursor_x = static_cast<int>(prefix.size())
		        + static_cast<int>(state->search.cursor - view_start);
		if (cursor_x < 0) {
			cursor_x = 0;
		}
		if (cursor_x >= width) {
			cursor_x = width - 1;
		}
		tb_set_cursor(cursor_x, height - 1);
	} else if (state->grep.active) {
		CopyRow(0,
		        height - 1,
		        width,
		        "grep ダイアログ: Enter=実行 Esc=取消 Tab=移動 Space=切替",
		        TB_WHITE,
		        TB_DEFAULT);
		tb_hide_cursor();
	} else {
		CopyRow(0, height - 1, width, BuildSelectedInfo(state), TB_WHITE, TB_DEFAULT);
		tb_hide_cursor();
	}

	if (state->prompt.active) {
		int box_width = width / 2;
		if (box_width < 28) {
			box_width = 28;
		}
		if (box_width > width - 2) {
			box_width = width - 2;
		}
		int box_height = 5;
		int box_x = (width - box_width) / 2;
		int box_y = (height - box_height) / 2;
		for (int yy = 0; yy < box_height; ++yy) {
			for (int xx = 0; xx < box_width; ++xx) {
				tb_set_cell(box_x + xx, box_y + yy, ' ', TB_WHITE, TB_BLUE);
			}
		}
		CopyRow(box_x + 1,
		        box_y + 1,
		        box_width - 2,
		        state->prompt.message,
		        TB_WHITE | TB_BOLD,
		        TB_BLUE);
		std::string yes_label = state->prompt.selected_yes ? "[はい]" : " はい ";
		std::string no_label = state->prompt.selected_yes ? " いいえ " : "[いいえ]";
		CopyRow(box_x + 2, box_y + 3, 8, yes_label, TB_WHITE, TB_BLUE);
		CopyRow(box_x + 12, box_y + 3, 10, no_label, TB_WHITE, TB_BLUE);
	}

	if (state->grep.active) {
		const int field_count = 9;
		const int label_x = 2;
		const int value_x = 18;
		int box_width = width - 8;
		if (box_width < 64) {
			box_width = 64;
		}
		if (box_width > width - 2) {
			box_width = width - 2;
		}
		int box_height = 14;
		if (box_height > height - 2) {
			box_height = height - 2;
		}
		int box_x = (width - box_width) / 2;
		int box_y = (height - box_height) / 2;
		for (int yy = 0; yy < box_height; ++yy) {
			for (int xx = 0; xx < box_width; ++xx) {
				tb_set_cell(box_x + xx, box_y + yy, ' ', TB_WHITE, TB_BLUE);
			}
		}
		CopyRow(box_x + 2, box_y + 1, box_width - 4, "GREP", TB_WHITE | TB_BOLD, TB_BLUE);

		std::string rows[field_count];
		rows[0] = "条件";
		rows[1] = "単語単位";
		rows[2] = "大文字小文字区別";
		rows[3] = "正規表現";
		rows[4] = "検索場所";
		rows[5] = "サブディレクトリ";
		rows[6] = "対象ファイル";
		rows[7] = "除外ファイル";
		rows[8] = "除外ディレクトリ";

		for (int i = 0; i < field_count; ++i) {
			uintattr_t line_fg = TB_WHITE;
			uintattr_t line_bg = TB_BLUE;
			if (state->grep.focus_index == i) {
				line_bg = TB_CYAN;
			}
			CopyRow(box_x + label_x,
			        box_y + 2 + i,
			        value_x - label_x - 1,
			        rows[i],
			        line_fg,
			        line_bg);
		}

		CopyRow(box_x + value_x,
		        box_y + 2,
		        box_width - value_x - 2,
		        state->grep.pattern,
		        TB_WHITE,
		        state->grep.focus_index == 0 ? TB_CYAN : TB_BLUE);
		CopyRow(box_x + value_x,
		        box_y + 3,
		        box_width - value_x - 2,
		        state->grep.word_match ? "[x]" : "[ ]",
		        TB_WHITE,
		        state->grep.focus_index == 1 ? TB_CYAN : TB_BLUE);
		CopyRow(box_x + value_x,
		        box_y + 4,
		        box_width - value_x - 2,
		        state->grep.case_sensitive ? "[x]" : "[ ]",
		        TB_WHITE,
		        state->grep.focus_index == 2 ? TB_CYAN : TB_BLUE);
		CopyRow(box_x + value_x,
		        box_y + 5,
		        box_width - value_x - 2,
		        state->grep.regex_mode ? "[x]" : "[ ]",
		        TB_WHITE,
		        state->grep.focus_index == 3 ? TB_CYAN : TB_BLUE);
		CopyRow(box_x + value_x,
		        box_y + 6,
		        box_width - value_x - 2,
		        state->grep.search_path,
		        TB_WHITE,
		        state->grep.focus_index == 4 ? TB_CYAN : TB_BLUE);
		CopyRow(box_x + value_x,
		        box_y + 7,
		        box_width - value_x - 2,
		        state->grep.recursive ? "[x]" : "[ ]",
		        TB_WHITE,
		        state->grep.focus_index == 5 ? TB_CYAN : TB_BLUE);
		CopyRow(box_x + value_x,
		        box_y + 8,
		        box_width - value_x - 2,
		        state->grep.include_pattern,
		        TB_WHITE,
		        state->grep.focus_index == 6 ? TB_CYAN : TB_BLUE);
		CopyRow(box_x + value_x,
		        box_y + 9,
		        box_width - value_x - 2,
		        state->grep.exclude_pattern,
		        TB_WHITE,
		        state->grep.focus_index == 7 ? TB_CYAN : TB_BLUE);
		CopyRow(box_x + value_x,
		        box_y + 10,
		        box_width - value_x - 2,
		        state->grep.exclude_dir_pattern,
		        TB_WHITE,
		        state->grep.focus_index == 8 ? TB_CYAN : TB_BLUE);

		CopyRow(box_x + 2,
		        box_y + box_height - 2,
		        box_width - 4,
		        "Enter: 実行  Esc: キャンセル  Tab/Shift+Tab: 項目移動",
		        TB_WHITE,
		        TB_BLUE);

		if (state->grep.focus_index == 0 || state->grep.focus_index == 4
		    || state->grep.focus_index == 6 || state->grep.focus_index == 7
		    || state->grep.focus_index == 8) {
			const std::string* text = NULL;
			int row = 0;
			if (state->grep.focus_index == 0) {
				text = &state->grep.pattern;
				row = 2;
			} else if (state->grep.focus_index == 4) {
				text = &state->grep.search_path;
				row = 6;
			} else if (state->grep.focus_index == 6) {
				text = &state->grep.include_pattern;
				row = 8;
			} else if (state->grep.focus_index == 7) {
				text = &state->grep.exclude_pattern;
				row = 9;
			} else {
				text = &state->grep.exclude_dir_pattern;
				row = 10;
			}
			int editable_width = box_width - value_x - 2;
			size_t view_start = 0;
			if (editable_width > 0 && state->grep.cursor >= static_cast<size_t>(editable_width)) {
				view_start = state->grep.cursor - static_cast<size_t>(editable_width - 1);
			}
			std::string visible_text
			        = text->substr(view_start, static_cast<size_t>(editable_width));
			CopyRow(box_x + value_x, box_y + row, editable_width, visible_text, TB_WHITE, TB_CYAN);
			int cursor_x = box_x + value_x + static_cast<int>(state->grep.cursor - view_start);
			if (cursor_x < box_x + value_x) {
				cursor_x = box_x + value_x;
			}
			if (cursor_x >= box_x + value_x + editable_width) {
				cursor_x = box_x + value_x + editable_width - 1;
			}
			tb_set_cursor(cursor_x, box_y + row);
		} else {
			tb_hide_cursor();
		}
	}

	if (needs_image_clear) {
		printf("\033[2J\033[H");
		fflush(stdout);
		tb_invalidate();
	}
	tb_present();
	if (state->preview_is_image) {
		DrawImagePreview(state, right_x, 1, right_width, body_height);
		state->image_drawn = true;
		state->image_drawn_path = state->preview_image_path;
	} else {
		state->image_drawn = false;
		state->image_drawn_path.clear();
	}
}

static bool RemovePathRecursive(const std::string& path) {
	struct stat info;
	if (lstat(path.c_str(), &info) != 0) {
		return false;
	}
	if (S_ISDIR(info.st_mode)) {
		DIR* dir = opendir(path.c_str());
		if (dir == NULL) {
			return false;
		}
		struct dirent* dent = NULL;
		while ((dent = readdir(dir)) != NULL) {
			if (std::strcmp(dent->d_name, ".") == 0 || std::strcmp(dent->d_name, "..") == 0) {
				continue;
			}
			std::string child = JoinPath(path, dent->d_name);
			if (!RemovePathRecursive(child)) {
				closedir(dir);
				return false;
			}
		}
		closedir(dir);
		return rmdir(path.c_str()) == 0;
	}
	return unlink(path.c_str()) == 0;
}

static bool CopyFileData(const std::string& src, const std::string& dst, mode_t mode) {
	FILE* input = fopen(src.c_str(), "rb");
	if (input == NULL) {
		return false;
	}
	FILE* output = fopen(dst.c_str(), "wb");
	if (output == NULL) {
		fclose(input);
		return false;
	}
	char buffer[8192];
	size_t read_count = 0;
	while ((read_count = fread(buffer, 1, sizeof(buffer), input)) > 0) {
		if (fwrite(buffer, 1, read_count, output) != read_count) {
			fclose(input);
			fclose(output);
			return false;
		}
	}
	if (ferror(input)) {
		fclose(input);
		fclose(output);
		return false;
	}
	fclose(input);
	fflush(output);
	fclose(output);
	chmod(dst.c_str(), mode & 0777);
	return true;
}

static bool CopyPathRecursive(const std::string& src, const std::string& dst) {
	struct stat info;
	if (stat(src.c_str(), &info) != 0) {
		return false;
	}
	if (S_ISDIR(info.st_mode)) {
		if (mkdir(dst.c_str(), info.st_mode & 0777) != 0 && errno != EEXIST) {
			return false;
		}
		DIR* dir = opendir(src.c_str());
		if (dir == NULL) {
			return false;
		}
		struct dirent* dent = NULL;
		while ((dent = readdir(dir)) != NULL) {
			if (std::strcmp(dent->d_name, ".") == 0 || std::strcmp(dent->d_name, "..") == 0) {
				continue;
			}
			std::string child_src = JoinPath(src, dent->d_name);
			std::string child_dst = JoinPath(dst, dent->d_name);
			if (!CopyPathRecursive(child_src, child_dst)) {
				closedir(dir);
				return false;
			}
		}
		closedir(dir);
		return true;
	}
	return CopyFileData(src, dst, info.st_mode);
}

static bool MovePathRecursive(const std::string& src, const std::string& dst) {
	if (rename(src.c_str(), dst.c_str()) == 0) {
		return true;
	}
	if (errno != EXDEV) {
		return false;
	}
	if (!CopyPathRecursive(src, dst)) {
		return false;
	}
	return RemovePathRecursive(src);
}

static bool EnsureParentDirectories(const std::string& path) {
	std::string parent = DirName(path);
	if (parent.empty() || parent == "." || parent == "/") {
		return true;
	}
	if (PathExists(parent)) {
		return true;
	}
	if (!EnsureParentDirectories(parent)) {
		return false;
	}
	if (mkdir(parent.c_str(), 0777) != 0 && errno != EEXIST) {
		return false;
	}
	return true;
}

static std::string MakeUniqueName(const std::string& directory, const std::string& name) {
	std::string stem;
	std::string ext;
	SplitNameAndExtension(name, &stem, &ext);
	for (int index = 1; index < 10000; ++index) {
		std::ostringstream stream;
		stream << stem << "(" << index << ")" << ext;
		if (!PathExists(JoinPath(directory, stream.str()))) {
			return stream.str();
		}
	}
	return name;
}

static std::string CurrentDateStamp(void) {
	time_t now = time(NULL);
	struct tm* local = localtime(&now);
	if (local == NULL) {
		return "00000000";
	}
	char buffer[16];
	if (strftime(buffer, sizeof(buffer), "%Y%m%d", local) == 0) {
		return "00000000";
	}
	return buffer;
}

static std::string MakeDuplicateName(const std::string& directory, const std::string& name) {
	const std::string date_stamp = CurrentDateStamp();
	for (int index = 1; index < 10000; ++index) {
		std::ostringstream stream;
		stream << name << "." << date_stamp;
		if (index > 1) {
			stream << "_" << index;
		}
		std::string candidate = stream.str();
		if (!PathExists(JoinPath(directory, candidate))) {
			return candidate;
		}
	}
	return name + "." + date_stamp;
}

static bool DuplicateSelectedEntry(AppState* state) {
	Entry* entry = GetSelectedEntry(state);
	if (entry == NULL) {
		SetStatus(state, "複製する項目がありません");
		return false;
	}
	std::string duplicate_name = MakeDuplicateName(state->current_dir, entry->name);
	std::string duplicate_path = JoinPath(state->current_dir, duplicate_name);
	if (!CopyPathRecursive(entry->path, duplicate_path)) {
		SetStatusError(state, "duplicate", entry->path);
		return false;
	}
	RefreshDirectory(state);
	SelectEntryByName(state, duplicate_name);
	SetStatus(state, entry->name + " を複製しました");
	return true;
}

static bool PasteClipboard(AppState* state, const std::string& target_dir, bool allow_overwrite) {
	if (state->clipboard.mode == CLIPBOARD_NONE || state->clipboard.items.empty()) {
		SetStatus(state, "貼り付ける項目がありません");
		return false;
	}
	if (!IsDirectoryPath(target_dir)) {
		SetStatus(state, "貼り付け先がディレクトリではありません");
		return false;
	}
	std::vector<std::string> destinations;
	destinations.reserve(state->clipboard.items.size());
	for (size_t i = 0; i < state->clipboard.items.size(); ++i) {
		const Entry& item = state->clipboard.items[i];
		std::string destination_name = item.name;
		if (state->clipboard.mode == CLIPBOARD_COPY && DirName(item.path) == target_dir) {
			destination_name = MakeUniqueName(target_dir, item.name);
		}
		destinations.push_back(JoinPath(target_dir, destination_name));
	}
	for (size_t i = 0; i < state->clipboard.items.size(); ++i) {
		const Entry& item = state->clipboard.items[i];
		const std::string& destination_path = destinations[i];
		if (state->clipboard.mode == CLIPBOARD_CUT && item.path == destination_path) {
			SetStatus(state, "同じ場所への切り取り貼り付けは不要です");
			return false;
		}
		if (PathExists(destination_path)) {
			if (state->clipboard.mode == CLIPBOARD_COPY && DirName(item.path) == target_dir) {
				continue;
			}
			if (state->clipboard.items.size() > 1) {
				SetStatus(state, "同名の項目が存在するため貼り付けを中止しました");
				return false;
			}
			if (!item.is_dir && allow_overwrite) {
				if (!RemovePathRecursive(destination_path)) {
					SetStatusError(state, "remove", destination_path);
					return false;
				}
			} else if (!item.is_dir) {
				SetStatus(state, "同名の項目が存在します");
				return false;
			} else {
				SetStatus(state, "同名のディレクトリが存在します");
				return false;
			}
		}
	}
	for (size_t i = 0; i < state->clipboard.items.size(); ++i) {
		const Entry& item = state->clipboard.items[i];
		const std::string& destination_path = destinations[i];
		if (state->clipboard.mode == CLIPBOARD_COPY) {
			if (!CopyPathRecursive(item.path, destination_path)) {
				SetStatusError(state, "copy", item.path);
				return false;
			}
		} else {
			if (!MovePathRecursive(item.path, destination_path)) {
				SetStatusError(state, "move", item.path);
				return false;
			}
		}
	}
	if (state->clipboard.mode == CLIPBOARD_CUT) {
		state->clipboard.mode = CLIPBOARD_NONE;
		state->clipboard.items.clear();
	}
	SetStatus(state, "貼り付けました");
	RefreshDirectory(state);
	return true;
}

static void HandlePasteRequest(AppState* state) {
	if (state->clipboard.mode == CLIPBOARD_NONE || state->clipboard.items.empty()) {
		SetStatus(state, "貼り付ける項目がありません");
		return;
	}
	const Entry* entry = GetSelectedEntry(state);
	std::string target_dir = state->current_dir;
	if (entry != NULL && entry->is_dir) {
		target_dir = entry->path;
	}
	if (state->clipboard.items.size() == 1 && !state->clipboard.items[0].is_dir) {
		std::string destination_path = JoinPath(target_dir, state->clipboard.items[0].name);
		if (PathExists(destination_path) && DirName(state->clipboard.items[0].path) != target_dir) {
			StartOverwritePrompt(state, target_dir, destination_path);
			return;
		}
	}
	PasteClipboard(state, target_dir, false);
}

static void HandleDuplicateRequest(AppState* state) {
	DuplicateSelectedEntry(state);
}

static void HandleDeleteRequest(AppState* state) {
	Entry* entry = GetSelectedEntry(state);
	if (entry == NULL) {
		SetStatus(state, "削除する項目がありません");
		return;
	}
	StartDeletePrompt(state, entry->path);
}

static bool InitializeTermbox(void) {
	if (tb_init() < 0) {
		return false;
	}
	tb_set_input_mode(TB_INPUT_ESC);
	tb_set_output_mode(TB_OUTPUT_NORMAL);
	return true;
}

static bool LaunchVimForPath(AppState* state,
                             const std::string& path,
                             const std::string& return_selection_name) {
	tb_shutdown();
	pid_t pid = fork();
	if (pid < 0) {
		if (!InitializeTermbox()) {
			state->quit = true;
			return false;
		}
		SetStatusError(state, "fork", path);
		return false;
	}
	if (pid == 0) {
		char exe_path[PATH_MAX];
		exe_path[0] = '\0';
		std::string vimrc_path;
		ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
		if (exe_len > 0) {
			exe_path[exe_len] = '\0';
			std::string exe_dir = DirName(exe_path);
			std::string candidate = JoinPath(exe_dir, ".vimrc");
			if (PathExists(candidate)) {
				vimrc_path = candidate;
			}
		}

		char* args[5];
		args[0] = const_cast<char*>("vim");
		if (!vimrc_path.empty()) {
			args[1] = const_cast<char*>("-u");
			args[2] = const_cast<char*>(vimrc_path.c_str());
			args[3] = const_cast<char*>(path.c_str());
			args[4] = NULL;
		} else {
			args[1] = const_cast<char*>(path.c_str());
			args[2] = NULL;
		}
		execvp(args[0], args);
		std::fprintf(stderr, "failed to exec vim: %s\n", std::strerror(errno));
		_exit(127);
	}
	int status = 0;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno == EINTR) {
			continue;
		}
		break;
	}
	if (!InitializeTermbox()) {
		state->quit = true;
		return false;
	}
	RefreshDirectory(state);
	if (!return_selection_name.empty()) {
		SelectEntryByName(state, return_selection_name);
	}
	return true;
}

static bool LaunchVimQuickfix(AppState* state, const std::string& quickfix_path) {
	tb_shutdown();
	pid_t pid = fork();
	if (pid < 0) {
		if (!InitializeTermbox()) {
			state->quit = true;
			return false;
		}
		SetStatusError(state, "fork", quickfix_path);
		return false;
	}
	if (pid == 0) {
		char exe_path[PATH_MAX];
		exe_path[0] = '\0';
		std::string vimrc_path;
		ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
		if (exe_len > 0) {
			exe_path[exe_len] = '\0';
			std::string exe_dir = DirName(exe_path);
			std::string candidate = JoinPath(exe_dir, ".vimrc");
			if (PathExists(candidate)) {
				vimrc_path = candidate;
			}
		}

		char* args[7];
		args[0] = const_cast<char*>("vim");
		if (!vimrc_path.empty()) {
			args[1] = const_cast<char*>("-u");
			args[2] = const_cast<char*>(vimrc_path.c_str());
			args[3] = const_cast<char*>("-q");
			args[4] = const_cast<char*>(quickfix_path.c_str());
			args[5] = const_cast<char*>("+copen");
			args[6] = NULL;
		} else {
			args[1] = const_cast<char*>("-q");
			args[2] = const_cast<char*>(quickfix_path.c_str());
			args[3] = const_cast<char*>("+copen");
			args[4] = NULL;
		}
		execvp(args[0], args);
		std::fprintf(stderr, "failed to exec vim: %s\n", std::strerror(errno));
		_exit(127);
	}
	int status = 0;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno == EINTR) {
			continue;
		}
		break;
	}
	if (!InitializeTermbox()) {
		state->quit = true;
		return false;
	}
	RefreshDirectory(state);
	return true;
}

static bool LaunchVimForSelected(AppState* state) {
	Entry* entry = GetSelectedEntry(state);
	if (entry == NULL) {
		SetStatus(state, "選択項目がありません");
		return false;
	}
	if (entry->is_dir) {
		return EnterSelectedDirectory(state);
	}
	if (!IsLikelyTextFile(entry->path)) {
		SetStatus(state, "テキストファイルではありません");
		return false;
	}
	const std::string file_name = entry->name;
	if (!LaunchVimForPath(state, entry->path, file_name)) {
		return false;
	}
	SetStatus(state, "Vim から戻りました");
	return true;
}

static void StartCommandInput(AppState* state) {
	state->command.active = true;
	state->command.command.clear();
	state->command.cursor = 0;
}

static bool ExecuteCommand(AppState* state) {
	std::string trimmed = Trim(state->command.command);
	if (trimmed.empty()) {
		SetStatus(state, "");
		return true;
	}
	std::istringstream stream(trimmed);
	std::string name;
	stream >> name;
	std::string args;
	std::getline(stream, args);
	args = Trim(args);

	if (name == "q") {
		state->quit = true;
		return true;
	}
	if (name == "cd") {
		std::string path = args;
		if (path.empty() || path == "~") {
			const char* home = std::getenv("HOME");
			if (home == NULL || *home == '\0') {
				SetStatus(state, "HOME が設定されていません");
				return false;
			}
			path = home;
		}
		if (!SetCurrentDirectory(state, path)) {
			return false;
		}
		RefreshDirectory(state);
		SetStatus(state, "ディレクトリを移動しました");
		return true;
	}
	if (name == "touch") {
		if (args.empty()) {
			SetStatus(state, "touch: ファイル名を指定してください");
			return false;
		}
		int fd = open(args.c_str(), O_WRONLY | O_CREAT, 0666);
		if (fd < 0) {
			SetStatusError(state, "touch", args);
			return false;
		}
		close(fd);
		if (utime(args.c_str(), NULL) != 0 && errno != ENOENT) {
			SetStatusError(state, "touch", args);
			return false;
		}
		RefreshDirectory(state);
		SelectEntryByName(state, BaseName(args));
		SetStatus(state, "ファイルを作成しました");
		return true;
	}
	if (name == "mkdir") {
		if (args.empty()) {
			SetStatus(state, "mkdir: ディレクトリ名を指定してください");
			return false;
		}
		if (mkdir(args.c_str(), 0777) != 0) {
			SetStatusError(state, "mkdir", args);
			return false;
		}
		RefreshDirectory(state);
		SelectEntryByName(state, BaseName(args));
		SetStatus(state, "ディレクトリを作成しました");
		return true;
	}
	SetStatus(state, "未対応のコマンドです");
	return false;
}

static void HandleCommandInput(AppState* state, const struct tb_event& event) {
	if (!state->command.active || event.type != TB_EVENT_KEY) {
		return;
	}
	if (event.key == TB_KEY_ESC) {
		state->command.active = false;
		SetStatus(state, "コマンドをキャンセルしました");
		return;
	}
	if (event.key == TB_KEY_ENTER) {
		state->command.active = false;
		ExecuteCommand(state);
		return;
	}
	if (event.key == TB_KEY_ARROW_LEFT) {
		if (state->command.cursor > 0) {
			--state->command.cursor;
		}
		return;
	}
	if (event.key == TB_KEY_ARROW_RIGHT) {
		if (state->command.cursor < state->command.command.size()) {
			++state->command.cursor;
		}
		return;
	}
	if (event.key == TB_KEY_HOME) {
		state->command.cursor = 0;
		return;
	}
	if (event.key == TB_KEY_END) {
		state->command.cursor = state->command.command.size();
		return;
	}
	if (event.key == TB_KEY_BACKSPACE || event.key == TB_KEY_BACKSPACE2) {
		if (state->command.cursor > 0) {
			state->command.command.erase(state->command.cursor - 1, 1);
			--state->command.cursor;
		}
		return;
	}
	if (event.key == TB_KEY_DELETE) {
		if (state->command.cursor < state->command.command.size()) {
			state->command.command.erase(state->command.cursor, 1);
		}
		return;
	}
	if (event.ch >= 32 && event.ch <= 126) {
		state->command.command.insert(state->command.cursor, 1, static_cast<char>(event.ch));
		++state->command.cursor;
	}
}

static void StartSearchInput(AppState* state) {
	state->search.active = true;
	state->search.query.clear();
	state->search.cursor = 0;
}

static bool ExecuteSearch(AppState* state) {
	std::string query = Trim(state->search.query);
	if (query.empty()) {
		SetStatus(state, "");
		return true;
	}
	for (size_t i = 0; i < state->entries.size(); ++i) {
		if (state->entries[i].name.size() < query.size()) {
			continue;
		}
		if (state->entries[i].name.compare(0, query.size(), query) == 0) {
			size_t previous_index = state->selected_index;
			state->selected_index = i;
			if (state->selected_index != previous_index) {
				state->status_message.clear();
			}
			BuildPreview(state);
			SetStatus(state, "検索で移動しました");
			return true;
		}
	}
	SetStatus(state, "一致する項目がありません");
	return false;
}

static void HandleSearchInput(AppState* state, const struct tb_event& event) {
	if (!state->search.active || event.type != TB_EVENT_KEY) {
		return;
	}
	if (event.key == TB_KEY_ESC) {
		state->search.active = false;
		SetStatus(state, "検索をキャンセルしました");
		return;
	}
	if (event.key == TB_KEY_ENTER) {
		state->search.active = false;
		ExecuteSearch(state);
		return;
	}
	if (event.key == TB_KEY_ARROW_LEFT) {
		if (state->search.cursor > 0) {
			--state->search.cursor;
		}
		return;
	}
	if (event.key == TB_KEY_ARROW_RIGHT) {
		if (state->search.cursor < state->search.query.size()) {
			++state->search.cursor;
		}
		return;
	}
	if (event.key == TB_KEY_HOME) {
		state->search.cursor = 0;
		return;
	}
	if (event.key == TB_KEY_END) {
		state->search.cursor = state->search.query.size();
		return;
	}
	if (event.key == TB_KEY_BACKSPACE || event.key == TB_KEY_BACKSPACE2) {
		if (state->search.cursor > 0) {
			state->search.query.erase(state->search.cursor - 1, 1);
			--state->search.cursor;
		}
		return;
	}
	if (event.key == TB_KEY_DELETE) {
		if (state->search.cursor < state->search.query.size()) {
			state->search.query.erase(state->search.cursor, 1);
		}
		return;
	}
	if (event.ch >= 32 && event.ch <= 126) {
		state->search.query.insert(state->search.cursor, 1, static_cast<char>(event.ch));
		++state->search.cursor;
	}
}

static void StartGrepInput(AppState* state) {
	state->grep.active = true;
	state->grep.focus_index = 0;
	state->grep.pattern.clear();
	state->grep.word_match = false;
	state->grep.case_sensitive = true;
	state->grep.regex_mode = false;
	state->grep.search_path = state->current_dir;
	state->grep.recursive = true;
	state->grep.include_pattern.clear();
	state->grep.exclude_pattern.clear();
	state->grep.exclude_dir_pattern.clear();
	state->grep.cursor = 0;
	SetStatus(state, "");
}

static bool ExecuteGrep(AppState* state) {
	std::string pattern = Trim(state->grep.pattern);
	std::string search_path = Trim(state->grep.search_path);
	if (pattern.empty()) {
		SetStatus(state, "条件を入力してください");
		return false;
	}
	if (search_path.empty()) {
		search_path = state->current_dir;
	}
	if (!IsDirectoryPath(search_path)) {
		SetStatus(state, "検索場所がディレクトリではありません");
		return false;
	}

	char temp_path[] = "/tmp/ef_grep_XXXXXX";
	int fd = mkstemp(temp_path);
	if (fd < 0) {
		SetStatusError(state, "mkstemp", "/tmp");
		return false;
	}
	close(fd);

	std::ostringstream grep_opts;
	grep_opts << "-n -H";
	if (state->grep.word_match) {
		grep_opts << " -w";
	}
	if (!state->grep.case_sensitive) {
		grep_opts << " -i";
	}
	if (state->grep.regex_mode) {
		grep_opts << " -E";
	} else {
		grep_opts << " -F";
	}
	if (state->grep.recursive) {
		grep_opts << " -r";
		AppendGrepOptionList(&grep_opts, "--include", state->grep.include_pattern);
		AppendGrepOptionList(&grep_opts, "--exclude", state->grep.exclude_pattern);
		AppendGrepOptionList(&grep_opts, "--exclude-dir", state->grep.exclude_dir_pattern);
	}

	std::string escaped_pattern = EscapeForSingleQuotedShell(pattern);
	std::string escaped_search_path = EscapeForSingleQuotedShell(search_path);
	std::string escaped_temp = EscapeForSingleQuotedShell(temp_path);

	std::ostringstream command;
	if (state->grep.recursive) {
		command << "grep " << grep_opts.str() << " '" << escaped_pattern << "' '"
		        << escaped_search_path << "' > '" << escaped_temp << "' 2>/dev/null";
	} else {
		std::string include_filter;
		if (!Trim(state->grep.include_pattern).empty()) {
			std::istringstream includes(state->grep.include_pattern);
			std::string token;
			bool first = true;
			while (includes >> token) {
				if (first) {
					include_filter.append(" \\(");
					first = false;
				} else {
					include_filter.append(" -o");
				}
				include_filter.append(" -name '");
				include_filter.append(EscapeForSingleQuotedShell(token));
				include_filter.append("'");
			}
			if (!first) {
				include_filter.append(" \\)");
			}
		}
		std::string exclude_filter;
		if (!Trim(state->grep.exclude_pattern).empty()) {
			std::istringstream excludes(state->grep.exclude_pattern);
			std::string token;
			while (excludes >> token) {
				exclude_filter.append(" ! -name '");
				exclude_filter.append(EscapeForSingleQuotedShell(token));
				exclude_filter.append("'");
			}
		}
		command << "find '" << escaped_search_path << "' -maxdepth 1 -type f" << include_filter
		        << exclude_filter << " -print0 2>/dev/null | xargs -0 -r grep " << grep_opts.str()
		        << " '" << escaped_pattern << "' > '" << escaped_temp << "' 2>/dev/null";
	}

	int result = std::system(command.str().c_str());
	if (result == -1) {
		SetStatusError(state, "grep", search_path);
		unlink(temp_path);
		return false;
	}

	struct stat info;
	bool has_results = false;
	if (stat(temp_path, &info) == 0 && info.st_size > 0) {
		has_results = true;
	}
	if (!has_results) {
		FILE* fp = std::fopen(temp_path, "w");
		if (fp != NULL) {
			std::fprintf(fp, "一致する結果はありませんでした。\n");
			std::fclose(fp);
		}
	}

	if (has_results) {
		if (!LaunchVimQuickfix(state, temp_path)) {
			unlink(temp_path);
			return false;
		}
	} else {
		if (!LaunchVimForPath(state, temp_path, "")) {
			unlink(temp_path);
			return false;
		}
	}
	unlink(temp_path);
	SetStatus(state, "grep 結果を表示しました");
	return true;
}

static void HandleGrepInput(AppState* state, const struct tb_event& event) {
	if (!state->grep.active || event.type != TB_EVENT_KEY) {
		return;
	}
	if (event.key == TB_KEY_ESC) {
		state->grep.active = false;
		SetStatus(state, "grep をキャンセルしました");
		return;
	}
	if (event.key == TB_KEY_ENTER) {
		state->grep.active = false;
		ExecuteGrep(state);
		return;
	}

	const int field_count = 9;
	if (event.key == TB_KEY_TAB) {
		state->grep.focus_index = (state->grep.focus_index + 1) % field_count;
		if (state->grep.focus_index == 0) {
			state->grep.cursor = state->grep.pattern.size();
		} else if (state->grep.focus_index == 4) {
			state->grep.cursor = state->grep.search_path.size();
		} else if (state->grep.focus_index == 6) {
			state->grep.cursor = state->grep.include_pattern.size();
		} else if (state->grep.focus_index == 7) {
			state->grep.cursor = state->grep.exclude_pattern.size();
		} else if (state->grep.focus_index == 8) {
			state->grep.cursor = state->grep.exclude_dir_pattern.size();
		}
		return;
	}
	if (event.key == TB_KEY_ARROW_UP) {
		state->grep.focus_index = (state->grep.focus_index + field_count - 1) % field_count;
		if (state->grep.focus_index == 0) {
			state->grep.cursor = state->grep.pattern.size();
		} else if (state->grep.focus_index == 4) {
			state->grep.cursor = state->grep.search_path.size();
		} else if (state->grep.focus_index == 6) {
			state->grep.cursor = state->grep.include_pattern.size();
		} else if (state->grep.focus_index == 7) {
			state->grep.cursor = state->grep.exclude_pattern.size();
		} else if (state->grep.focus_index == 8) {
			state->grep.cursor = state->grep.exclude_dir_pattern.size();
		}
		return;
	}
	if (event.key == TB_KEY_ARROW_DOWN) {
		state->grep.focus_index = (state->grep.focus_index + 1) % field_count;
		if (state->grep.focus_index == 0) {
			state->grep.cursor = state->grep.pattern.size();
		} else if (state->grep.focus_index == 4) {
			state->grep.cursor = state->grep.search_path.size();
		} else if (state->grep.focus_index == 6) {
			state->grep.cursor = state->grep.include_pattern.size();
		} else if (state->grep.focus_index == 7) {
			state->grep.cursor = state->grep.exclude_pattern.size();
		} else if (state->grep.focus_index == 8) {
			state->grep.cursor = state->grep.exclude_dir_pattern.size();
		}
		return;
	}

	bool* toggle_target = NULL;
	if (state->grep.focus_index == 1) {
		toggle_target = &state->grep.word_match;
	} else if (state->grep.focus_index == 2) {
		toggle_target = &state->grep.case_sensitive;
	} else if (state->grep.focus_index == 3) {
		toggle_target = &state->grep.regex_mode;
	} else if (state->grep.focus_index == 5) {
		toggle_target = &state->grep.recursive;
	}
	if (toggle_target != NULL) {
		if (event.ch == ' ' || event.key == TB_KEY_SPACE || event.key == TB_KEY_ARROW_LEFT
		    || event.key == TB_KEY_ARROW_RIGHT || event.ch == 'h' || event.ch == 'l') {
			*toggle_target = !*toggle_target;
		}
		return;
	}

	std::string* target = NULL;
	if (state->grep.focus_index == 0) {
		target = &state->grep.pattern;
	} else if (state->grep.focus_index == 4) {
		target = &state->grep.search_path;
	} else if (state->grep.focus_index == 6) {
		target = &state->grep.include_pattern;
	} else if (state->grep.focus_index == 7) {
		target = &state->grep.exclude_pattern;
	} else if (state->grep.focus_index == 8) {
		target = &state->grep.exclude_dir_pattern;
	}
	if (target == NULL) {
		return;
	}
	if (state->grep.cursor > target->size()) {
		state->grep.cursor = target->size();
	}

	if (event.key == TB_KEY_ARROW_LEFT) {
		if (state->grep.cursor > 0) {
			--state->grep.cursor;
		}
		return;
	}
	if (event.key == TB_KEY_ARROW_RIGHT) {
		if (state->grep.cursor < target->size()) {
			++state->grep.cursor;
		}
		return;
	}
	if (event.key == TB_KEY_HOME) {
		state->grep.cursor = 0;
		return;
	}
	if (event.key == TB_KEY_END) {
		state->grep.cursor = target->size();
		return;
	}
	if (event.key == TB_KEY_BACKSPACE || event.key == TB_KEY_BACKSPACE2) {
		if (state->grep.cursor > 0) {
			target->erase(state->grep.cursor - 1, 1);
			--state->grep.cursor;
		}
		return;
	}
	if (event.key == TB_KEY_DELETE) {
		if (state->grep.cursor < target->size()) {
			target->erase(state->grep.cursor, 1);
		}
		return;
	}
	if (event.ch >= 32 && event.ch <= 126) {
		target->insert(state->grep.cursor, 1, static_cast<char>(event.ch));
		++state->grep.cursor;
	}
}

static void StartRenameInput(AppState* state) {
	Entry* entry = GetSelectedEntry(state);
	if (entry == NULL) {
		SetStatus(state, "リネームする項目がありません");
		return;
	}
	state->rename.active = true;
	state->rename.original_name = entry->name;
	state->rename.original_path = entry->path;
	state->rename.edit_name = entry->name;
	state->rename.cursor = state->rename.edit_name.size();
	SetStatus(state, "");
}

static bool ApplyRename(AppState* state) {
	if (!state->rename.active) {
		return false;
	}
	const std::string new_name = state->rename.edit_name;
	const std::string old_name = state->rename.original_name;
	const std::string old_path = state->rename.original_path;
	if (new_name.empty()) {
		SetStatus(state, "名前を空にできません");
		return false;
	}
	if (new_name.find('/') != std::string::npos) {
		SetStatus(state, "名前に '/' は使えません");
		return false;
	}
	if (new_name == old_name) {
		state->rename.active = false;
		SetStatus(state, "リネームをキャンセルしました");
		return true;
	}
	std::string new_path = JoinPath(state->current_dir, new_name);
	if (new_path != old_path && PathExists(new_path)) {
		SetStatus(state, "同名の項目が存在します");
		return false;
	}
	if (rename(old_path.c_str(), new_path.c_str()) != 0) {
		SetStatusError(state, "rename", old_path);
		return false;
	}
	state->rename.active = false;
	RefreshDirectory(state);
	SelectEntryByName(state, new_name);
	SetStatus(state, "リネームしました");
	return true;
}

static void HandleRenameInput(AppState* state, const struct tb_event& event) {
	if (!state->rename.active || event.type != TB_EVENT_KEY) {
		return;
	}
	if (event.key == TB_KEY_ESC) {
		state->rename.active = false;
		SetStatus(state, "リネームをキャンセルしました");
		return;
	}
	if (event.key == TB_KEY_ENTER) {
		ApplyRename(state);
		return;
	}
	if (event.key == TB_KEY_ARROW_LEFT) {
		if (state->rename.cursor > 0) {
			--state->rename.cursor;
		}
		return;
	}
	if (event.key == TB_KEY_ARROW_RIGHT) {
		if (state->rename.cursor < state->rename.edit_name.size()) {
			++state->rename.cursor;
		}
		return;
	}
	if (event.key == TB_KEY_HOME) {
		state->rename.cursor = 0;
		return;
	}
	if (event.key == TB_KEY_END) {
		state->rename.cursor = state->rename.edit_name.size();
		return;
	}
	if (event.key == TB_KEY_BACKSPACE || event.key == TB_KEY_BACKSPACE2) {
		if (state->rename.cursor > 0) {
			state->rename.edit_name.erase(state->rename.cursor - 1, 1);
			--state->rename.cursor;
		}
		return;
	}
	if (event.key == TB_KEY_DELETE) {
		if (state->rename.cursor < state->rename.edit_name.size()) {
			state->rename.edit_name.erase(state->rename.cursor, 1);
		}
		return;
	}
	if (event.ch >= 32 && event.ch <= 126 && event.ch != '/') {
		state->rename.edit_name.insert(state->rename.cursor, 1, static_cast<char>(event.ch));
		++state->rename.cursor;
	}
}

static void HandlePromptInput(AppState* state, const struct tb_event& event) {
	if (!state->prompt.active || event.type != TB_EVENT_KEY) {
		return;
	}
	if (event.key == TB_KEY_ESC) {
		ClearPrompt(state);
		SetStatus(state, "キャンセルしました");
		return;
	}
	if (event.key == TB_KEY_ARROW_LEFT || event.ch == 'h') {
		state->prompt.selected_yes = true;
		return;
	}
	if (event.key == TB_KEY_ARROW_RIGHT || event.ch == 'l') {
		state->prompt.selected_yes = false;
		return;
	}
	if (event.key != TB_KEY_ENTER) {
		return;
	}
	bool confirm = state->prompt.selected_yes;
	PromptType type = state->prompt.type;
	std::string target_dir = state->prompt.target_dir;
	std::string target_path = state->prompt.target_path;
	ClearPrompt(state);
	if (type == PROMPT_DELETE) {
		if (confirm) {
			if (!RemovePathRecursive(target_path)) {
				SetStatusError(state, "delete", target_path);
			} else {
				SetStatus(state, "削除しました");
				RefreshDirectory(state);
			}
		} else {
			SetStatus(state, "削除をキャンセルしました");
		}
		return;
	}
	if (type == PROMPT_OVERWRITE) {
		if (confirm) {
			PasteClipboard(state, target_dir, true);
		} else {
			SetStatus(state, "上書きをキャンセルしました");
		}
	}
}

static void CommandStartSearchInput(AppState* state) {
	StartSearchInput(state);
}

static void CommandStartGrepInput(AppState* state) {
	StartGrepInput(state);
}

static void CommandStartCommandInput(AppState* state) {
	StartCommandInput(state);
}

static void CommandStartRenameInput(AppState* state) {
	StartRenameInput(state);
}

static void CommandQuit(AppState* state) {
	state->quit = true;
}

static void CommandGoUpDirectory(AppState* state) {
	GoUpDirectory(state);
}

static void CommandNavigateHistoryBack(AppState* state) {
	NavigateHistory(state, -1);
}

static void CommandNavigateHistoryForward(AppState* state) {
	NavigateHistory(state, 1);
}

static void CommandOpenOrEnter(AppState* state) {
	LaunchVimForSelected(state);
}

static void CommandEnterDirectory(AppState* state) {
	EnterSelectedDirectory(state);
}

static void CommandMoveUp(AppState* state) {
	MoveSelection(state, -1);
}

static void CommandMoveDown(AppState* state) {
	MoveSelection(state, 1);
}

static void CommandMarkCopy(AppState* state) {
	MarkClipboard(state, CLIPBOARD_COPY);
}

static void CommandMarkCut(AppState* state) {
	MarkClipboard(state, CLIPBOARD_CUT);
}

static void CommandPaste(AppState* state) {
	HandlePasteRequest(state);
}

static void CommandDuplicate(AppState* state) {
	HandleDuplicateRequest(state);
}

static void CommandRefresh(AppState* state) {
	RefreshDirectory(state);
	SetStatus(state, "カレントディレクトリを更新しました");
}

static void CommandDelete(AppState* state) {
	HandleDeleteRequest(state);
}

static const NamedCommand* FindNamedCommand(CommandId command_id) {
	for (size_t i = 0; i < sizeof(kNamedCommands) / sizeof(kNamedCommands[0]); ++i) {
		if (kNamedCommands[i].id == command_id) {
			return &kNamedCommands[i];
		}
	}
	return NULL;
}

static const KeyBinding* ResolveNormalKeyBinding(const AppState* state,
                                                 const struct tb_event& event) {
	for (size_t i = 0; i < state->key_bindings.size(); ++i) {
		const KeyBinding& binding = state->key_bindings[i];
		if (binding.match_key) {
			if (event.key == binding.key) {
				return &binding;
			}
		} else {
			if (event.ch == binding.ch) {
				return &binding;
			}
		}
	}
	return NULL;
}

static bool ExecuteNamedCommand(AppState* state, CommandId command_id) {
	const NamedCommand* command = FindNamedCommand(command_id);
	if (command == NULL || command->handler == NULL) {
		return false;
	}
	command->handler(state);
	return true;
}

static void BuildDefaultKeyBindings(std::vector<KeyBinding>* bindings) {
	bindings->clear();
	for (size_t i = 0; i < sizeof(kNormalKeyBindings) / sizeof(kNormalKeyBindings[0]); ++i) {
		bindings->push_back(kNormalKeyBindings[i]);
	}
}

static std::string GetExecutableDirectory(void) {
	char exe_path[PATH_MAX];
	exe_path[0] = '\0';
	ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
	if (exe_len <= 0) {
		return ".";
	}
	exe_path[exe_len] = '\0';
	return DirName(exe_path);
}

static std::string GetKeymapConfigPath(void) {
	return JoinPath(GetExecutableDirectory(), kKeymapFileName);
}

static const char* KeyNameFromCode(uint16_t key) {
	if (key >= 1 && key <= 26) {
		static char ctrl_name[] = "CTRL+X";
		ctrl_name[5] = static_cast<char>('A' + (key - 1));
		return ctrl_name;
	}
	if (key == TB_KEY_F1) {
		return "F1";
	}
	if (key == TB_KEY_F2) {
		return "F2";
	}
	if (key == TB_KEY_F3) {
		return "F3";
	}
	if (key == TB_KEY_F4) {
		return "F4";
	}
	if (key == TB_KEY_F5) {
		return "F5";
	}
	if (key == TB_KEY_F6) {
		return "F6";
	}
	if (key == TB_KEY_F7) {
		return "F7";
	}
	if (key == TB_KEY_F8) {
		return "F8";
	}
	if (key == TB_KEY_F9) {
		return "F9";
	}
	if (key == TB_KEY_F10) {
		return "F10";
	}
	if (key == TB_KEY_F11) {
		return "F11";
	}
	if (key == TB_KEY_F12) {
		return "F12";
	}
	if (key == TB_KEY_ESC) {
		return "ESC";
	}
	if (key == TB_KEY_TAB) {
		return "TAB";
	}
	if (key == TB_KEY_SPACE) {
		return "SPACE";
	}
	if (key == TB_KEY_BACKSPACE) {
		return "BACKSPACE";
	}
	if (key == TB_KEY_BACKSPACE2) {
		return "BACKSPACE2";
	}
	if (key == TB_KEY_ARROW_LEFT) {
		return "LEFT";
	}
	if (key == TB_KEY_ARROW_RIGHT) {
		return "RIGHT";
	}
	if (key == TB_KEY_ENTER) {
		return "ENTER";
	}
	if (key == TB_KEY_ARROW_UP) {
		return "UP";
	}
	if (key == TB_KEY_ARROW_DOWN) {
		return "DOWN";
	}
	if (key == TB_KEY_DELETE) {
		return "DELETE";
	}
	return NULL;
}

static bool ParseUnsigned(const std::string& text, unsigned long* value) {
	if (text.empty()) {
		return false;
	}
	char* end = NULL;
	unsigned long parsed = std::strtoul(text.c_str(), &end, 0);
	if (end == NULL || *end != '\0') {
		return false;
	}
	*value = parsed;
	return true;
}

static bool ParseCtrlKeyName(const std::string& name, uint16_t* key) {
	if (name.size() != 6 || name.compare(0, 5, "CTRL+") != 0) {
		return false;
	}
	char c = name[5];
	if (c < 'A' || c > 'Z') {
		return false;
	}
	*key = static_cast<uint16_t>(c - 'A' + 1);
	return true;
}

static bool KeyCodeFromName(const std::string& name, uint16_t* key) {
	std::string upper = ToUpper(name);
	if (ParseCtrlKeyName(upper, key)) {
		return true;
	}
	if (upper == "F1") {
		*key = TB_KEY_F1;
		return true;
	}
	if (upper == "F2") {
		*key = TB_KEY_F2;
		return true;
	}
	if (upper == "F3") {
		*key = TB_KEY_F3;
		return true;
	}
	if (upper == "F4") {
		*key = TB_KEY_F4;
		return true;
	}
	if (upper == "F5") {
		*key = TB_KEY_F5;
		return true;
	}
	if (upper == "F6") {
		*key = TB_KEY_F6;
		return true;
	}
	if (upper == "F7") {
		*key = TB_KEY_F7;
		return true;
	}
	if (upper == "F8") {
		*key = TB_KEY_F8;
		return true;
	}
	if (upper == "F9") {
		*key = TB_KEY_F9;
		return true;
	}
	if (upper == "F10") {
		*key = TB_KEY_F10;
		return true;
	}
	if (upper == "F11") {
		*key = TB_KEY_F11;
		return true;
	}
	if (upper == "F12") {
		*key = TB_KEY_F12;
		return true;
	}
	if (upper == "TAB") {
		*key = TB_KEY_TAB;
		return true;
	}
	if (upper == "SPACE") {
		*key = TB_KEY_SPACE;
		return true;
	}
	if (upper == "ESC") {
		*key = TB_KEY_ESC;
		return true;
	}
	if (upper == "BACKSPACE") {
		*key = TB_KEY_BACKSPACE;
		return true;
	}
	if (upper == "BACKSPACE2") {
		*key = TB_KEY_BACKSPACE2;
		return true;
	}
	if (upper == "LEFT") {
		*key = TB_KEY_ARROW_LEFT;
		return true;
	}
	if (upper == "RIGHT") {
		*key = TB_KEY_ARROW_RIGHT;
		return true;
	}
	if (upper == "ENTER") {
		*key = TB_KEY_ENTER;
		return true;
	}
	if (upper == "UP") {
		*key = TB_KEY_ARROW_UP;
		return true;
	}
	if (upper == "DOWN") {
		*key = TB_KEY_ARROW_DOWN;
		return true;
	}
	if (upper == "DELETE") {
		*key = TB_KEY_DELETE;
		return true;
	}
	return false;
}

static std::string KeyTokenFromBinding(const KeyBinding& binding) {
	if (binding.match_key) {
		const char* name = KeyNameFromCode(binding.key);
		if (name == NULL) {
			return "";
		}
		return std::string("key:") + name;
	}
	std::string token = "ch:";
	token.push_back(static_cast<char>(binding.ch));
	return token;
}

static bool KeyTokenToBinding(const std::string& token, KeyBinding* binding) {
	if (token.size() >= 8 && token.compare(0, 8, "keycode:") == 0) {
		unsigned long key_value = 0;
		if (!ParseUnsigned(token.substr(8), &key_value) || key_value > 0xffffUL) {
			return false;
		}
		binding->match_key = true;
		binding->key = static_cast<uint16_t>(key_value);
		binding->ch = 0;
		return true;
	}
	if (token.size() >= 4 && token.compare(0, 4, "key:") == 0) {
		uint16_t key = 0;
		if (!KeyCodeFromName(token.substr(4), &key)) {
			return false;
		}
		binding->match_key = true;
		binding->key = key;
		binding->ch = 0;
		return true;
	}
	if (token.size() >= 3 && token.compare(0, 3, "ch:") == 0) {
		std::string value = token.substr(3);
		if (value == "SPACE") {
			binding->match_key = false;
			binding->key = 0;
			binding->ch = static_cast<uint32_t>(' ');
			return true;
		}
		if (value.size() > 2
		    && (value.compare(0, 2, "0x") == 0 || value.compare(0, 2, "0X") == 0)) {
			unsigned long ch_value = 0;
			if (!ParseUnsigned(value, &ch_value) || ch_value > 0x10ffffUL) {
				return false;
			}
			binding->match_key = false;
			binding->key = 0;
			binding->ch = static_cast<uint32_t>(ch_value);
			return true;
		}
		if (value.size() > 1 && std::isdigit(static_cast<unsigned char>(value[0]))) {
			unsigned long ch_value = 0;
			if (!ParseUnsigned(value, &ch_value) || ch_value > 0x10ffffUL) {
				return false;
			}
			binding->match_key = false;
			binding->key = 0;
			binding->ch = static_cast<uint32_t>(ch_value);
			return true;
		}
		if (value.size() != 1) {
			return false;
		}
		binding->match_key = false;
		binding->key = 0;
		binding->ch = static_cast<uint32_t>(static_cast<unsigned char>(value[0]));
		return true;
	}
	return false;
}

static bool WriteKeymapFile(const std::string& path, const std::vector<KeyBinding>& bindings) {
	std::ofstream out(path.c_str());
	if (!out) {
		return false;
	}
	out << "# easy_filer key map\n";
	out << "# format: bind <key_token> <command_name>\n";
	out << "# key_token examples: ch:j, ch:/, key:ENTER, key:LEFT, key:CTRL+X, keycode:24\n";
	out << "# ch supports literal, SPACE, decimal and hex (ex: ch:32, ch:0x20)\n";
	for (size_t i = 0; i < bindings.size(); ++i) {
		std::string key_token = KeyTokenFromBinding(bindings[i]);
		if (key_token.empty()) {
			continue;
		}
		const NamedCommand* command = FindNamedCommand(bindings[i].command_id);
		if (command == NULL || command->name == NULL) {
			continue;
		}
		out << "bind " << key_token << " " << command->name << "\n";
	}
	return true;
}

static bool ReadKeymapFile(const std::string& path, std::vector<KeyBinding>* bindings) {
	std::ifstream in(path.c_str());
	if (!in) {
		return false;
	}
	std::vector<KeyBinding> parsed;
	std::string line;
	while (std::getline(in, line)) {
		std::string trimmed = Trim(line);
		if (trimmed.empty() || trimmed[0] == '#') {
			continue;
		}
		std::istringstream stream(trimmed);
		std::string bind_word;
		std::string key_token;
		std::string command_name;
		stream >> bind_word >> key_token >> command_name;
		if (bind_word != "bind" || key_token.empty() || command_name.empty()) {
			continue;
		}
		KeyBinding binding;
		if (!KeyTokenToBinding(key_token, &binding)) {
			continue;
		}
		bool found = false;
		for (size_t i = 0; i < sizeof(kNamedCommands) / sizeof(kNamedCommands[0]); ++i) {
			if (command_name == kNamedCommands[i].name) {
				binding.command_id = kNamedCommands[i].id;
				parsed.push_back(binding);
				found = true;
				break;
			}
		}
		if (!found) {
			continue;
		}
	}
	if (parsed.empty()) {
		return false;
	}
	*bindings = parsed;
	return true;
}

static bool InitializeKeyBindings(AppState* state) {
	BuildDefaultKeyBindings(&state->key_bindings);
	std::string config_path = GetKeymapConfigPath();
	if (!PathExists(config_path)) {
		if (!WriteKeymapFile(config_path, state->key_bindings)) {
			SetStatus(state, "キーマップファイルの作成に失敗しました");
			return false;
		}
	}
	std::vector<KeyBinding> loaded;
	if (ReadKeymapFile(config_path, &loaded)) {
		state->key_bindings = loaded;
		return true;
	}
	if (!WriteKeymapFile(config_path, state->key_bindings)) {
		SetStatus(state, "キーマップファイルの更新に失敗しました");
		return false;
	}
	return true;
}

static void HandleNormalInput(AppState* state, const struct tb_event& event) {
	if (event.type != TB_EVENT_KEY) {
		return;
	}
	const KeyBinding* binding = ResolveNormalKeyBinding(state, event);
	if (binding == NULL) {
		return;
	}
	ExecuteNamedCommand(state, binding->command_id);
}

static bool StartApplicationDirectory(AppState* state, int argc, char** argv) {
	char buffer[PATH_MAX];
	if (argc > 1) {
		struct stat info;
		if (stat(argv[1], &info) != 0 || !S_ISDIR(info.st_mode)) {
			std::fprintf(stderr, "start path is not a directory: %s\n", argv[1]);
			return false;
		}
		if (chdir(argv[1]) != 0) {
			std::fprintf(stderr, "failed to enter directory: %s\n", argv[1]);
			return false;
		}
	}
	if (getcwd(buffer, sizeof(buffer)) == NULL) {
		std::fprintf(stderr, "failed to resolve working directory: %s\n", std::strerror(errno));
		return false;
	}
	state->current_dir = buffer;
	return true;
}

static int RunApplication(int argc, char** argv) {
	AppState state;
	state.clipboard.mode = CLIPBOARD_NONE;
	state.prompt.type = PROMPT_NONE;
	state.prompt.active = false;
	state.prompt.selected_yes = true;
	state.rename.active = false;
	state.rename.cursor = 0;
	state.command.active = false;
	state.command.cursor = 0;
	state.search.active = false;
	state.search.cursor = 0;
	state.grep.active = false;
	state.grep.focus_index = 0;
	state.grep.cursor = 0;
	state.key_bindings.clear();
	state.preview_is_image = false;
	state.image_drawn = false;
	state.parent_selected_index = static_cast<size_t>(-1);
	state.selected_index = 0;
	state.current_scroll = 0;
	state.history_index = 0;
	state.navigating_history = false;
	state.quit = false;
	if (!StartApplicationDirectory(&state, argc, argv)) {
		return 1;
	}
	InitializeKeyBindings(&state);
	state.directory_history.push_back(state.current_dir);
	if (!InitializeTermbox()) {
		std::fprintf(stderr, "tb_init failed: %s\n", std::strerror(errno));
		return 1;
	}
	RefreshDirectory(&state);
	DrawInterface(&state);
	while (!state.quit) {
		struct tb_event event;
		if (tb_poll_event(&event) < 0) {
			if (errno == EINTR) {
				continue;
			}
			SetStatusError(&state, "event", state.current_dir);
			break;
		}
		if (event.type == TB_EVENT_RESIZE) {
			DrawInterface(&state);
			continue;
		}
		if (state.rename.active) {
			HandleRenameInput(&state, event);
		} else if (state.command.active) {
			HandleCommandInput(&state, event);
		} else if (state.search.active) {
			HandleSearchInput(&state, event);
		} else if (state.grep.active) {
			HandleGrepInput(&state, event);
		} else if (state.prompt.active) {
			HandlePromptInput(&state, event);
		} else {
			HandleNormalInput(&state, event);
		}
		DrawInterface(&state);
	}
	tb_shutdown();
	return 0;
}

int main(int argc, char** argv) {
	return RunApplication(argc, argv);
}
