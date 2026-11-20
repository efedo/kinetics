#include "classic/host.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "classic/kinetics.hpp"

namespace classic {
namespace {

constexpr int kCellW = 9, kCellH = 16;
constexpr int kTextW = kCols * kCellW;   // 720
constexpr int kTextH = kRows * kCellH;   // 400
constexpr uint32_t kPalette[16] = {
    0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA, 0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
    0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF, 0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF,
};
constexpr int kBlinkMs = 267;  // VGA: 16 frames on, 16 off at 60 Hz

// The bar under the screen: one text row.
const std::string kBarLeft = " Kinetics 3.51 (classic interface)";
const std::string kBarButton = std::string(" Modern interface (") + kSwitchKeyName + ") ";

void draw_cell(uint32_t* px, int pitch, int col, int row, uint8_t ch, uint32_t fg, uint32_t bg) {
    const uint16_t* g = vga_glyph(ch);
    for (int y = 0; y < kCellH; ++y) {
        uint32_t* line = px + (row * kCellH + y) * pitch + col * kCellW;
        for (int x = 0; x < kCellW; ++x) line[x] = (g[y] >> (8 - x) & 1) ? fg : bg;
    }
}

// DOS scan code for a special key, or 0.
uint8_t scan_code(SDL_Keycode k, SDL_Keymod mod) {
    const bool ctrl = mod & SDL_KMOD_CTRL;
    switch (k) {
        case SDLK_HOME: return ctrl ? key::CTL_HOME : key::HOME;
        case SDLK_UP: return key::UP;
        case SDLK_PAGEUP: return ctrl ? key::CTL_PGUP : key::PGUP;
        case SDLK_LEFT: return ctrl ? key::CTL_LEFT : key::LEFT;
        case SDLK_RIGHT: return ctrl ? key::CTL_RIGHT : key::RIGHT;
        case SDLK_END: return ctrl ? key::CTL_END : key::END;
        case SDLK_DOWN: return key::DOWN;
        case SDLK_PAGEDOWN: return ctrl ? key::CTL_PGDN : key::PGDN;
        case SDLK_INSERT: return key::INSERT;
        case SDLK_DELETE: return key::DEL;
        default: break;
    }
    if (k >= SDLK_F1 && k <= SDLK_F10) {
        const int i = static_cast<int>(k - SDLK_F1);
        if (mod & SDL_KMOD_ALT) return static_cast<uint8_t>(0x68 + i);
        if (ctrl) return static_cast<uint8_t>(0x5E + i);
        if (mod & SDL_KMOD_SHIFT) return static_cast<uint8_t>(0x54 + i);
        return static_cast<uint8_t>(0x3B + i);
    }
    return 0;
}

}  // namespace

Host::~Host() { stop(); }

void Host::release() {
    stop();
    for (SDL_Texture** t : {&textTex_, &gfxTex_, &barTex_})
        if (*t) {
            SDL_DestroyTexture(*t);
            *t = nullptr;
        }
    if (audio_) {
        SDL_DestroyAudioStream(audio_);
        audio_ = nullptr;
    }
}

void Host::start(Session& session, SDL_Window* window) {
    stop();
    session_ = &session;
    window_ = window;
    set_session(&session);
    machine().reset();
    g_fatal_error.clear();
    fatal_ = false;
    running_ = true;
    if (window_) SDL_StartTextInput(window_);
    program_.emplace(run_program());
    program_->start();
    pump();
}

void Host::stop() {
    program_.reset();  // destroys the suspended program and its windows
    running_ = false;
}

void Host::inject_keys(std::string_view spec) {
    auto& m = machine();
    size_t pos = 0;
    while (pos <= spec.size()) {
        size_t end = spec.find(',', pos);
        if (end == std::string_view::npos) end = spec.size();
        std::string tok(spec.substr(pos, end - pos));
        pos = end + 1;
        if (tok.empty()) {
            if (end < spec.size()) m.push_key(',');  // ",," types a comma
            continue;
        }
        std::string low = tok;
        for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        static const std::pair<const char*, uint8_t> specials[] = {
            {"up", key::UP}, {"down", key::DOWN}, {"left", key::LEFT}, {"right", key::RIGHT}, {"home", key::HOME},
            {"end", key::END}, {"pgup", key::PGUP}, {"pgdn", key::PGDN}, {"ins", key::INSERT}, {"del", key::DEL},
            {"ctrl+home", key::CTL_HOME}, {"ctrl+end", key::CTL_END}, {"ctrl+pgup", key::CTL_PGUP},
            {"ctrl+pgdn", key::CTL_PGDN}, {"ctrl+left", key::CTL_LEFT}, {"ctrl+right", key::CTL_RIGHT}};
        bool done = false;
        for (const auto& [name, sc] : specials)
            if (low == name) {
                m.push_special(sc);
                done = true;
            }
        if (done) continue;
        if (low == "enter") m.push_key(key::RETURN);
        else if (low == "esc") m.push_key(key::ESC);
        else if (low == "tab") m.push_key(key::TAB);
        else if (low == "bksp") m.push_key(key::BKSPACE);
        else if (low == "space") m.push_key(' ');
        else if (low.size() == 5 && low.rfind("alt+", 0) == 0 && low[4] >= 'a' && low[4] <= 'z')
            m.push_special(key::kAlt[low[4] - 'a']);
        else if (low.size() >= 2 && low[0] == 'f' && std::isdigit(static_cast<unsigned char>(low[1])) &&
                 std::atoi(low.c_str() + 1) >= 1 && std::atoi(low.c_str() + 1) <= 10)
            m.push_special(static_cast<uint8_t>(0x3B + std::atoi(low.c_str() + 1) - 1));
        else
            for (char c : cp437(tok)) m.push_key(static_cast<uint8_t>(c));  // literal text
    }
    pump();
}

bool Host::take_switch_request() { return std::exchange(switchRequested_, false); }
bool Host::take_quit_request() { return std::exchange(quitRequested_, false); }

bool Host::wants_frames() const { return running_; }

void Host::pump() {
    if (!running_) return;
    auto& m = machine();
    if (!fatal_) m.pump();
    if (!fatal_ && !g_fatal_error.empty()) show_fatal_error();
    if (!fatal_ && program_ && program_->done()) {
        // Alt+Q left the program. On the desktop that quits, as it did in
        // DOS; in the browser there is nothing to quit to, so start again.
        if (session_ && session_->is_web()) start(*session_, window_);
        else quitRequested_ = true;
    }
}

void Host::show_fatal_error() {
    // The original printed these errors and exited to DOS.
    fatal_ = true;
    textattr(0x07);
    window(1, 1, 80, 25);
    clrscr();
    cputs("\r\n");
    cputs(cp437(g_fatal_error).c_str());
    cputs("\r\n...now exiting to system (any key to continue)...\r\n");
    machine().keys.clear();
}

void Host::handle_event(const SDL_Event& event) {
    if (!running_) return;
    auto& m = machine();
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN: {
            const SDL_Keycode k = event.key.key;
            const SDL_Keymod mod = event.key.mod;
            if (k == SDLK_F12 && (mod & SDL_KMOD_CTRL)) {
                switchRequested_ = true;
                return;
            }
            if (fatal_) {
                start(*session_, window_);
                return;
            }
            if ((mod & SDL_KMOD_ALT) && k >= SDLK_A && k <= SDLK_Z) {
                m.push_special(key::kAlt[k - SDLK_A]);
            } else if (const uint8_t sc = scan_code(k, mod)) {
                m.push_special(sc);
            } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                m.push_key(key::RETURN);
            } else if (k == SDLK_ESCAPE) {
                m.push_key(key::ESC);
            } else if (k == SDLK_BACKSPACE) {
                m.push_key(key::BKSPACE);
            } else if (k == SDLK_TAB) {
                m.push_key(key::TAB);
            } else if ((mod & SDL_KMOD_CTRL) && k >= SDLK_A && k <= SDLK_Z) {
                m.push_key(static_cast<uint8_t>(k - SDLK_A + 1));
            }
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            if (fatal_) return;
            const SDL_Keymod mod = SDL_GetModState();
            if (mod & (SDL_KMOD_ALT | SDL_KMOD_CTRL)) return;  // handled as key presses
            for (char c : cp437(event.text.text)) m.push_key(static_cast<uint8_t>(c));
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (!renderer_) return;
            SDL_Event e = event;
            SDL_ConvertEventToRenderCoordinates(renderer_, &e);
            if (e.button.y >= barTop_ && e.button.y <= barBottom_ && e.button.x >= buttonLeft_ &&
                e.button.x <= buttonRight_)
                switchRequested_ = true;
            break;
        }
        default: break;
    }
    pump();
}

