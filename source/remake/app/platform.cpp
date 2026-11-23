#include "app/platform.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstdint>
#include <cstdlib>
#include <mutex>

#include "core/import.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace app {
namespace {

std::mutex g_mutex;
std::vector<FileDialogResult> g_results;
std::optional<std::string> g_paste;

void push_result(FilePurpose purpose, std::string path) {
    std::lock_guard lock(g_mutex);
    g_results.push_back({purpose, std::move(path)});
}

}  // namespace

std::vector<FileDialogResult> take_file_dialog_results() {
    std::lock_guard lock(g_mutex);
    std::vector<FileDialogResult> out;
    out.swap(g_results);
    return out;
}

std::optional<std::string> take_clipboard_text() {
    std::lock_guard lock(g_mutex);
    auto out = std::move(g_paste);
    g_paste.reset();
    return out;
}

bool platform_has_pending() {
    std::lock_guard lock(g_mutex);
    return !g_results.empty() || g_paste.has_value();
}

#ifndef __EMSCRIPTEN__
// ===========================================================================
// Desktop: SDL3 native dialogs
// ===========================================================================
namespace {

SDL_Window* g_window = nullptr;
std::string g_defaultLocation;  // must outlive the dialog

// Filters must stay valid until the callback runs, so they are static.
const SDL_DialogFileFilter kTableFilters[] = {{"Data tables (*.csv, *.txt)", "csv;txt"}, {"All files", "*"}};
const SDL_DialogFileFilter kSpreadsheetFilters[] = {{"Spreadsheet files (*.csv, *.tsv, *.txt, *.prn)", "csv;tsv;txt;prn"},
                                                    {"All files", "*"}};
const SDL_DialogFileFilter kMpaFilters[] = {{"MPA output (*.txt, *.prn, *.dat)", "txt;prn;dat"}, {"All files", "*"}};
const SDL_DialogFileFilter kReportFilters[] = {{"Reports (*.txt, *.prn, *.csv)", "txt;prn;csv"}, {"All files", "*"}};
const SDL_DialogFileFilter kAnalysisFilters[] = {{"Kinetics analyses (*.kinetics)", "kinetics"}, {"All files", "*"}};
const SDL_DialogFileFilter kRunsFilters[] = {{"Spreadsheet (*.csv)", "csv"}, {"All files", "*"}};

const SDL_DialogFileFilter* filters_for(FilePurpose p) {
    switch (p) {
        case FilePurpose::OpenTable:
        case FilePurpose::SaveTable: return kTableFilters;
        case FilePurpose::ImportSpreadsheet:
        case FilePurpose::ImportRuns: return kSpreadsheetFilters;
        case FilePurpose::ExportRuns: return kRunsFilters;
        case FilePurpose::ImportMpa: return kMpaFilters;
        case FilePurpose::ReportFile: return kReportFilters;
        case FilePurpose::OpenAnalysis:
        case FilePurpose::SaveAnalysis: return kAnalysisFilters;
    }
    return kTableFilters;
}

void SDLCALL on_dialog(void* userdata, const char* const* filelist, int /*filter*/) {
    if (!filelist || !filelist[0]) return;  // cancelled or error
    push_result(static_cast<FilePurpose>(reinterpret_cast<intptr_t>(userdata)), filelist[0]);
}

void* encode(FilePurpose p) { return reinterpret_cast<void*>(static_cast<intptr_t>(p)); }

}  // namespace

void platform_init(SDL_Window* window) { g_window = window; }

void show_open_dialog(FilePurpose purpose, const std::string& defaultLocation) {
    g_defaultLocation = defaultLocation;
    SDL_ShowOpenFileDialog(on_dialog, encode(purpose), g_window, filters_for(purpose), 2,
                           g_defaultLocation.empty() ? nullptr : g_defaultLocation.c_str(), false);
}

void show_save_dialog(FilePurpose purpose, const std::string& defaultLocation) {
    g_defaultLocation = defaultLocation;
    SDL_ShowSaveFileDialog(on_dialog, encode(purpose), g_window, filters_for(purpose), 2,
                           g_defaultLocation.empty() ? nullptr : g_defaultLocation.c_str());
}

void file_written(const std::filesystem::path&) {}

std::optional<std::string> request_clipboard_text() {
    const char* t = ImGui::GetClipboardText();
    return std::string(t ? t : "");
}

void persist_file(const std::filesystem::path&) {}
void restore_persisted_file(const std::filesystem::path&) {}

#else
// ===========================================================================
// Web: HTML file input, browser downloads, async clipboard, localStorage
// ===========================================================================
namespace {
std::string g_clipCache;  // last text copied in the app or read from the browser
}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void kinetics_web_file_chosen(int purpose, const char* path) {
    push_result(static_cast<FilePurpose>(purpose), path ? path : "");
}

// forPaste: 1 when requested by "Paste data", 0 for a background refresh.
EMSCRIPTEN_KEEPALIVE void kinetics_web_clipboard(int forPaste, const char* text) {
    std::lock_guard lock(g_mutex);
    if (text) g_clipCache = text;
    if (forPaste) g_paste = text ? std::string(text) : std::string();
}

}  // extern "C"

