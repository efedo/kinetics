// Kinetics: SDL3 window, Dear ImGui / ImPlot setup and the main loop, for
// the desktop and (built with Emscripten) the browser.
//
// Command line (desktop; all optional, handy for scripting and screenshots):
//   --open <file>             open a data table into the run shown, or a saved
//                             analysis (*.kinetics)
//   --import <file>           import a spreadsheet file of runs
//   --import-mpa <file>       import an MPA file (for the classic interface)
//   --model <a..k>            choose the equation by its v3.51 menu letter
//   --fit                     fit the table after loading
//   --analyse-all             fit every run
//   --report <file>           after --fit or --analyse-all, append the report of
//                             every fitted run (current report options) to <file>
//   --tab <name>              results | graph | residuals | standard | runs | report
//                             (standard: the Results tab's Calculate box)
//   --plot <1..10>            plot type (v3.51 numbering)
//   --show <name>             help | about | report-options | fit-options | graph | residuals
//   --theme <name>            system (default) | dark | light
//   --style <name>            default | soft | slate | paper | flat (the original look)
//   --text-size <percent>     text size, 80 to 150 (default 100)
//   --predict-x <y>           Calculate box: read X off the curve for this Y
//   --predict-y <x>           Calculate box: read Y off the curve for this X
//   --classic                 start in the classic (v3.51) interface
//   --classic-keys <keys>     start in the classic interface and type these keys,
//                             e.g. "alt+t,a,enter" (see classic::Host::inject_keys)
//   --size <W>x<H>            window size in points
//   --screenshot <file.bmp>   render a few frames off-screen, save, and exit

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <implot.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/app.hpp"
#include "app/platform.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"
#include "classic/host.hpp"
#include "classic/session.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {

struct Options {
    std::string open, import, importMpa, tab, screenshot, show, report, theme, style, classicKeys, predictX, predictY;
    bool classic = false;
    char model = 0;
    int plot = 0;
    int textSize = 0;
    bool fit = false;
    bool analyseAll = false;
    int width = 1400;
    int height = 880;
};

Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--open") o.open = next();
        else if (a == "--import") o.import = next();
        else if (a == "--import-mpa") o.importMpa = next();
        else if (a == "--model") {
            const auto v = next();
            o.model = v.empty() ? 0 : v[0];
        } else if (a == "--fit") o.fit = true;
        else if (a == "--analyse-all") o.analyseAll = true;
        else if (a == "--show") o.show = next();
        else if (a == "--report") o.report = next();
        else if (a == "--tab") o.tab = next();
        else if (a == "--plot") o.plot = std::atoi(next().c_str());
        else if (a == "--screenshot") o.screenshot = next();
        else if (a == "--theme") o.theme = next();
        else if (a == "--style") o.style = next();
        else if (a == "--text-size") o.textSize = std::atoi(next().c_str());
        else if (a == "--predict-x") o.predictX = next();
        else if (a == "--predict-y") o.predictY = next();
        else if (a == "--classic") o.classic = true;
        else if (a == "--classic-keys") o.classicKeys = next();
        else if (a == "--size") {
            const auto v = next();
            char* end = nullptr;
            const long w = std::strtol(v.c_str(), &end, 10);
            if (end && *end == 'x') {
                const long h = std::strtol(end + 1, nullptr, 10);
                if (w >= 640 && h >= 400) {
                    o.width = static_cast<int>(w);
                    o.height = static_cast<int>(h);
                }
            }
        }
    }
    return o;
}

const char* first_existing(std::initializer_list<const char*> paths) {
    for (const char* p : paths)
        if (std::filesystem::exists(p)) return p;
    return nullptr;
}

