// =============================================================================
//  桌面桌宠 - Desktop Pet v1.3.0
//  Win32 API + GDI+，四维属性系统（饥饿/清洁/心情/健康）
//  参考 QQ 宠物机制，支持多皮肤目录扫描
// =============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <ctime>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")

using namespace Gdiplus;

// =============================================================================
//  常量
// =============================================================================
constexpr int DISPLAY_SIZE    = 200;
constexpr int TIMER_STATUS    = 3;        // 状态刷新定时器 ID
constexpr int STATUS_TICK_MS  = 5000;     // 状态 tick 间隔 (5s)

// 四维属性衰减速率（参考 QQ 宠物：每 60 秒）
constexpr int HUNGER_DEC_TICK = 12;       // 12 tick = 60s, ↓5~8 饥饿
constexpr int CLEAN_DEC_TICK  = 12;       // 12 tick = 60s, ↓5~8 清洁
constexpr int MOOD_DEC_TICK   = 6;        // 6  tick = 30s, ↓2~4 心情

// QQ 宠物属性阈值
constexpr int HUNGER_THRESHOLD = 720;     // 饥饿 < 720 → 进入饥饿状态
constexpr int CLEAN_THRESHOLD  = 1080;    // 清洁 < 1080 → 进入脏污状态
constexpr int MOOD_THRESHOLD   = 100;     // 心情 < 100 → 心情低落

// 养护动作恢复量
constexpr int FEED_AMOUNT  = 1000;        // 喂食 +1000 饥饿
constexpr int BATH_AMOUNT  = 1000;        // 洗澡 +1000 清洁
constexpr int PLAY_AMOUNT  = 100;         // 玩耍 +100 心情

// 菜单 ID 范围
constexpr int MENU_STATE_BASE  = 1000;
constexpr int MENU_SKIN_BASE   = 2000;
constexpr int MENU_ACTION_BASE = 4000;     // 喂食/洗澡/玩耍
constexpr int ID_TOPMOST       = 9001;
constexpr int ID_QUIT          = 9002;

// =============================================================================
//  状态枚举（7 种状态）
// =============================================================================
enum PetState {
    STATE_IDLE     = 0,   // 待机/开心
    STATE_PLAYING  = 1,   // 玩耍
    STATE_EATING   = 2,   // 吃东西
    STATE_BATHING  = 3,   // 洗澡
    STATE_SICK     = 4,   // 生病
    STATE_ANGRY    = 5,   // 生气
    STATE_SLEEPING = 6,   // 睡觉
    STATE_COUNT    = 7
};

// 素材文件名映射
const wchar_t* STATE_FILENAMES[] = {
    L"idle.png",        // 待机/开心
    L"playing.png",     // 玩耍
    L"eating.png",      // 吃东西
    L"bathing.png",     // 洗澡
    L"sick.png",        // 生病
    L"angry.png",       // 生气
    L"sleeping.png",    // 睡觉
};
constexpr int BASE_STATE_COUNT = 7;

// =============================================================================
//  皮肤结构
// =============================================================================
struct SkinInfo {
    std::wstring name;
    std::wstring path;
};

// =============================================================================
//  资源结构
// =============================================================================
struct StateResource {
    HDC     hdc = NULL;
    HBITMAP hBmp = NULL;
    HBITMAP hOldBmp = NULL;
    void*   pBits = NULL;
};

// =============================================================================
//  全局变量
// =============================================================================
HINSTANCE       g_hInst = NULL;
HWND            g_hwnd  = NULL;
std::vector<SkinInfo> g_skins;
int             g_currentSkinIdx = 0;

StateResource   g_stateRes[STATE_COUNT];
PetState        g_currentState = STATE_IDLE;
bool            g_isDragging   = false;
POINT           g_dragOffset   = {0, 0};
bool            g_alwaysOnTop  = true;
std::wstring    g_exeDir;