EM_JS_DEPS(kinetics_platform, "$stringToNewUTF8,$UTF8ToString");

// The file is read into the in-memory file system under /uploads.
EM_JS(void, js_open_file, (int purpose, const char* accept), {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = UTF8ToString(accept);
    input.style.display = 'none';
    document.body.appendChild(input);
    input.addEventListener('change', () => {
        const file = input.files && input.files[0];
        input.remove();
        if (!file) return;
        file.arrayBuffer().then((buf) => {
            try { FS.mkdir('/uploads'); } catch (e) {}
            const path = '/uploads/' + file.name.replace(/[\\/]/g, '_');
            FS.writeFile(path, new Uint8Array(buf));
            const p = stringToNewUTF8(path);
            _kinetics_web_file_chosen(purpose, p);
            _free(p);
        });
    });
    input.click();
});

EM_JS(void, js_download, (const char* path, const char* name), {
    const data = FS.readFile(UTF8ToString(path));
    const url = URL.createObjectURL(new Blob([data], { type: 'text/plain' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = UTF8ToString(name);
    document.body.appendChild(a);
    a.click();
    setTimeout(() => { URL.revokeObjectURL(url); a.remove(); }, 5000);
});

EM_JS(void, js_read_clipboard, (int forPaste), {
    const done = (text) => {
        if (text === null) { _kinetics_web_clipboard(forPaste, 0); return; }
        const p = stringToNewUTF8(text);
        _kinetics_web_clipboard(forPaste, p);
        _free(p);
    };
    if (!navigator.clipboard || !navigator.clipboard.readText) { done(null); return; }
    navigator.clipboard.readText().then(done, () => done(null));
});

EM_JS(void, js_write_clipboard, (const char* text), {
    if (navigator.clipboard && navigator.clipboard.writeText)
        navigator.clipboard.writeText(UTF8ToString(text)).catch(() => {});
});

EM_JS(char*, js_storage_get, (const char* key), {
    try {
        const v = localStorage.getItem(UTF8ToString(key));
        return v === null ? 0 : stringToNewUTF8(v);
    } catch (e) { return 0; }
});

EM_JS(void, js_storage_set, (const char* key, const char* value), {
    try { localStorage.setItem(UTF8ToString(key), UTF8ToString(value)); } catch (e) {}
});

namespace {

const char* accept_for(FilePurpose p) {
    switch (p) {
        case FilePurpose::OpenTable: return ".csv,.txt,text/plain,text/csv";
        case FilePurpose::ImportSpreadsheet:
        case FilePurpose::ImportRuns: return ".csv,.tsv,.txt,.prn,text/csv,text/plain";
        case FilePurpose::OpenAnalysis: return ".kinetics,.txt,text/plain";
        case FilePurpose::ImportMpa: return ".txt,.prn,.dat,text/plain";
        default: return "";
    }
}

// Text copied inside the app is kept and also sent to the system clipboard.
// Pasting with Ctrl+V uses the last known text and refreshes it from the
// browser in the background (reading the clipboard is asynchronous).
const char* web_get_clipboard(ImGuiContext*) {
    js_read_clipboard(0);
    return g_clipCache.c_str();
}

void web_set_clipboard(ImGuiContext*, const char* text) {
    g_clipCache = text ? text : "";
    js_write_clipboard(g_clipCache.c_str());
}

std::string storage_key(const std::filesystem::path& p) { return "kinetics:" + p.filename().string(); }

}  // namespace

void platform_init(SDL_Window*) {
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_GetClipboardTextFn = web_get_clipboard;
    pio.Platform_SetClipboardTextFn = web_set_clipboard;
}

void show_open_dialog(FilePurpose purpose, const std::string&) { js_open_file(static_cast<int>(purpose), accept_for(purpose)); }

// There is no save dialog in a browser: the file is written under
// /downloads and offered as a download by file_written().
void show_save_dialog(FilePurpose purpose, const std::string& defaultLocation) {
    std::string name = std::filesystem::path(defaultLocation).filename().string();
    if (name.empty())
        name = purpose == FilePurpose::ReportFile     ? "results.txt"
               : purpose == FilePurpose::SaveAnalysis ? "analysis.kinetics"
               : purpose == FilePurpose::ExportRuns   ? "runs.csv"
                                                      : "data.csv";
    std::error_code ec;
    std::filesystem::create_directories("/downloads", ec);
    push_result(purpose, "/downloads/" + name);
}

void file_written(const std::filesystem::path& path) {
    js_download(path.string().c_str(), path.filename().string().c_str());
}

std::optional<std::string> request_clipboard_text() {
    js_read_clipboard(1);
    return std::nullopt;
}

void persist_file(const std::filesystem::path& path) {
    if (const auto text = kin::read_text_file(path)) js_storage_set(storage_key(path).c_str(), text->c_str());
}

void restore_persisted_file(const std::filesystem::path& path) {
    char* v = js_storage_get(storage_key(path).c_str());
    if (!v) return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    kin::write_text_file(path, v, false);
    free(v);
}

#endif

}  // namespace app
