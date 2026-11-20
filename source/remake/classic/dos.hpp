#pragma once
// The emulated PC that the classic interface runs on: 80x25 colour text
// mode, the Borland C++ console functions the original program used
// (conio), the keyboard as getch() saw it, and the subset of Borland's BGI
// graphics used for the plots, in VGA high resolution (640x480): the
// program called initgraph(DETECT) and shipped EGAVGA.BGI, so that is what
// it drew on a VGA PC. It never set a colour, so the plots are white
// (getmaxcolor()) on black.
//
// Coordinates follow Borland: text positions are 1-based (column, row),
// graphics pixels 0-based. There is no SDL here; host.cpp draws the screen.

#include <coroutine>
#include <cstdint>
#include <deque>
#include <string>

namespace classic {

enum Colors {
    BLACK, BLUE, GREEN, CYAN, RED, MAGENTA, BROWN, LIGHTGRAY,
    DARKGRAY, LIGHTBLUE, LIGHTGREEN, LIGHTCYAN, LIGHTRED, LIGHTMAGENTA, YELLOW, WHITE
};

constexpr int kCols = 80;
constexpr int kRows = 25;
constexpr int kGfxWidth = 640;
constexpr int kGfxHeight = 480;

// Borland conio / BGI constants.
enum { _NOCURSOR = 0, _SOLIDCURSOR = 1, _NORMALCURSOR = 2 };
enum { DETECT = 0, VGA = 9, VGAHI = 2 };
enum { DEFAULT_FONT = 0 };
enum { HORIZ_DIR = 0, VERT_DIR = 1 };
enum { LEFT_TEXT = 0, CENTER_TEXT = 1, RIGHT_TEXT = 2 };
enum { BOTTOM_TEXT = 0, TOP_TEXT = 2 };

// Key codes as getch() returned them: a special key is 0 followed by one of
// these scan codes (KEYSTROK.HPP).
namespace key {
inline constexpr uint8_t F1 = 0x3B, F10 = 0x44;
inline constexpr uint8_t HOME = 0x47, UP = 0x48, PGUP = 0x49, LEFT = 0x4B, MIDDLE = 0x4C, RIGHT = 0x4D,
                         END = 0x4F, DOWN = 0x50, PGDN = 0x51, INSERT = 0x52, DEL = 0x53;
inline constexpr uint8_t CTL_LEFT = 0x73, CTL_RIGHT = 0x74, CTL_END = 0x75, CTL_PGDN = 0x76,
                         CTL_HOME = 0x77, CTL_PGUP = 0x84;
inline constexpr uint8_t RETURN = 0x0D, BKSPACE = 0x08, TAB = 0x09, ESC = 0x1B, BELL = 0x07, SPECIAL = 0x00;
// Alt+letter scan codes, A..Z.
inline constexpr uint8_t kAlt[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                     0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
}  // namespace key

class Machine {
public:
    // ---- text mode: as at B800:0000, (character, attribute) per cell
    uint8_t vram[kRows * kCols * 2] = {};
    int attr = 0x07;
    int winLeft = 1, winTop = 1, winRight = kCols, winBottom = kRows;
    int curX = 1, curY = 1;  // relative to the window
    int cursorType = _NORMALCURSOR;

    // ---- graphics mode
    bool graphics = false;
    uint8_t pixels[kGfxHeight][kGfxWidth] = {};
    int color = 1;
    int textDir = HORIZ_DIR, textSize = 1, justH = LEFT_TEXT, justV = TOP_TEXT;

    // ---- keyboard: the bytes getch() would return, oldest first
    std::deque<uint8_t> keys;
    std::coroutine_handle<> keyWaiter;
    std::coroutine_handle<> frameWaiter;  // a task that asked to let a frame be drawn

    int pendingBeeps = 0;
    uint64_t changes = 0;  // bumped on every change to the display

    Machine() { reset(); }
    void reset();

    // Host side: queue key bytes and wake a task waiting in getch().
    void push_key(uint8_t ascii);
    void push_special(uint8_t scan);
    // Resumes whichever task is waiting and can run. Returns true if one ran.
    bool pump();
};

// The one machine the classic interface uses.
Machine& machine();

// ---- conio (text mode) -----------------------------------------------------
int gettext(int left, int top, int right, int bottom, void* dest);
int puttext(int left, int top, int right, int bottom, const void* src);
void clrscr();
void textattr(int attribute);
void textcolor(int c);
void textbackground(int c);
void window(int left, int top, int right, int bottom);
void gotoxy(int x, int y);
int wherex();
int wherey();
void putch(int c);
void cputs(const char* s);
void _setcursortype(int type);
void beep();  // putchar(BELL)

// ---- keyboard ---------------------------------------------------------------
// `co_await getch()`: the next byte, as Borland's getch() returned it.
struct GetchAwaiter {
    bool await_ready() const noexcept { return !machine().keys.empty(); }
    void await_suspend(std::coroutine_handle<> h) noexcept { machine().keyWaiter = h; }
    int await_resume() {
        auto& m = machine();
        const int k = m.keys.front();
        m.keys.pop_front();
        return k;
    }
};
inline GetchAwaiter getch() { return {}; }
inline bool kbhit() { return !machine().keys.empty(); }

// `co_await next_frame()`: let the host draw the screen and collect input,
// for long loops that poll kbhit().
struct FrameAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept { machine().frameWaiter = h; }
    void await_resume() noexcept {}
};
inline FrameAwaiter next_frame() { return {}; }

// ---- BGI graphics (VGA high resolution) -------------------------------------
void initgraph(int* driver, int* mode, const char* path);
void closegraph();
int getgraphmode();
void setgraphmode(int mode);
void restorecrtmode();
void cleardevice();
int getmaxx();
int getmaxy();
void setcolor(int c);
void putpixel(int x, int y, int c);
void line(int x1, int y1, int x2, int y2);
void settextstyle(int font, int direction, int size);
void settextjustify(int horiz, int vert);
int textwidth(const char* s);
int textheight(const char* s);
// BGI text takes code page 437 bytes, as in DOS: pass cp437("...") for text
// with characters such as "┴".
void outtextxy(int x, int y, const char* s);

// Glyph rows for host rendering and BGI text.
const uint16_t* vga_glyph(uint8_t c);  // 16 rows, 9 bits each, MSB = left
const uint8_t* bios_glyph(uint8_t c);  // 8 rows, 8 bits each, MSB = left
const uint8_t* bgi_glyph(uint8_t c);   // BGI's DEFAULT_FONT, same layout

}  // namespace classic