// 四维属性
constexpr int MAX_HUNGER = 3000;
constexpr int MAX_CLEAN  = 3000;
constexpr int MAX_MOOD   = 1000;
constexpr int MAX_HEALTH = 5;

int g_hunger  = MAX_HUNGER;
int g_clean   = MAX_CLEAN;
int g_mood    = MAX_MOOD;
int g_health  = MAX_HEALTH;
int g_statusTick = 0;

// 疾病状态
bool g_isSick = false;
std::wstring g_sickName;

// =============================================================================
//  辅助函数
// =============================================================================
std::wstring GetExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring full(path);
    size_t pos = full.find_last_of(L"\\/");
    if (pos != std::wstring::npos) full = full.substr(0, pos);
    return full;
}

// =============================================================================
//  释放资源
// =============================================================================
void FreeStateResource(StateResource& res) {
    if (res.hdc) {
        if (res.hOldBmp) SelectObject(res.hdc, res.hOldBmp);
        DeleteDC(res.hdc);
        res.hdc = NULL;
    }
    if (res.hBmp) {
        DeleteObject(res.hBmp);
        res.hBmp = NULL;
    }
    res.hOldBmp = NULL;
    res.pBits = NULL;
}

void FreeAllResources() {
    for (int i = 0; i < STATE_COUNT; i++) FreeStateResource(g_stateRes[i]);
}