app::Fonts load_fonts() {
    app::Fonts f;
    ImGuiIO& io = ImGui::GetIO();
    // /fonts/... exist only in the web build, which embeds them.
    const char* ui = first_existing({"/fonts/Roboto-Medium.ttf",
#ifdef _WIN32
                                     "C:\\Windows\\Fonts\\segoeui.ttf",
#endif
                                     "/System/Library/Fonts/SFNS.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                     "/usr/share/fonts/TTF/DejaVuSans.ttf"});
    const char* mono = first_existing({"/fonts/Cousine-Regular.ttf",
#ifdef _WIN32
                                       "C:\\Windows\\Fonts\\consola.ttf",
#endif
                                       "/System/Library/Fonts/Menlo.ttc", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                                       "/usr/share/fonts/TTF/DejaVuSansMono.ttf"});
    if (ui) f.ui = io.Fonts->AddFontFromFileTTF(ui, 17.0f);
    if (!f.ui) f.ui = io.Fonts->AddFontDefault();
    // Section titles: a semibold face where the system has one. The web
    // build's Roboto Medium is already a little heavy, so it serves for both.
    const char* heading = first_existing({
#ifdef _WIN32
        "C:\\Windows\\Fonts\\seguisb.ttf",
#endif
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"});
    if (heading && ui) f.heading = io.Fonts->AddFontFromFileTTF(heading, 17.0f);
    if (!f.heading) f.heading = f.ui;
    if (mono && (f.mono = io.Fonts->AddFontFromFileTTF(mono, 15.0f)) && ui) {
        // Fill glyphs the monospaced font lacks (e.g. >= in equation names).
        ImFontConfig cfg;
        cfg.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(ui, 15.0f, &cfg);
    }
    return f;
}

// "graph" and "residuals" are parts of the Results tab (App::show_window).
std::optional<app::Tab> tab_from_name(const std::string& n) {
    if (n == "results") return app::Tab::Results;
    if (n == "standard") return app::Tab::Results;  // the Calculate box
    if (n == "runs") return app::Tab::Runs;
    if (n == "report") return app::Tab::Report;
    return std::nullopt;
}

struct Context {
    Options opt;
    bool screenshot = false;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    app::App app;
    int busyFrames = 3;  // keep drawing a few frames after input, then idle
    int idleTicks = 0;
    int frameCount = 0;
    // The classic (v3.51) interface, sharing the App's session.
    classic::Host classicHost;
    std::unique_ptr<classic::Session> classicSession;
    bool classicActive = false;
};

void enter_classic(Context& c) {
    c.app.prepare_for_classic();
    if (!c.classicSession) c.classicSession = app::make_classic_session(c.app);
    c.classicHost.start(*c.classicSession, c.window);
    c.classicActive = true;
}

void leave_classic(Context& c) {
    c.classicHost.stop();
    c.classicActive = false;
    // Keys released while the classic interface had the keyboard.
    ImGui::GetIO().ClearInputKeys();
}

bool is_switch_key(const SDL_Event& e) {
    return e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F12 && (e.key.mod & SDL_KMOD_CTRL);
}

bool handle_event(Context& c, const SDL_Event& e) {
    if (e.type == SDL_EVENT_QUIT) return false;
    if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(c.window)) return false;
    if (e.type == SDL_EVENT_SYSTEM_THEME_CHANGED) app::system_theme_changed();
    if (c.classicActive) {
        c.classicHost.handle_event(e);
        return true;
    }
    if (is_switch_key(e)) {
        enter_classic(c);
        return true;
    }
    ImGui_ImplSDL3_ProcessEvent(&e);
    return true;
}

// One pass of the main loop: events, then (unless idle) a frame.
// Returns false when the program should end.
bool iterate(Context& c, bool mayWait) {
    bool running = true, any = false;
    SDL_Event e;
    if (mayWait && !c.screenshot && c.busyFrames <= 0 && !app::platform_has_pending() &&
        SDL_WaitEventTimeout(&e, 250)) {
        running &= handle_event(c, e);
        any = true;
    }
    while (SDL_PollEvent(&e)) {
        running &= handle_event(c, e);
        any = true;
    }
    if (app::platform_has_pending()) any = true;
    c.busyFrames = any ? 3 : c.busyFrames - 1;
    if (!running) return false;

    if (!c.screenshot && (SDL_GetWindowFlags(c.window) & SDL_WINDOW_MINIMIZED)) {
        if (mayWait) SDL_Delay(20);
        return true;
    }
    // In the browser the canvas keeps its last frame, so when nothing is
    // happening draw only a few frames a second (enough for tooltips, and in
    // the classic interface for blinking).
    if (!mayWait && c.busyFrames <= 0 && (++c.idleTicks % (c.classicActive ? 4 : 15)) != 0) return true;

    if (c.classicActive) {
        c.classicHost.frame(c.renderer);
        if (c.classicHost.take_quit_request()) running = false;
        if (c.classicHost.take_switch_request()) leave_classic(c);
    } else {
        ImGuiIO& io = ImGui::GetIO();
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (!c.app.frame()) running = false;
        ImGui::Render();

        SDL_SetRenderScale(c.renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        {
            const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
            SDL_SetRenderDrawColorFloat(c.renderer, bg.x, bg.y, bg.z, 1.0f);
        }
        SDL_RenderClear(c.renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), c.renderer);
        if (c.app.take_classic_request()) enter_classic(c);
    }

    if (c.screenshot && ++c.frameCount >= 8) {
        if (SDL_Surface* shot = SDL_RenderReadPixels(c.renderer, nullptr)) {
            if (!SDL_SaveBMP(shot, c.opt.screenshot.c_str())) std::fprintf(stderr, "SDL_SaveBMP: %s\n", SDL_GetError());
            SDL_DestroySurface(shot);
        } else {
            std::fprintf(stderr, "SDL_RenderReadPixels: %s\n", SDL_GetError());
        }
        running = false;
    }
    SDL_RenderPresent(c.renderer);
    return running;
}

#ifndef __EMSCRIPTEN__
void shutdown(Context& c) {
    c.classicHost.release();
    c.app.shutdown();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(c.renderer);
    SDL_DestroyWindow(c.window);
    SDL_Quit();
}
#else
Context* g_web = nullptr;

void web_loop(void* p) {
    if (!iterate(*static_cast<Context*>(p), false)) emscripten_cancel_main_loop();
}

// Save preferences when the tab is hidden or closed (there is no Quit).
EM_JS(void, js_install_save_hooks, (), {
    const save = () => _kinetics_web_save();
    window.addEventListener('pagehide', save);
    document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'hidden') save(); });
});
#endif

