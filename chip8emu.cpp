/*
 * chip8emu 0.1.1 — CHIP-8 emulator with mGBA-style Win32 GUI
 * Window: 600x400 | Text: blue | Buttons: black | Built for Windows
 *
 * Build (MinGW g++):
 *   g++ chip8emu.cpp -o chip8emu.exe -mwindows -lgdi32 -lcomdlg32 -luser32 -O2 -std=c++17
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

// =============================================================================
// Constants / theme (mGBA blue hue)
// =============================================================================
static constexpr int SCREEN_W = 64;
static constexpr int SCREEN_H = 32;
static constexpr int MEM_SIZE = 4096;
static constexpr int PROGRAM_START = 0x200;
static constexpr int FONT_START = 0x50;
static constexpr int STACK_LIMIT = 16;
static constexpr int WINDOW_W = 600;
static constexpr int WINDOW_H = 400;
static constexpr int TARGET_FPS = 60;
static constexpr UINT_PTR TIMER_ID = 1;

static constexpr COLORREF COL_BG       = RGB(18, 28, 42);   // blue-dark
static constexpr COLORREF COL_PANEL    = RGB(28, 40, 58);
static constexpr COLORREF COL_STATUS   = RGB(12, 20, 32);
static constexpr COLORREF COL_TEXT     = RGB(90, 155, 212); // blue text
static constexpr COLORREF COL_TEXT_DIM = RGB(70, 120, 170);
static constexpr COLORREF COL_ACCENT   = RGB(90, 155, 212);
static constexpr COLORREF COL_BTN_BG   = RGB(0, 0, 0);      // black buttons
static constexpr COLORREF COL_SCREEN_ON  = RGB(168, 230, 168);
static constexpr COLORREF COL_SCREEN_OFF = RGB(13, 26, 13);

static const char* APP_NAME = "chip8emu 0.1.1";
static const char* APP_CLASS = "Chip8EmuWinClass";

static const uint8_t FONT_DATA[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, 0x20, 0x60, 0x20, 0x20, 0x70,
    0xF0, 0x10, 0xF0, 0x80, 0xF0, 0xF0, 0x10, 0xF0, 0x10, 0xF0,
    0x90, 0x90, 0xF0, 0x10, 0x10, 0xF0, 0x80, 0xF0, 0x10, 0xF0,
    0xF0, 0x80, 0xF0, 0x90, 0xF0, 0xF0, 0x10, 0x20, 0x40, 0x40,
    0xF0, 0x90, 0xF0, 0x90, 0xF0, 0xF0, 0x90, 0xF0, 0x10, 0xF0,
    0xF0, 0x90, 0xF0, 0x90, 0x90, 0xE0, 0x90, 0xE0, 0x90, 0xE0,
    0xF0, 0x80, 0x80, 0x80, 0xF0, 0xE0, 0x90, 0x90, 0x90, 0xE0,
    0xF0, 0x80, 0xF0, 0x80, 0xF0, 0xF0, 0x80, 0xF0, 0x80, 0x80,
};

// CHIP-8 keypad -> PC keys
// 1 2 3 C    1 2 3 4
// 4 5 6 D    Q W E R
// 7 8 9 E    A S D F
// A 0 B F    Z X C V

enum : int {
    ID_BTN_OPEN = 1001,
    ID_BTN_PAUSE = 1002,
    ID_BTN_RESET = 1003,
    ID_MENU_OPEN = 2001,
    ID_MENU_EXIT = 2002,
    ID_MENU_PAUSE = 2003,
    ID_MENU_RESET = 2004,
    ID_MENU_CONTROLS = 2005,
    ID_MENU_ABOUT = 2006,
};

// =============================================================================
// CHIP-8 Core
// =============================================================================
struct Chip8 {
    uint8_t memory[MEM_SIZE]{};
    uint8_t v[16]{};
    uint16_t index = 0;
    uint16_t pc = PROGRAM_START;
    uint16_t stack[STACK_LIMIT]{};
    int sp = 0;
    uint8_t delay_timer = 0;
    uint8_t sound_timer = 0;
    uint8_t display[SCREEN_H][SCREEN_W]{};
    uint8_t keys[16]{};
    bool draw_flag = true;
    bool halted = false;
    int wait_key = -1;
    bool quirk_vf_reset = true;
    bool quirk_shift_vy = false;
    bool quirk_mem_increment = false;
    bool quirk_jump_vx = false;
    std::vector<uint8_t> rom;
    std::mt19937 rng{std::random_device{}()};

    void reset() {
        std::memset(memory, 0, sizeof(memory));
        std::memset(v, 0, sizeof(v));
        index = 0;
        pc = PROGRAM_START;
        sp = 0;
        delay_timer = 0;
        sound_timer = 0;
        std::memset(display, 0, sizeof(display));
        std::memset(keys, 0, sizeof(keys));
        draw_flag = true;
        halted = false;
        wait_key = -1;
        std::memcpy(memory + FONT_START, FONT_DATA, sizeof(FONT_DATA));
        if (!rom.empty()) {
            const size_t n = (std::min)(rom.size(), size_t(MEM_SIZE - PROGRAM_START));
            std::memcpy(memory + PROGRAM_START, rom.data(), n);
        }
    }

    bool load_rom(const uint8_t* data, size_t len) {
        if (!data || len == 0) return false;
        if (len > size_t(MEM_SIZE - PROGRAM_START))
            len = size_t(MEM_SIZE - PROGRAM_START);
        rom.assign(data, data + len);
        reset();
        return true;
    }

    void draw_sprite(uint8_t vx, uint8_t vy, uint8_t height) {
        const int x0 = vx % SCREEN_W;
        const int y0 = vy % SCREEN_H;
        uint8_t collision = 0;
        for (int row = 0; row < height; ++row) {
            const int py = y0 + row;
            if (py >= SCREEN_H) break;
            const uint8_t byte = memory[(index + row) & 0xFFF];
            if (!byte) continue;
            for (int col = 0; col < 8; ++col) {
                if (!(byte & (0x80 >> col))) continue;
                const int px = x0 + col;
                if (px >= SCREEN_W) break;
                if (display[py][px]) {
                    display[py][px] = 0;
                    collision = 1;
                } else {
                    display[py][px] = 1;
                }
            }
        }
        v[0xF] = collision;
        draw_flag = true;
    }

    void execute_cycle() {
        if (halted) return;
        const uint16_t opcode = (uint16_t(memory[pc & 0xFFF]) << 8) |
                                memory[(pc + 1) & 0xFFF];
        pc = (pc + 2) & 0xFFF;

        const uint16_t nnn = opcode & 0x0FFF;
        const uint8_t n = opcode & 0x000F;
        const uint8_t x = (opcode & 0x0F00) >> 8;
        const uint8_t y = (opcode & 0x00F0) >> 4;
        const uint8_t kk = opcode & 0x00FF;
        const uint16_t top = opcode & 0xF000;

        if (opcode == 0x00E0) {
            std::memset(display, 0, sizeof(display));
            draw_flag = true;
        } else if (opcode == 0x00EE) {
            if (sp > 0) {
                pc = stack[--sp] & 0xFFF;
            } else {
                halted = true;
            }
        } else if (top == 0x1000) {
            pc = nnn;
        } else if (top == 0x2000) {
            if (sp >= STACK_LIMIT) {
                halted = true;
            } else {
                stack[sp++] = pc;
                pc = nnn;
            }
        } else if (top == 0x3000) {
            if (v[x] == kk) pc = (pc + 2) & 0xFFF;
        } else if (top == 0x4000) {
            if (v[x] != kk) pc = (pc + 2) & 0xFFF;
        } else if (top == 0x5000 && n == 0) {
            if (v[x] == v[y]) pc = (pc + 2) & 0xFFF;
        } else if (top == 0x6000) {
            v[x] = kk;
        } else if (top == 0x7000) {
            v[x] = uint8_t(v[x] + kk);
        } else if (top == 0x8000) {
            if (n == 0x0) {
                v[x] = v[y];
            } else if (n == 0x1) {
                v[x] |= v[y];
                if (quirk_vf_reset) v[0xF] = 0;
            } else if (n == 0x2) {
                v[x] &= v[y];
                if (quirk_vf_reset) v[0xF] = 0;
            } else if (n == 0x3) {
                v[x] ^= v[y];
                if (quirk_vf_reset) v[0xF] = 0;
            } else if (n == 0x4) {
                const int result = v[x] + v[y];
                v[x] = uint8_t(result);
                v[0xF] = result > 0xFF ? 1 : 0;
            } else if (n == 0x5) {
                const uint8_t borrow = v[x] >= v[y] ? 1 : 0;
                v[x] = uint8_t(v[x] - v[y]);
                v[0xF] = borrow;
            } else if (n == 0x6) {
                const uint8_t src = quirk_shift_vy ? v[y] : v[x];
                const uint8_t flag = src & 0x1;
                v[x] = src >> 1;
                v[0xF] = flag;
            } else if (n == 0x7) {
                const uint8_t borrow = v[y] >= v[x] ? 1 : 0;
                v[x] = uint8_t(v[y] - v[x]);
                v[0xF] = borrow;
            } else if (n == 0xE) {
                const uint8_t src = quirk_shift_vy ? v[y] : v[x];
                const uint8_t flag = (src >> 7) & 0x1;
                v[x] = uint8_t(src << 1);
                v[0xF] = flag;
            }
        } else if (top == 0x9000 && n == 0) {
            if (v[x] != v[y]) pc = (pc + 2) & 0xFFF;
        } else if (top == 0xA000) {
            index = nnn;
        } else if (top == 0xB000) {
            const uint8_t base = quirk_jump_vx ? v[x] : v[0];
            pc = (nnn + base) & 0xFFF;
        } else if (top == 0xC000) {
            std::uniform_int_distribution<int> dist(0, 255);
            v[x] = uint8_t(dist(rng) & kk);
        } else if (top == 0xD000) {
            draw_sprite(v[x], v[y], n);
        } else if (top == 0xE000) {
            const uint8_t key = v[x] & 0xF;
            if (kk == 0x9E) {
                if (keys[key]) pc = (pc + 2) & 0xFFF;
            } else if (kk == 0xA1) {
                if (!keys[key]) pc = (pc + 2) & 0xFFF;
            }
        } else if (top == 0xF000) {
            if (kk == 0x07) {
                v[x] = delay_timer;
            } else if (kk == 0x0A) {
                if (wait_key < 0) {
                    for (int i = 0; i < 16; ++i) {
                        if (keys[i]) {
                            wait_key = i;
                            break;
                        }
                    }
                    pc = (pc - 2) & 0xFFF;
                } else if (keys[wait_key]) {
                    pc = (pc - 2) & 0xFFF;
                } else {
                    v[x] = uint8_t(wait_key);
                    wait_key = -1;
                }
            } else if (kk == 0x15) {
                delay_timer = v[x];
            } else if (kk == 0x18) {
                sound_timer = v[x];
            } else if (kk == 0x1E) {
                index = (index + v[x]) & 0xFFF;
            } else if (kk == 0x29) {
                index = (FONT_START + (v[x] & 0xF) * 5) & 0xFFF;
            } else if (kk == 0x33) {
                const uint8_t value = v[x];
                const uint16_t i = index;
                memory[i & 0xFFF] = value / 100;
                memory[(i + 1) & 0xFFF] = (value / 10) % 10;
                memory[(i + 2) & 0xFFF] = value % 10;
            } else if (kk == 0x55) {
                for (int i = 0; i <= x; ++i)
                    memory[(index + i) & 0xFFF] = v[i];
                if (quirk_mem_increment)
                    index = (index + x + 1) & 0xFFF;
            } else if (kk == 0x65) {
                for (int i = 0; i <= x; ++i)
                    v[i] = memory[(index + i) & 0xFFF];
                if (quirk_mem_increment)
                    index = (index + x + 1) & 0xFFF;
            }
        }
    }

    void decrement_timers() {
        if (delay_timer > 0) --delay_timer;
        if (sound_timer > 0) --sound_timer;
    }
};

// =============================================================================
// App state
// =============================================================================
struct App {
    HWND hwnd = nullptr;
    HWND btnOpen = nullptr;
    HWND btnPause = nullptr;
    HWND btnReset = nullptr;
    HWND status = nullptr;
    HBRUSH bgBrush = nullptr;
    HBRUSH panelBrush = nullptr;
    HBRUSH btnBrush = nullptr;
    HBRUSH statusBrush = nullptr;
    HFONT uiFont = nullptr;
    Chip8 chip8;
    bool rom_loaded = false;
    bool paused = false;
    int cycles_per_frame = 12;
    int zoom = 8;
    float fps = 0.f;
    int frame_count = 0;
    DWORD last_fps_tick = 0;
    std::string rom_name = "No ROM loaded";
    BITMAPINFO bmi{};
    uint32_t pixels[SCREEN_W * SCREEN_H]{};
};

static App g;

static int MapVkToChip8(WPARAM vk) {
    switch (vk) {
    case '1': return 0x1; case '2': return 0x2; case '3': return 0x3; case '4': return 0xC;
    case 'Q': return 0x4; case 'W': return 0x5; case 'E': return 0x6; case 'R': return 0xD;
    case 'A': return 0x7; case 'S': return 0x8; case 'D': return 0x9; case 'F': return 0xE;
    case 'Z': return 0xA; case 'X': return 0x0; case 'C': return 0xB; case 'V': return 0xF;
    default: return -1;
    }
}

static void UpdateStatus() {
    if (!g.status) return;
    const char* state = "Stopped";
    if (g.rom_loaded) {
        if (g.chip8.halted) state = "Halted";
        else if (g.paused) state = "Paused";
        else state = "Running";
    }
    char buf[256];
    // snprintf: wsprintfA does not support floating-point and can crash on boot
    std::snprintf(buf, sizeof(buf),
                  "  %s  |  FPS: %.0f  |  %s  |  %d cyc/f  |  Zoom: %dx%s",
                  g.rom_name.c_str(), double(g.fps), state, g.cycles_per_frame, g.zoom,
                  g.chip8.sound_timer > 0 ? "  [SND]" : "");
    SetWindowTextA(g.status, buf);
}

static void RebuildPixels() {
    for (int y = 0; y < SCREEN_H; ++y) {
        for (int x = 0; x < SCREEN_W; ++x) {
            const COLORREF c = g.chip8.display[y][x] ? COL_SCREEN_ON : COL_SCREEN_OFF;
            // DIB is BGRX bottom-up when biHeight > 0; we use top-down (negative height)
            g.pixels[y * SCREEN_W + x] =
                (GetRValue(c)) | (GetGValue(c) << 8) | (GetBValue(c) << 16);
        }
    }
}

static RECT DisplayRect(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    rc.top += 40;      // toolbar
    rc.bottom -= 24;   // status
    rc.left += 8;
    rc.right -= 8;
    return rc;
}

static void FitZoom(HWND hwnd) {
    const RECT dr = DisplayRect(hwnd);
    const int aw = (std::max)(1, int(dr.right - dr.left));
    const int ah = (std::max)(1, int(dr.bottom - dr.top));
    g.zoom = (std::max)(1, (std::min)(32, (std::min)(aw / SCREEN_W, ah / SCREEN_H)));
}

static void OpenRomDialog(HWND hwnd) {
    char path[MAX_PATH] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter =
        "CHIP-8 ROMs (*.ch8;*.c8;*.rom;*.bin)\0*.ch8;*.c8;*.rom;*.bin\0"
        "All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = "Open CHIP-8 ROM";
    if (!GetOpenFileNameA(&ofn)) return;

    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        MessageBoxA(hwnd, "Failed to open ROM file.", "Error", MB_ICONERROR);
        return;
    }
    const DWORD size = GetFileSize(h, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0) {
        CloseHandle(h);
        MessageBoxA(hwnd, "ROM file is empty or unreadable.", "Error", MB_ICONERROR);
        return;
    }
    std::vector<uint8_t> data(size);
    DWORD read = 0;
    const BOOL ok = ReadFile(h, data.data(), size, &read, nullptr);
    CloseHandle(h);
    if (!ok || read == 0 || !g.chip8.load_rom(data.data(), read)) {
        MessageBoxA(hwnd, "Failed to load ROM.", "Error", MB_ICONERROR);
        return;
    }

    const char* base = strrchr(path, '\\');
    g.rom_name = base ? (base + 1) : path;
    g.rom_loaded = true;
    g.paused = false;
    char title[256];
    std::snprintf(title, sizeof(title), "%s - %s", APP_NAME, g.rom_name.c_str());
    SetWindowTextA(hwnd, title);
    RebuildPixels();
    UpdateStatus();
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT client;
    GetClientRect(hwnd, &client);

    // Toolbar background
    RECT toolbar = client;
    toolbar.bottom = 40;
    FillRect(hdc, &toolbar, g.bgBrush);

    // Panel + display
    RECT body = client;
    body.top = 40;
    body.bottom -= 24;
    FillRect(hdc, &body, g.panelBrush);

    RECT dr = DisplayRect(hwnd);
    const int dw = SCREEN_W * g.zoom;
    const int dh = SCREEN_H * g.zoom;
    const int x = dr.left + (std::max)(0, int(dr.right - dr.left - dw) / 2);
    const int y = dr.top + (std::max)(0, int(dr.bottom - dr.top - dh) / 2);

    // Accent border
    HPEN pen = CreatePen(PS_SOLID, 1, COL_ACCENT);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, x - 1, y - 1, x + dw + 1, y + dh + 1);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    StretchDIBits(hdc, x, y, dw, dh,
                  0, 0, SCREEN_W, SCREEN_H,
                  g.pixels, &g.bmi, DIB_RGB_COLORS, SRCCOPY);

    EndPaint(hwnd, &ps);
}

static void StyleBlackButton(HWND btn) {
    // Owner-draw via WM_CTLCOLORBTN + custom draw in subclass if needed.
    // Win32 buttons with BS_OWNERDRAW for black bg + blue text.
    LONG style = GetWindowLong(btn, GWL_STYLE);
    SetWindowLong(btn, GWL_STYLE, style | BS_OWNERDRAW);
}

static void DrawOwnerButton(DRAWITEMSTRUCT* dis) {
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const COLORREF bg = pressed ? RGB(20, 20, 20) : COL_BTN_BG;
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(dis->hDC, &dis->rcItem, br);
    DeleteObject(br);

    HPEN pen = CreatePen(PS_SOLID, 1, COL_ACCENT);
    HGDIOBJ oldPen = SelectObject(dis->hDC, pen);
    HGDIOBJ oldBrush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top,
              dis->rcItem.right, dis->rcItem.bottom);
    SelectObject(dis->hDC, oldBrush);
    SelectObject(dis->hDC, oldPen);
    DeleteObject(pen);

    char text[64] = {};
    GetWindowTextA(dis->hwndItem, text, sizeof(text));
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, COL_TEXT);
    if (g.uiFont) SelectObject(dis->hDC, g.uiFont);
    DrawTextA(dis->hDC, text, -1, &dis->rcItem,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g.hwnd = hwnd;
        g.bgBrush = CreateSolidBrush(COL_BG);
        g.panelBrush = CreateSolidBrush(COL_PANEL);
        g.btnBrush = CreateSolidBrush(COL_BTN_BG);
        g.statusBrush = CreateSolidBrush(COL_STATUS);
        g.uiFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        g.bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        g.bmi.bmiHeader.biWidth = SCREEN_W;
        g.bmi.bmiHeader.biHeight = -SCREEN_H; // top-down
        g.bmi.bmiHeader.biPlanes = 1;
        g.bmi.bmiHeader.biBitCount = 32;
        g.bmi.bmiHeader.biCompression = BI_RGB;

        g.chip8.reset();
        RebuildPixels();

        HMENU menubar = CreateMenu();
        HMENU fileMenu = CreatePopupMenu();
        AppendMenuA(fileMenu, MF_STRING, ID_MENU_OPEN, "&Open ROM...\tCtrl+O");
        AppendMenuA(fileMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(fileMenu, MF_STRING, ID_MENU_EXIT, "E&xit");
        AppendMenuA(menubar, MF_POPUP, (UINT_PTR)fileMenu, "&File");

        HMENU emuMenu = CreatePopupMenu();
        AppendMenuA(emuMenu, MF_STRING, ID_MENU_PAUSE, "&Pause/Resume\tP");
        AppendMenuA(emuMenu, MF_STRING, ID_MENU_RESET, "&Reset\tCtrl+R");
        AppendMenuA(menubar, MF_POPUP, (UINT_PTR)emuMenu, "&Emulation");

        HMENU helpMenu = CreatePopupMenu();
        AppendMenuA(helpMenu, MF_STRING, ID_MENU_CONTROLS, "&Controls");
        AppendMenuA(helpMenu, MF_STRING, ID_MENU_ABOUT, "&About");
        AppendMenuA(menubar, MF_POPUP, (UINT_PTR)helpMenu, "&Help");
        SetMenu(hwnd, menubar);

        g.btnOpen = CreateWindowA("BUTTON", "Open ROM",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            8, 8, 100, 24, hwnd, (HMENU)ID_BTN_OPEN, nullptr, nullptr);
        g.btnPause = CreateWindowA("BUTTON", "Pause",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            116, 8, 80, 24, hwnd, (HMENU)ID_BTN_PAUSE, nullptr, nullptr);
        g.btnReset = CreateWindowA("BUTTON", "Reset",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            204, 8, 80, 24, hwnd, (HMENU)ID_BTN_RESET, nullptr, nullptr);

        for (HWND b : {g.btnOpen, g.btnPause, g.btnReset}) {
            if (g.uiFont) SendMessage(b, WM_SETFONT, (WPARAM)g.uiFont, TRUE);
        }

        g.status = CreateWindowA("STATIC", "  No ROM loaded",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            0, WINDOW_H - 24, WINDOW_W, 24, hwnd, nullptr, nullptr, nullptr);
        if (g.uiFont) SendMessage(g.status, WM_SETFONT, (WPARAM)g.uiFont, TRUE);

        FitZoom(hwnd);
        g.last_fps_tick = GetTickCount();
        SetTimer(hwnd, TIMER_ID, 1000 / TARGET_FPS, nullptr);
        UpdateStatus();
        return 0;
    }

    case WM_SIZE: {
        const int w = LOWORD(lParam);
        const int h = HIWORD(lParam);
        if (g.status)
            MoveWindow(g.status, 0, h - 24, w, 24, TRUE);
        FitZoom(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        Paint(hwnd);
        return 0;

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis && dis->CtlType == ODT_BUTTON) {
            DrawOwnerButton(dis);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, COL_TEXT);
        SetBkColor(hdc, COL_STATUS);
        return (LRESULT)g.statusBrush;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == ID_BTN_OPEN || id == ID_MENU_OPEN) {
            OpenRomDialog(hwnd);
        } else if (id == ID_BTN_PAUSE || id == ID_MENU_PAUSE) {
            if (g.rom_loaded) {
                g.paused = !g.paused;
                UpdateStatus();
            }
        } else if (id == ID_BTN_RESET || id == ID_MENU_RESET) {
            g.chip8.reset();
            g.paused = false;
            RebuildPixels();
            UpdateStatus();
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (id == ID_MENU_EXIT) {
            DestroyWindow(hwnd);
        } else if (id == ID_MENU_CONTROLS) {
            MessageBoxA(hwnd,
                "CHIP-8 Keypad        PC Keyboard\n"
                "1 2 3 C              1 2 3 4\n"
                "4 5 6 D              Q W E R\n"
                "7 8 9 E              A S D F\n"
                "A 0 B F              Z X C V\n\n"
                "P          Pause / Resume\n"
                "Ctrl+O     Open ROM\n"
                "Ctrl+R     Reset",
                "Controls", MB_OK | MB_ICONINFORMATION);
        } else if (id == ID_MENU_ABOUT) {
            MessageBoxA(hwnd,
                "chip8emu 0.1.1\n\n"
                "mGBA-inspired blue hue Win32 GUI\n"
                "600x400 default window\n"
                "Blue text, black buttons\n"
                "Full CHIP-8 opcode core",
                "About", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        if (wParam == 'P') {
            if (g.rom_loaded) {
                g.paused = !g.paused;
                UpdateStatus();
            }
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'O') {
            OpenRomDialog(hwnd);
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'R') {
            g.chip8.reset();
            g.paused = false;
            RebuildPixels();
            UpdateStatus();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        const int k = MapVkToChip8(wParam);
        if (k >= 0) g.chip8.keys[k] = 1;
        return 0;
    }

    case WM_KEYUP: {
        const int k = MapVkToChip8(wParam);
        if (k >= 0) g.chip8.keys[k] = 0;
        return 0;
    }

    case WM_TIMER: {
        if (wParam != TIMER_ID) break;
        if (g.rom_loaded && !g.paused && !g.chip8.halted) {
            for (int i = 0; i < g.cycles_per_frame; ++i) {
                g.chip8.execute_cycle();
                if (g.chip8.halted) break;
            }
            g.chip8.decrement_timers();
            if (g.chip8.draw_flag) {
                RebuildPixels();
                g.chip8.draw_flag = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            ++g.frame_count;
            const DWORD now = GetTickCount();
            if (now - g.last_fps_tick >= 1000) {
                g.fps = float(g.frame_count) * 1000.f / float(now - g.last_fps_tick);
                g.frame_count = 0;
                g.last_fps_tick = now;
                UpdateStatus();
            }
        }
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        if (g.bgBrush) DeleteObject(g.bgBrush);
        if (g.panelBrush) DeleteObject(g.panelBrush);
        if (g.btnBrush) DeleteObject(g.btnBrush);
        if (g.statusBrush) DeleteObject(g.statusBrush);
        if (g.uiFont) DeleteObject(g.uiFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = APP_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = CreateSolidBrush(COL_BG);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassA(&wc)) {
        MessageBoxA(nullptr, "RegisterClass failed.", APP_NAME, MB_ICONERROR);
        return 1;
    }

    RECT wr = {0, 0, WINDOW_W, WINDOW_H};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, TRUE);
    const int ww = wr.right - wr.left;
    const int wh = wr.bottom - wr.top;

    HWND hwnd = CreateWindowExA(
        0, APP_CLASS, APP_NAME,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, ww, wh,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        MessageBoxA(nullptr, "CreateWindow failed.", APP_NAME, MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nShow <= 0 ? SW_SHOW : nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return int(msg.wParam);
}
