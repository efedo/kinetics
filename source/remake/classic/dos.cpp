#include "classic/dos.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace classic {
namespace {

#include "classic/fonts/bios8x8.inc"
#include "classic/fonts/bgi8x8.inc"
#include "classic/fonts/vga9x16.inc"

Machine g_machine;

uint8_t* cell(Machine& m, int x, int y) { return &m.vram[((y - 1) * kCols + (x - 1)) * 2]; }

void clear_text(Machine& m, int attribute) {
    for (int i = 0; i < kRows * kCols; ++i) {
        m.vram[i * 2] = ' ';
        m.vram[i * 2 + 1] = static_cast<uint8_t>(attribute);
    }
    ++m.changes;
}

void clear_pixels(Machine& m) {
    std::memset(m.pixels, 0, sizeof m.pixels);
    ++m.changes;
}

void reset_graphics_state(Machine& m) {
    m.color = 1;
    m.textDir = HORIZ_DIR;
    m.textSize = 1;
    m.justH = LEFT_TEXT;
    m.justV = TOP_TEXT;
}

// A BIOS mode set back to 80x25 colour text clears the screen.
void text_mode(Machine& m) {
    m.graphics = false;
    clear_text(m, 0x07);
}

void scroll_window(Machine& m) {
    for (int y = m.winTop; y < m.winBottom; ++y)
        for (int x = m.winLeft; x <= m.winRight; ++x) {
            uint8_t* to = cell(m, x, y);
            const uint8_t* from = cell(m, x, y + 1);
            to[0] = from[0];
            to[1] = from[1];
        }
    for (int x = m.winLeft; x <= m.winRight; ++x) {
        uint8_t* c = cell(m, x, m.winBottom);
        c[0] = ' ';
        c[1] = static_cast<uint8_t>(m.attr);
    }
}

}  // namespace

Machine& machine() { return g_machine; }

void Machine::reset() {
    clear_text(*this, 0x07);
    attr = 0x07;
    winLeft = winTop = 1;
    winRight = kCols;
    winBottom = kRows;
    curX = curY = 1;
    cursorType = _NORMALCURSOR;
    graphics = false;
    clear_pixels(*this);
    reset_graphics_state(*this);
    keys.clear();
    keyWaiter = {};
    frameWaiter = {};
    pendingBeeps = 0;
}

void Machine::push_key(uint8_t ascii) { keys.push_back(ascii); }

void Machine::push_special(uint8_t scan) {
    keys.push_back(key::SPECIAL);
    keys.push_back(scan);
}

bool Machine::pump() {
    bool ran = false;
    // A task resumed here may wait for the next key, which may already be
    // queued, so keep going until the queue is empty or nobody waits.
    while (keyWaiter && !keys.empty()) {
        auto h = keyWaiter;
        keyWaiter = {};
        h.resume();
        ran = true;
    }
    if (frameWaiter) {
        auto h = frameWaiter;
        frameWaiter = {};
        h.resume();
        ran = true;
        while (keyWaiter && !keys.empty()) {
            auto k = keyWaiter;
            keyWaiter = {};
            k.resume();
        }
    }
    return ran;
}

// ---- conio -------------------------------------------------------------------

int gettext(int left, int top, int right, int bottom, void* dest) {
    auto& m = machine();
    if (left < 1 || top < 1 || right > kCols || bottom > kRows || left > right || top > bottom) return 0;
    auto* out = static_cast<uint8_t*>(dest);
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            const uint8_t* c = cell(m, x, y);
            *out++ = c[0];
            *out++ = c[1];
        }
    return 1;
}

int puttext(int left, int top, int right, int bottom, const void* src) {
    auto& m = machine();
    if (left < 1 || top < 1 || right > kCols || bottom > kRows || left > right || top > bottom) return 0;
    const auto* in = static_cast<const uint8_t*>(src);
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            uint8_t* c = cell(m, x, y);
            c[0] = *in++;
            c[1] = *in++;
        }
    ++m.changes;
    return 1;
}

void clrscr() {
    auto& m = machine();
    for (int y = m.winTop; y <= m.winBottom; ++y)
        for (int x = m.winLeft; x <= m.winRight; ++x) {
            uint8_t* c = cell(m, x, y);
            c[0] = ' ';
            c[1] = static_cast<uint8_t>(m.attr);
        }
    m.curX = m.curY = 1;
    ++m.changes;
}

void textattr(int attribute) { machine().attr = attribute & 0xFF; }
void textcolor(int c) { auto& m = machine(); m.attr = (m.attr & 0x70) | (c & 0x8F); }
void textbackground(int c) { auto& m = machine(); m.attr = (m.attr & 0x8F) | ((c & 0x07) << 4); }

void window(int left, int top, int right, int bottom) {
    if (left < 1 || top < 1 || right > kCols || bottom > kRows || left > right || top > bottom) return;
    auto& m = machine();
    m.winLeft = left;
    m.winTop = top;
    m.winRight = right;
    m.winBottom = bottom;
    m.curX = m.curY = 1;
}

void gotoxy(int x, int y) {
    auto& m = machine();
    if (x < 1 || y < 1 || x > m.winRight - m.winLeft + 1 || y > m.winBottom - m.winTop + 1) return;
    m.curX = x;
    m.curY = y;
    ++m.changes;
}

int wherex() { return machine().curX; }
int wherey() { return machine().curY; }