void Host::frame(SDL_Renderer* r) {
    renderer_ = r;
    pump();
    play_beeps();
    draw(r);
}

void Host::ensure_textures(SDL_Renderer* r) {
    if (textTex_) return;
    textTex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kTextW, kTextH);
    gfxTex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kGfxWidth, kGfxHeight);
    barTex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kTextW, kCellH);
    for (SDL_Texture* t : {textTex_, gfxTex_, barTex_}) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_PIXELART);

    // The bar: dark grey, with the switch button on the right.
    void* pixels = nullptr;
    int pitchBytes = 0;
    if (SDL_LockTexture(barTex_, nullptr, &pixels, &pitchBytes)) {
        auto* px = static_cast<uint32_t*>(pixels);
        const int pitch = pitchBytes / 4;
        const std::string left = cp437(kBarLeft), button = cp437(kBarButton);
        for (int c = 0; c < kCols; ++c) draw_cell(px, pitch, c, 0, ' ', kPalette[LIGHTGRAY], kPalette[DARKGRAY]);
        for (size_t i = 0; i < left.size() && i < kCols; ++i)
            draw_cell(px, pitch, static_cast<int>(i), 0, static_cast<uint8_t>(left[i]), kPalette[LIGHTGRAY], kPalette[DARKGRAY]);
        const int start = kCols - static_cast<int>(button.size()) - 1;
        for (size_t i = 0; i < button.size(); ++i)
            draw_cell(px, pitch, start + static_cast<int>(i), 0, static_cast<uint8_t>(button[i]), kPalette[BLACK], kPalette[LIGHTGRAY]);
        SDL_UnlockTexture(barTex_);
    }
}

