#pragma once
// Platform services that differ between the desktop and the browser build:
//  - file dialogs: native SDL3 dialogs on the desktop; on the web an HTML
//    file picker (the file is copied into the in-memory file system) and,
//    for saving, a path in /downloads that is offered as a browser download
//    once written;
//  - clipboard: on the web, reading the system clipboard is asynchronous;
//  - small persistent text files (preferences): browser localStorage.
// SDL may run desktop dialog callbacks on another thread, so all results are
// queued and collected by the UI thread each frame.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;

namespace app {

enum class FilePurpose { OpenTable, SaveTable, ImportSpreadsheet, ImportMpa, ReportFile };

struct FileDialogResult {
    FilePurpose purpose;
    std::string path;
};

constexpr bool kIsWeb =
#ifdef __EMSCRIPTEN__
    true;
#else
    false;
#endif

void platform_init(SDL_Window* window);

void show_open_dialog(FilePurpose purpose, const std::string& defaultLocation);
void show_save_dialog(FilePurpose purpose, const std::string& defaultLocation);
std::vector<FileDialogResult> take_file_dialog_results();

// Call after writing a file chosen with show_save_dialog. On the web this
// downloads it; on the desktop it does nothing.
void file_written(const std::filesystem::path& path);

// Clipboard text for "Paste data". Desktop: returned at once. Web: requested
// from the browser; the text arrives later through take_clipboard_text().
std::optional<std::string> request_clipboard_text();
std::optional<std::string> take_clipboard_text();

// True when a queued result (file, clipboard) is waiting: the UI should draw.
bool platform_has_pending();

// Web: copy a small text file to/from browser storage. Desktop: no-ops,
// since files persist anyway.
void persist_file(const std::filesystem::path& path);
void restore_persisted_file(const std::filesystem::path& path);

}  // namespace app
