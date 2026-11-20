#pragma once
// Runs the classic interface in the app's window: starts the program,
// passes it the keyboard as DOS saw it, resumes it when keys arrive, and
// draws the emulated screen (VGA text, or VGA graphics while a plot is
// shown) at a 4:3 aspect ratio, with a bar underneath that switches back to
// the modern interface.

#include <cstdint>
#include <optional>
#include <string_view>

#include "classic/task.hpp"

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;
struct SDL_AudioStream;
union SDL_Event;

namespace classic {

class Session;

// The key that switches between the two interfaces, in either direction.
inline constexpr const char* kSwitchKeyName = "Ctrl+F12";

class Host {
public:
    Host() = default;
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // (Re)starts the program at its title screen, with the session's state.
    void start(Session& session, SDL_Window* window);
    void stop();
    // Frees the textures and audio; call before the renderer and SDL go.
    void release();
    bool running() const { return running_; }

    void handle_event(const SDL_Event& e);
    // Resumes the program if input is waiting, then draws the screen.
    void frame(SDL_Renderer* renderer);
    // True while the program is busy or a blink is due: keep drawing frames.
    bool wants_frames() const;

    // Types keys as if pressed, for scripted screenshots: a comma-separated
    // list of literal text and key names (enter, esc, tab, bksp, space, up,
    // down, left, right, home, end, pgup, pgdn, ins, del, f1..f10, alt+a..z).
    void inject_keys(std::string_view spec);

    bool take_switch_request();
    bool take_quit_request();

private:
    void pump();
    void show_fatal_error();
    void draw(SDL_Renderer* r);
    void ensure_textures(SDL_Renderer* r);
    void play_beeps();

    Session* session_ = nullptr;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::optional<Task<void>> program_;
    bool running_ = false;
    bool fatal_ = false;
    bool switchRequested_ = false;
    bool quitRequested_ = false;

    SDL_Texture* textTex_ = nullptr;
    SDL_Texture* gfxTex_ = nullptr;
    SDL_Texture* barTex_ = nullptr;
    uint64_t drawnChanges_ = ~0ull;
    int drawnBlink_ = -1;
    bool drawnGraphics_ = false;
    float barTop_ = 0, barBottom_ = 0, buttonLeft_ = 0, buttonRight_ = 0;  // render coordinates

    SDL_AudioStream* audio_ = nullptr;
    bool audioTried_ = false;
};

}  // namespace classic