// Earlier versions kept their preferences in an "SB" folder
// (%APPDATA%\SB\Kinetics on Windows). Move those files into prefDir once,
// keeping any that prefDir already has, and remove the old folders if empty.
void move_old_preferences(const std::filesystem::path& prefDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = prefDir;
    if (!dir.has_filename()) dir = dir.parent_path();  // SDL ends it with a separator
    const fs::path oldOrg = dir.parent_path() / "SB";
    const fs::path oldDir = oldOrg / "Kinetics";
    if (!fs::is_directory(oldDir, ec)) return;
    std::vector<fs::path> files;
    for (fs::directory_iterator it(oldDir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) files.push_back(it->path());
    for (const fs::path& f : files) {
        const fs::path to = dir / f.filename();
        if (!fs::exists(to, ec)) fs::rename(f, to, ec);
    }
    fs::remove(oldDir, ec);  // these fail, as intended, unless the folder is empty
    fs::remove(oldOrg, ec);
}

}  // namespace

#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void kinetics_web_save() {
    if (g_web) g_web->app.save_settings();
}
#endif

int main(int argc, char** argv) {
    auto* ctx = new Context;
    Context& c = *ctx;
    c.opt = parse_args(argc, argv);
    c.screenshot = !app::kIsWeb && !c.opt.screenshot.empty();

    SDL_SetAppMetadata("Kinetics", KINETICS_VERSION, "org.sbrooks.kinetics");
    if (c.screenshot) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    // The window's icon is the program's (kinetics.rc, resource 1).
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kinetics", SDL_GetError(), nullptr);
        return 1;
    }

    // In the browser, sizes are CSS pixels and the device pixel ratio is
    // applied through the framebuffer scale, so no extra scaling is needed.
    const float scale = (c.screenshot || app::kIsWeb) ? 1.0f : SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    // Web: create the window for WebGL up front. Otherwise SDL_CreateRenderer
    // re-creates it, and SDL 3.4's fill-document teardown then fails while
    // moving the canvas back into the page.
    if (app::kIsWeb) flags |= SDL_WINDOW_FILL_DOCUMENT | SDL_WINDOW_OPENGL;
    else flags |= SDL_WINDOW_HIDDEN;
    c.window = SDL_CreateWindow("Kinetics", static_cast<int>(c.opt.width * scale),
                                static_cast<int>(c.opt.height * scale), flags);
    if (!c.window) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kinetics", SDL_GetError(), nullptr);
        return 1;
    }
    c.renderer = SDL_CreateRenderer(c.window, nullptr);
    if (!c.renderer) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kinetics", SDL_GetError(), c.window);
        return 1;
    }
    SDL_SetRenderVSync(c.renderer, 1);
    if (!c.screenshot && !app::kIsWeb) {
        SDL_SetWindowMinimumSize(c.window, 900, 600);
        SDL_SetWindowPosition(c.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_ShowWindow(c.window);
    }

    std::filesystem::path prefDir;
    if (app::kIsWeb) {
        prefDir = "/prefs";
        app::restore_persisted_file(prefDir / "kinetics.ini");
    } else if (char* pref = SDL_GetPrefPath(nullptr, "Kinetics")) {  // %APPDATA%\Kinetics on Windows
        prefDir = app::path_from_utf8(pref);
        SDL_free(pref);
        move_old_preferences(prefDir);
    }
    std::filesystem::path exeDir;
    if (const char* base = SDL_GetBasePath()) exeDir = app::path_from_utf8(base);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    static std::string iniPath;
    if (c.screenshot || app::kIsWeb || prefDir.empty()) {
        io.IniFilename = nullptr;
    } else {
        iniPath = (prefDir / "imgui.ini").string();
        io.IniFilename = iniPath.c_str();
    }
    app::init_style(scale);

    ImGui_ImplSDL3_InitForSDLRenderer(c.window, c.renderer);
    ImGui_ImplSDLRenderer3_Init(c.renderer);
    app::platform_init(c.window);

    app::fonts() = load_fonts();
    c.app.init(c.screenshot ? std::filesystem::path() : prefDir, exeDir);

    const Options& o = c.opt;
    if (o.model)
        if (auto m = kin::model_from_letter(o.model)) c.app.set_model(*m);
    if (!o.open.empty()) {
        const auto path = app::path_from_utf8(o.open);
        if (path.extension() == ".kinetics") c.app.open_analysis_file(path, false);
        else c.app.open_table_file(path);
    }
    if (!o.import.empty()) c.app.import_runs_file(app::path_from_utf8(o.import), false);
    if (!o.importMpa.empty()) c.app.import_mpa(app::path_from_utf8(o.importMpa));  // for the classic interface
    if (!o.report.empty()) c.app.set_report_file(app::path_from_utf8(o.report));
    if (o.fit) c.app.request_fit();
    if (o.analyseAll) c.app.fit_all_runs();
    if (!o.report.empty() && (o.fit || o.analyseAll)) c.app.write_current_report();
    if (o.plot >= 1 && o.plot <= 10) c.app.set_plot_type(static_cast<kin::PlotType>(o.plot));
    if (auto t = tab_from_name(o.tab)) c.app.select_tab(*t);
    else if (o.tab == "graph" || o.tab == "residuals") c.app.show_window(o.tab);
    if (!o.show.empty()) c.app.show_window(o.show);
    if (auto st = app::style_from_name(o.style)) app::set_style(*st);
    if (auto t = app::theme_from_name(o.theme)) app::set_theme(*t);
    if (o.textSize > 0) c.app.set_text_scale(o.textSize);
    if (!o.predictX.empty() || !o.predictY.empty()) c.app.set_predictions(o.predictX, o.predictY);
    if (o.classic || !o.classicKeys.empty()) {
        enter_classic(c);
        if (!o.classicKeys.empty()) c.classicHost.inject_keys(o.classicKeys);
    }

#ifdef __EMSCRIPTEN__
    g_web = ctx;
    js_install_save_hooks();
    emscripten_set_main_loop_arg(web_loop, ctx, 0, false);  // returns; the page keeps running
    return 0;
#else
    while (iterate(c, true)) {
    }
    shutdown(c);
    delete ctx;
    return 0;
#endif
}