void putch(int ch) {
    auto& m = machine();
    const int width = m.winRight - m.winLeft + 1, height = m.winBottom - m.winTop + 1;
    switch (ch) {
        case '\r': m.curX = 1; break;
        case '\n':
            if (++m.curY > height) {
                scroll_window(m);
                m.curY = height;
            }
            break;
        case key::BELL: beep(); break;
        case key::BKSPACE: if (m.curX > 1) --m.curX; break;
        default: {
            uint8_t* c = cell(m, m.winLeft + m.curX - 1, m.winTop + m.curY - 1);
            c[0] = static_cast<uint8_t>(ch);
            c[1] = static_cast<uint8_t>(m.attr);
            if (++m.curX > width) {
                m.curX = 1;
                if (++m.curY > height) {
                    scroll_window(m);
                    m.curY = height;
                }
            }
        }
    }
    ++m.changes;
}

void cputs(const char* s) {
    while (*s) putch(static_cast<unsigned char>(*s++));
}

void _setcursortype(int type) {
    machine().cursorType = type;
    ++machine().changes;
}

void beep() { ++machine().pendingBeeps; }

// ---- BGI -----------------------------------------------------------------------

void initgraph(int* driver, int* mode, const char*) {
    if (driver) *driver = VGA;
    if (mode) *mode = VGAHI;
    auto& m = machine();
    m.graphics = true;
    clear_pixels(m);
    reset_graphics_state(m);
}

void closegraph() { text_mode(machine()); }
int getgraphmode() { return VGAHI; }

void setgraphmode(int) {
    auto& m = machine();
    m.graphics = true;
    clear_pixels(m);
    reset_graphics_state(m);
}

void restorecrtmode() { text_mode(machine()); }
void cleardevice() { clear_pixels(machine()); }
int getmaxx() { return kGfxWidth - 1; }
int getmaxy() { return kGfxHeight - 1; }
void setcolor(int c) { machine().color = c ? 1 : 0; }

void putpixel(int x, int y, int c) {
    if (x < 0 || y < 0 || x >= kGfxWidth || y >= kGfxHeight) return;
    machine().pixels[y][x] = c ? 1 : 0;
}

void line(int x1, int y1, int x2, int y2) {
    auto& m = machine();
    const int dx = std::abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    const int dy = -std::abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    // Guard against absurd coordinates from a degenerate plot scale.
    if (dx > 100000 || dy < -100000) return;
    int err = dx + dy;
    for (;;) {
        putpixel(x1, y1, m.color);
        if (x1 == x2 && y1 == y2) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
    ++m.changes;
}

void settextstyle(int, int direction, int size) {
    auto& m = machine();
    m.textDir = direction == VERT_DIR ? VERT_DIR : HORIZ_DIR;
    m.textSize = std::clamp(size, 1, 10);
}

void settextjustify(int horiz, int vert) {
    machine().justH = horiz;
    machine().justV = vert;
}

int textwidth(const char* s) { return 8 * machine().textSize * static_cast<int>(std::strlen(s)); }
int textheight(const char*) { return 8 * machine().textSize; }

void outtextxy(int x, int y, const char* s) {
    auto& m = machine();
    const int size = m.textSize;
    const int along = textwidth(s);  // length along the text
    const int across = 8 * size;     // character height
    // Measured against v3.51 in DOSBox: horizontally centred text starts
    // one pixel right of x - width / 2; vertically centred text at
    // y - height / 2.
    auto offset = [](int just, int extent) {
        return just == CENTER_TEXT ? extent / 2 - 1 : (just == RIGHT_TEXT ? extent : 0);
    };
    const int n = static_cast<int>(std::strlen(s));
    if (m.textDir == HORIZ_DIR) {
        const int left = x - offset(m.justH, along);
        // Vertical: TOP_TEXT (2) puts y at the top, CENTER_TEXT (1) in the
        // middle, BOTTOM_TEXT (0) at the bottom.
        const int top = y - (m.justV == TOP_TEXT ? 0 : (m.justV == CENTER_TEXT ? across / 2 : across));
        for (int k = 0; k < n; ++k) {
            const uint8_t* g = bgi_glyph(static_cast<uint8_t>(s[k]));
            for (int gy = 0; gy < 8; ++gy)
                for (int gx = 0; gx < 8; ++gx)
                    if (g[gy] & (0x80 >> gx))
                        for (int i = 0; i < size; ++i)
                            for (int j = 0; j < size; ++j)
                                putpixel(left + (k * 8 + gx) * size + j, top + gy * size + i, m.color);
        }
    } else {
        // Rotated 90 degrees anticlockwise: reads upwards, tops of the
        // letters to the left.
        const int left = x - offset(m.justH, across);
        const int bottom = y + (m.justV == TOP_TEXT ? along : (m.justV == CENTER_TEXT ? along / 2 : 0));
        for (int k = 0; k < n; ++k) {
            const uint8_t* g = bgi_glyph(static_cast<uint8_t>(s[k]));
            for (int gy = 0; gy < 8; ++gy)
                for (int gx = 0; gx < 8; ++gx)
                    if (g[gy] & (0x80 >> gx))
                        for (int i = 0; i < size; ++i)
                            for (int j = 0; j < size; ++j)
                                putpixel(left + gy * size + j, bottom - 1 - ((k * 8 + gx) * size + i), m.color);
        }
    }
    ++m.changes;
}

const uint16_t* vga_glyph(uint8_t c) { return kVga9x16[c]; }
const uint8_t* bios_glyph(uint8_t c) { return kBios8x8[c]; }

// As the BGI drivers' text routine does it: '0' is drawn as 'O', 'A' to
// 'z' come from the drivers' own table, the rest from the ROM BIOS font.
const uint8_t* bgi_glyph(uint8_t c) {
    if (c == '0') c = 'O';
    if (c >= 'A' && c <= 'z') return kBgi8x8[c - 'A'];
    return kBios8x8[c];
}

}  // namespace classic