// =============================================================================
//  加载单张 PNG → StateResource（缩放到 DISPLAY_SIZE）
// =============================================================================
bool LoadImageToResource(const std::wstring& fullPath, StateResource& res) {
    Image* pImage = Image::FromFile(fullPath.c_str());
    if (!pImage || pImage->GetLastStatus() != Ok) {
        delete pImage;
        return false;
    }

    HDC hdcScreen = GetDC(NULL);
    res.hdc = CreateCompatibleDC(hdcScreen);
    ReleaseDC(NULL, hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = DISPLAY_SIZE;
    bmi.bmiHeader.biHeight      = -DISPLAY_SIZE;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    res.hBmp = CreateDIBSection(res.hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    res.hOldBmp = (HBITMAP)SelectObject(res.hdc, res.hBmp);
    res.pBits = pBits;

    Graphics g(res.hdc);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.DrawImage(pImage, 0, 0, DISPLAY_SIZE, DISPLAY_SIZE);

    delete pImage;

    // 关键修复：GDI+ DrawImage 写入 DIB Section 的像素是非预乘 ARGB，
    // 但 UpdateLayeredWindow(AC_SRC_ALPHA) 期望预乘格式。
    // 必须手动预乘：R=R*A/255, G=G*A/255, B=B*A/255
    if (res.pBits) {
        BYTE* pixels = (BYTE*)res.pBits;
        for (int i = 0; i < DISPLAY_SIZE * DISPLAY_SIZE; i++) {
            BYTE* px = pixels + i * 4;
            BYTE a = px[3];          // Alpha 通道（byte 3 在 BGRX 布局中）
            if (a == 0) {
                px[0] = 0; px[1] = 0; px[2] = 0;  // 完全透明 → RGB 也归零
            } else {
                px[0] = (BYTE)((px[0] * a + 128) / 255);  // B
                px[1] = (BYTE)((px[1] * a + 128) / 255);  // G
                px[2] = (BYTE)((px[2] * a + 128) / 255);  // R
                // Alpha 保持不变
            }
        }
    }

    return true;
}

// =============================================================================
//  扫描 animal/ 子目录作为可用皮肤
// =============================================================================
bool ScanSkins() {
    g_skins.clear();
    std::wstring animalDir = g_exeDir + L"\\animal";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((animalDir + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            wcscmp(fd.cFileName, L".") != 0 &&
            wcscmp(fd.cFileName, L"..") != 0) {
            SkinInfo skin;
            skin.name = fd.cFileName;
            skin.path = animalDir + L"\\" + fd.cFileName;
            g_skins.push_back(skin);
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return !g_skins.empty();
}

// =============================================================================
//  加载指定皮肤（7 张状态 PNG）
// =============================================================================
bool LoadSkin(int idx) {
    if (idx < 0 || idx >= (int)g_skins.size()) return false;

    FreeAllResources();
    const SkinInfo& skin = g_skins[idx];

    for (int i = 0; i < BASE_STATE_COUNT; i++) {
        std::wstring fp = skin.path + L"\\" + STATE_FILENAMES[i];
        if (!LoadImageToResource(fp, g_stateRes[i])) {
            // 容错：找不到某个状态的图片就用 idle 代替
            LoadImageToResource(skin.path + L"\\idle.png", g_stateRes[i]);
        }
    }

    g_currentSkinIdx = idx;
    return true;
}

// =============================================================================
//  更新窗口图片 + 四维状态条
// =============================================================================
void UpdatePetWindow() {
    StateResource& res = g_stateRes[g_currentState];
    if (!res.hdc || !res.pBits) return;

    // 创建临时帧缓冲
    HDC hdcFrame = CreateCompatibleDC(NULL);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = DISPLAY_SIZE;
    bmi.bmiHeader.biHeight      = -DISPLAY_SIZE;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* frameBits = NULL;
    HBITMAP hBmpFrame = CreateDIBSection(hdcFrame, &bmi, DIB_RGB_COLORS, &frameBits, NULL, 0);
    HGDIOBJ hOldFrame = SelectObject(hdcFrame, hBmpFrame);

    // 复制源像素
    memcpy(frameBits, res.pBits, DISPLAY_SIZE * DISPLAY_SIZE * 4);

    // ---- 绘制四维状态条 ----
    {
        Graphics g(hdcFrame);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

        const int barW = 44, barH = 5, gap = 3;
        int bx = DISPLAY_SIZE - barW - 4;
        int by = DISPLAY_SIZE - 36;

        // 半透明背景
        SolidBrush bgBrush(Color(110, 0, 0, 0));
        g.FillRectangle(&bgBrush, bx - 2, by - 2, barW + 4, (barH + gap) * 4 + 4);

        // 饥饿条（橙色）— 标签 "H"
        {
            SolidBrush barBg(Color(80, 60, 60, 60));
            g.FillRectangle(&barBg, bx, by, barW, barH);
            int fillW = (barW * g_hunger) / MAX_HUNGER;
            SolidBrush brush(Color(220, 240, 160, 50));
            g.FillRectangle(&brush, bx, by, fillW, barH);
        }

        // 清洁条（蓝色）
        {
            int yy = by + barH + gap;
            SolidBrush barBg(Color(80, 60, 60, 60));
            g.FillRectangle(&barBg, bx, yy, barW, barH);
            int fillW = (barW * g_clean) / MAX_CLEAN;
            SolidBrush brush(Color(220, 80, 160, 240));
            g.FillRectangle(&brush, bx, yy, fillW, barH);
        }

        // 心情条（粉色）
        {
            int yy = by + (barH + gap) * 2;
            SolidBrush barBg(Color(80, 60, 60, 60));
            g.FillRectangle(&barBg, bx, yy, barW, barH);
            int fillW = (barW * g_mood) / MAX_MOOD;
            SolidBrush brush(Color(220, 240, 100, 180));
            g.FillRectangle(&brush, bx, yy, fillW, barH);
        }

        // 健康条（绿色）
        {
            int yy = by + (barH + gap) * 3;
            SolidBrush barBg(Color(80, 60, 60, 60));
            g.FillRectangle(&barBg, bx, yy, barW, barH);
            int fillW = (barW * g_health) / MAX_HEALTH;
            // 健康值低时变红
            Color hc = (g_health >= 4) ? Color(220, 80, 220, 80)
                                      : Color(220, 220, 60, 60);
            SolidBrush brush(hc);
            g.FillRectangle(&brush, bx, yy, fillW, barH);
        }
    }

    // ---- 更新分层窗口 ----
    BLENDFUNCTION blend = {};
    blend.BlendOp             = AC_SRC_OVER;
    blend.BlendFlags          = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat         = AC_SRC_ALPHA;

    POINT ptSrc = {0, 0};
    SIZE  sizeDst = {DISPLAY_SIZE, DISPLAY_SIZE};
    RECT rect;
    GetWindowRect(g_hwnd, &rect);
    POINT ptDst = {rect.left, rect.top};

    UpdateLayeredWindow(g_hwnd, NULL, &ptDst, &sizeDst,
                        hdcFrame, &ptSrc, 0, &blend, ULW_ALPHA);

    // 清理
    SelectObject(hdcFrame, hOldFrame);
    DeleteObject(hBmpFrame);
    DeleteDC(hdcFrame);
}

// =============================================================================
//  四维属性检测 → 自动切换状态
//  优先级：生病 > 饥饿/脏污 > 心情
// =============================================================================
void CheckAutoSwitch() {
    // 吃东西/洗澡/睡觉时不自动切换
    if (g_currentState == STATE_EATING ||
        g_currentState == STATE_BATHING ||
        g_currentState == STATE_SLEEPING) return;

    // 生病优先
    if (g_isSick || g_health < MAX_HEALTH) {
        if (g_currentState != STATE_SICK) {
            g_currentState = STATE_SICK;
            UpdatePetWindow();
        }
        return;
    }

    // 饥饿或脏污 → 生气
    if (g_hunger < HUNGER_THRESHOLD || g_clean < CLEAN_THRESHOLD) {
        if (g_currentState != STATE_ANGRY) {
            g_currentState = STATE_ANGRY;
            UpdatePetWindow();
        }
        return;
    }

    // 心情低落 → 生气，否则开心
    PetState target = (g_mood > MOOD_THRESHOLD * 3) ? STATE_IDLE : STATE_ANGRY;
    if (target != g_currentState) {
        g_currentState = target;
        UpdatePetWindow();
    }
}

// =============================================================================
//  状态切换（用户手动）
// =============================================================================
void SwitchToState(PetState newState) {
    if (newState < 0 || newState >= STATE_COUNT) return;
    g_currentState = newState;

    // 玩耍增加心情
    if (newState == STATE_PLAYING) {
        g_mood = (g_mood + PLAY_AMOUNT > MAX_MOOD) ? MAX_MOOD : (g_mood + PLAY_AMOUNT);
    }

    UpdatePetWindow();
}

// =============================================================================
//  养护动作
// =============================================================================
void DoFeed() {
    g_hunger = (g_hunger + FEED_AMOUNT > MAX_HUNGER) ? MAX_HUNGER : (g_hunger + FEED_AMOUNT);
    g_mood = (g_mood + 50 > MAX_MOOD) ? MAX_MOOD : (g_mood + 50);
    g_currentState = STATE_EATING;
    UpdatePetWindow();
}

void DoBath() {
    g_clean = (g_clean + BATH_AMOUNT > MAX_CLEAN) ? MAX_CLEAN : (g_clean + BATH_AMOUNT);
    g_mood = (g_mood + 50 > MAX_MOOD) ? MAX_MOOD : (g_mood + 50);
    g_currentState = STATE_BATHING;
    UpdatePetWindow();
}

void DoPlay() {
    g_mood = (g_mood + PLAY_AMOUNT > MAX_MOOD) ? MAX_MOOD : (g_mood + PLAY_AMOUNT);
    g_currentState = STATE_PLAYING;
    UpdatePetWindow();
}

// =============================================================================
//  皮肤切换
// =============================================================================
bool SwitchToSkin(int idx) {
    if (idx < 0 || idx >= (int)g_skins.size() || idx == g_currentSkinIdx) return false;
    if (!LoadSkin(idx)) return false;
    UpdatePetWindow();
    return true;
}

// =============================================================================
//  右键菜单
// =============================================================================
void ShowContextMenu(HWND hwnd, int x, int y) {
    HMENU hMenu = CreatePopupMenu();

    // === 四维状态概览 ===
    {
        // 计算百分比
        int hungerPct = (g_hunger * 100) / MAX_HUNGER;
        int cleanPct  = (g_clean * 100) / MAX_CLEAN;
        int moodPct   = (g_mood * 100) / MAX_MOOD;
        wchar_t statusText[128];
        wsprintfW(statusText,
            L"\u2764 \u9965\u997F:%d%%  \u6E05\u6D01:%d%%  \u5FC3\u60C5:%d%%  \u5065\u5EB7:%d/5",
            hungerPct, cleanPct, moodPct, g_health);
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, statusText);
        if (g_isSick) {
            wchar_t sickText[64];
            wsprintfW(sickText, L"  \u26A0 \u751F\u75C5: %s", g_sickName.c_str());
            AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, sickText);
        }
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    }

    // === 养护动作 ===
    AppendMenuW(hMenu, MF_STRING, MENU_ACTION_BASE + 0,
        L"\U0001f35c \u5582\u98DF (+1000\u9965\u997F)");  // 🍜 喂食
    AppendMenuW(hMenu, MF_STRING, MENU_ACTION_BASE + 1,
        L"\U0001f6bf \u6D17\u6FA1 (+1000\u6E05\u6D01)");  // 🚿 洗澡
    AppendMenuW(hMenu, MF_STRING, MENU_ACTION_BASE + 2,
        L"\u26BD \u73A9\u800D (+100\u5FC3\u60C5)");        // ⚽ 玩耍
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    // === 状态切换 ===
    AppendMenuW(hMenu, MF_STRING, MENU_STATE_BASE + STATE_IDLE,
        L"\U0001f60a \u5F00\u5FC3");    // 😊 开心
    AppendMenuW(hMenu, MF_STRING, MENU_STATE_BASE + STATE_PLAYING,
        L"\U0001f3ae \u73A9\u800D");    // 🎮 玩耍
    AppendMenuW(hMenu, MF_STRING, MENU_STATE_BASE + STATE_SLEEPING,
        L"\U0001f4a4 \u7761\u89C9");    // 💤 睡觉
    AppendMenuW(hMenu, MF_STRING, MENU_STATE_BASE + STATE_ANGRY,
        L"\U0001f620 \u751F\u6C14");    // 😠 生气
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    // === 皮肤选择 ===
    if (!g_skins.empty()) {
        HMENU hSkinMenu = CreatePopupMenu();
        for (size_t i = 0; i < g_skins.size(); i++) {
            UINT flags = MF_STRING;
            if ((int)i == g_currentSkinIdx) flags |= MF_CHECKED;
            AppendMenuW(hSkinMenu, flags, MENU_SKIN_BASE + (UINT)i,
                        g_skins[i].name.c_str());
        }
        AppendMenuW(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hSkinMenu,
                    L"\U0001f3ae \u76AE\u80A4");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    }

    // === 置顶 ===
    if (g_alwaysOnTop)
        AppendMenuW(hMenu, MF_STRING | MF_CHECKED, ID_TOPMOST, L"\U0001f4cc \u59CB\u7EC8\u7F6E\u9876");
    else
        AppendMenuW(hMenu, MF_STRING, ID_TOPMOST, L"\U0001f4cc \u59CB\u7EC8\u7F6E\u9876");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_QUIT, L"\U0001f6aa \u9000\u51FA");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, x, y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

// =============================================================================
//  窗口过程
// =============================================================================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE: {
        srand((unsigned)time(NULL));
        g_hwnd = hwnd;
        g_exeDir = GetExeDir();

        // 扫描皮肤
        if (!ScanSkins()) {
            MessageBoxW(hwnd,
                L"\u672A\u627E\u5230\u4EFB\u4F55\u76AE\u80A4\uFF01\n\u8BF7\u5728 animal/ \u76EE\u5F55\u4E0B\u521B\u5EFA\u76AE\u80A4\u5B50\u76EE\u5F55\u5E76\u653E\u5165\u7D20\u6750\u3002",
                L"\u684C\u5BA0\u9519\u8BEF", MB_ICONERROR);
            PostQuitMessage(1);
            return -1;
        }

        // 加载第一个皮肤
        if (!LoadSkin(0)) {
            MessageBoxW(hwnd,
                L"\u65E0\u6CD5\u52A0\u8F7D\u9ED8\u8BA4\u76AE\u80A4\uFF01",
                L"\u684C\u5BA0\u9519\u8BEF", MB_ICONERROR);
            PostQuitMessage(1);
            return -1;
        }

        // 初始化四维属性
        g_hunger  = MAX_HUNGER;
        g_clean   = MAX_CLEAN;
        g_mood    = MAX_MOOD;
        g_health  = MAX_HEALTH;
        g_statusTick = 0;

        // 启动状态刷新定时器
        SetTimer(hwnd, TIMER_STATUS, STATUS_TICK_MS, NULL);

        // 初始状态
        SwitchToState(STATE_IDLE);

        // 屏幕右下角
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hwnd, NULL,
                     screenW - DISPLAY_SIZE - 50,
                     screenH - DISPLAY_SIZE - 100,
                     DISPLAY_SIZE, DISPLAY_SIZE,
                     SWP_NOZORDER | SWP_NOACTIVATE);

        UpdatePetWindow();
        return 0;
    }

    case WM_NCHITTEST: {
        // 穿透点击：检查鼠标位置下对应像素的 alpha 值
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        int x = pt.x;
        int y = pt.y;
        if (x >= 0 && x < DISPLAY_SIZE && y >= 0 && y < DISPLAY_SIZE) {
            StateResource& res = g_stateRes[g_currentState];
            if (res.pBits) {
                // 32-bit ARGB, 每像素4字节
                DWORD* pixels = (DWORD*)res.pBits;
                DWORD pixel = pixels[y * DISPLAY_SIZE + x];
                BYTE alpha = (pixel >> 24) & 0xFF;
                // alpha < 40 视为透明，穿透点击到下层窗口
                if (alpha < 40) {
                    return HTTRANSPARENT;
                }
            }
        }
        return HTCLIENT;
    }

    case WM_LBUTTONDOWN: {
        g_isDragging = true;
        SetCapture(hwnd);
        g_dragOffset.x = LOWORD(lParam);
        g_dragOffset.y = HIWORD(lParam);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (g_isDragging) {
            RECT rect;
            GetWindowRect(hwnd, &rect);
            int newX = LOWORD(lParam) + rect.left - g_dragOffset.x;
            int newY = HIWORD(lParam) + rect.top  - g_dragOffset.y;
            SetWindowPos(hwnd, NULL, newX, newY,
                         0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_isDragging) {
            g_isDragging = false;
            ReleaseCapture();

            POINT pt = {LOWORD(lParam), HIWORD(lParam)};
            int dx = abs(pt.x - g_dragOffset.x);
            int dy = abs(pt.y - g_dragOffset.y);
            if (dx < 5 && dy < 5) {
                // 点击 → 增加心情 + 切换下一个非活动状态
                g_mood = (g_mood + 20 > MAX_MOOD) ? MAX_MOOD : (g_mood + 20);
                PetState next = (PetState)((g_currentState + 1) % STATE_COUNT);
                SwitchToState(next);
            }
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        POINT pt = {LOWORD(lParam), HIWORD(lParam)};
        ClientToScreen(hwnd, &pt);
        ShowContextMenu(hwnd, pt.x, pt.y);
        return 0;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);

        // 养护动作
        if (id == MENU_ACTION_BASE + 0) { DoFeed(); return 0; }
        if (id == MENU_ACTION_BASE + 1) { DoBath(); return 0; }
        if (id == MENU_ACTION_BASE + 2) { DoPlay(); return 0; }

        // 皮肤切换
        if (id >= MENU_SKIN_BASE && id < MENU_SKIN_BASE + (UINT)g_skins.size()) {
            SwitchToSkin(id - MENU_SKIN_BASE);
            return 0;
        }

        // 状态切换
        if (id >= MENU_STATE_BASE && id < MENU_STATE_BASE + STATE_COUNT) {
            SwitchToState((PetState)(id - MENU_STATE_BASE));
            return 0;
        }

        switch (id) {
        case ID_TOPMOST:
            g_alwaysOnTop = !g_alwaysOnTop;
            SetWindowPos(hwnd,
                         g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            break;
        case ID_QUIT:
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    }

    case WM_TIMER: {
        if (wParam == TIMER_STATUS) {
            g_statusTick++;

            // 饥饿下降：12 tick = 60s, ↓5~8（随机）
            if (g_statusTick % HUNGER_DEC_TICK == 0) {
                int dec = 5 + (rand() % 4);  // 5~7
                if (g_mood < 300) dec += 2;   // 心情低额外 -2（参考 QQ 宠物）
                g_hunger = (g_hunger - dec < 0) ? 0 : (g_hunger - dec);
            }

            // 清洁下降：12 tick = 60s, ↓5~8
            if (g_statusTick % CLEAN_DEC_TICK == 0) {
                int dec = 5 + (rand() % 4);
                if (g_mood < 300) dec += 2;
                g_clean = (g_clean - dec < 0) ? 0 : (g_clean - dec);
            }

            // 心情下降：6 tick = 30s, ↓2~4
            if (g_statusTick % MOOD_DEC_TICK == 0) {
                int dec = 2 + (rand() % 3);  // 2~4
                g_mood = (g_mood - dec < 0) ? 0 : (g_mood - dec);
            }

            // 生病时健康持续下降（简化版：不治疗自动恶化）
            if (g_isSick && g_statusTick % 60 == 0) {  // 每 300s
                g_health = (g_health - 1 < 0) ? 0 : (g_health - 1);
            }

            // 随机生病（简化版：饥饿+脏污同时低于阈值有概率生病）
            if (!g_isSick && g_health > 0 &&
                g_hunger < 300 && g_clean < 300 &&
                g_statusTick % 120 == 0 && (rand() % 10 < 3)) {
                g_health = (g_health > 1) ? (g_health - 1) : 1;
                g_isSick = true;
                g_sickName = L"\u611F\u5192";  // 感冒
            }

            // 自动状态切换
            CheckAutoSwitch();
            UpdatePetWindow();
            return 0;
        }
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(hwnd, TIMER_STATUS);
        FreeAllResources();
        PostQuitMessage(0);
        return 0;
    }

    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// =============================================================================
//  程序入口
// =============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    PWSTR pCmdLine, int nCmdShow) {
    GdiplusStartupInput gdiplusStartupInput;
    ZeroMemory(&gdiplusStartupInput, sizeof(gdiplusStartupInput));
    gdiplusStartupInput.GdiplusVersion = 1;
    ULONG_PTR gdiplusToken;
    if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) != Ok) {
        MessageBoxW(NULL, L"GDI+ init failed", L"Error", MB_ICONERROR);
        return 1;
    }

    g_hInst = hInstance;

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"DesktopPetWindow";
    wc.style         = CS_DBLCLKS;

    if (!RegisterClassW(&wc)) {
        GdiplusShutdown(gdiplusToken);
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"DesktopPetWindow",
        L"Desktop Pet",
        WS_POPUP,
        0, 0, DISPLAY_SIZE, DISPLAY_SIZE,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) {
        GdiplusShutdown(gdiplusToken);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}