void Host::draw(SDL_Renderer* r) {
    ensure_textures(r);
    auto& m = machine();
    const int blink = static_cast<int>(SDL_GetTicks() / kBlinkMs % 2);

    if (m.graphics) {
        if (m.changes != drawnChanges_ || !drawnGraphics_) {
            void* pixels = nullptr;
            int pitchBytes = 0;
            if (SDL_LockTexture(gfxTex_, nullptr, &pixels, &pitchBytes)) {
                auto* px = static_cast<uint32_t*>(pixels);
                for (int y = 0; y < kGfxHeight; ++y)
                    for (int x = 0; x < kGfxWidth; ++x)
                        px[y * (pitchBytes / 4) + x] = m.pixels[y][x] ? kPalette[WHITE] : kPalette[BLACK];
                SDL_UnlockTexture(gfxTex_);
            }
        }
    } else if (m.changes != drawnChanges_ || blink != drawnBlink_ || drawnGraphics_) {
        void* pixels = nullptr;
        int pitchBytes = 0;
        if (SDL_LockTexture(textTex_, nullptr, &pixels, &pitchBytes)) {
            auto* px = static_cast<uint32_t*>(pixels);
            const int pitch = pitchBytes / 4;
            for (int row = 0; row < kRows; ++row)
                for (int col = 0; col < kCols; ++col) {
                    const uint8_t ch = m.vram[(row * kCols + col) * 2];
                    const uint8_t at = m.vram[(row * kCols + col) * 2 + 1];
                    const uint32_t bg = kPalette[(at >> 4) & 0x07];
                    const uint32_t fg = ((at & 0x80) && !blink) ? bg : kPalette[at & 0x0F];
                    draw_cell(px, pitch, col, row, ch, fg, bg);
                }
            // Hardware cursor (the program mostly hides it and draws its own).
            if (m.cursorType != _NOCURSOR && blink) {
                const int col = m.winLeft + m.curX - 2, row = m.winTop + m.curY - 2;
                if (col >= 0 && col < kCols && row >= 0 && row < kRows) {
                    const uint32_t fg = kPalette[m.vram[(row * kCols + col) * 2 + 1] & 0x0F];
                    const int from = m.cursorType == _SOLIDCURSOR ? 0 : kCellH - 3;
                    for (int y = from; y < kCellH - 1; ++y)
                        for (int x = 0; x < kCellW - 1; ++x) px[(row * kCellH + y) * pitch + col * kCellW + x] = fg;
                }
            }
            SDL_UnlockTexture(textTex_);
        }
    }
    drawnChanges_ = m.changes;
    drawnBlink_ = blink;
    drawnGraphics_ = m.graphics;

    // Fit a 4:3 screen plus one text row of bar into the output.
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(r, &w, &h);
    SDL_SetRenderScale(r, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    const float barRatio = 0.75f / kRows;  // bar height relative to screen width
    const float W = std::min(static_cast<float>(w), static_cast<float>(h) / (0.75f + barRatio));
    const float H = 0.75f * W, B = barRatio * W;
    const float x0 = (static_cast<float>(w) - W) / 2.0f, y0 = (static_cast<float>(h) - H - B) / 2.0f;
    const SDL_FRect screenRect{x0, y0, W, H};
    SDL_RenderTexture(r, m.graphics ? gfxTex_ : textTex_, nullptr, &screenRect);
    const SDL_FRect barRect{x0, y0 + H, W, B};
    SDL_RenderTexture(r, barTex_, nullptr, &barRect);
    barTop_ = barRect.y;
    barBottom_ = barRect.y + barRect.h;
    const float cell = W / kCols;
    const int buttonStart = kCols - static_cast<int>(cp437(kBarButton).size()) - 1;
    buttonLeft_ = x0 + cell * static_cast<float>(buttonStart);
    buttonRight_ = x0 + cell * static_cast<float>(kCols - 1);
}

void Host::play_beeps() {
    auto& m = machine();
    if (m.pendingBeeps <= 0) return;
    m.pendingBeeps = 0;
    if (!audio_ && !audioTried_) {
        audioTried_ = true;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, 22050};
            audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (audio_) SDL_ResumeAudioStreamDevice(audio_);
        }
    }
    if (!audio_) return;
    // A PC-speaker style square wave: 800 Hz for a quarter of a second.
    std::vector<float> samples(22050 / 4);
    for (size_t i = 0; i < samples.size(); ++i) samples[i] = ((i * 800 * 2 / 22050) % 2) ? 0.12f : -0.12f;
    SDL_ClearAudioStream(audio_);
    SDL_PutAudioStreamData(audio_, samples.data(), static_cast<int>(samples.size() * sizeof(float)));
}

}  // namespace classic
