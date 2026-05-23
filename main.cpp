// =============================================================================
//  桌面桌宠 - Desktop Pet
//  Win32 API + GDI+，支持多皮肤 + 食物合成（基础姿势 + 食物素材叠加）
//  兼容旧格式（吃XX.png）和新格式（吃东西.png + food/ 子目录）
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
#include <ctime>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")

using namespace Gdiplus;

// =============================================================================
//  常量
// =============================================================================
constexpr int DISPLAY_SIZE    = 200;
constexpr int STATE_INTERVAL  = 4000;   // 普通状态自动切换间隔 (ms)
constexpr int EATING_INTERVAL = 500;    // 吃东西食物切换间隔 (ms)
constexpr int TIMER_STATE     = 1;
constexpr int TIMER_EATING    = 2;

constexpr int MENU_SKIN_BASE  = 2000;   // 皮肤切换菜单起始 ID
constexpr int MENU_STATE_BASE = 1000;   // 状态切换菜单起始 ID
constexpr int MENU_FOOD_BASE  = 3000;   // 食物投喂菜单起始 ID

// 食物合成默认位置 (占 DISPLAY_SIZE 的百分比)
constexpr float DEFAULT_FOOD_POS_X = 0.55f;
constexpr float DEFAULT_FOOD_POS_Y = 0.50f;
constexpr float DEFAULT_FOOD_SIZE  = 0.28f;

// =============================================================================
//  状态枚举
// =============================================================================
enum PetState {
    STATE_IDLE    = 0,   // 开心
    STATE_PLAYING = 1,   // 玩耍
    STATE_DRAWING = 2,   // 画画
    STATE_WRITING = 3,   // 写日记
    STATE_ANGRY   = 4,   // 生气
    STATE_EATING  = 5,   // 吃东西
    STATE_COUNT   = 6
};

// 5 种基础状态素材文件名
const wchar_t* STATE_FILENAMES[] = {
    L"\u5F00\u5FC3.png",        // 开心
    L"\u73A9\u800D.png",        // 玩耍
    L"\u753B\u753B.png",        // 画画
    L"\u5199\u65E5\u8BB0.png",  // 写日记
    L"\u751F\u6C14.png",        // 生气
};

// 新格式: 吃东西基础姿势
const wchar_t* EATING_BASE_FILENAME = L"\u5403\u4E1C\u897F.png"; // 吃东西.png

// 新格式: 食物子目录名
const wchar_t* FOOD_DIR_NAME = L"food";

// 新格式: 食物位置配置文件
const wchar_t* FOOD_CONFIG_FILE = L"food_offset.txt";

// 旧格式: 吃东西素材文件名
const wchar_t* EATING_FILENAMES_OLD[] = {
    L"\u5403\u6A59\u5B50.png",  // 吃橙子
    L"\u5403\u68A8.png",        // 吃梨
    L"\u5403\u82F9\u679C.png",  // 吃苹果
    L"\u5403\u9999\u8549.png",  // 吃香蕉
    L"\u5403\u6A31\u6843.png",  // 吃樱桃
};
const wchar_t* EATING_FOOD_NAMES_OLD[] = {
    L"\u6A59\u5B50",  // 橙子
    L"\u68A8",        // 梨
    L"\u82F9\u679C",  // 苹果
    L"\u9999\u8549",  // 香蕉
    L"\u6A31\u6843",  // 樱桃
};
constexpr int EATING_OLD_COUNT = 5;

// =============================================================================
//  皮肤结构
// =============================================================================
struct SkinInfo {
    std::wstring name;   // 皮肤名称
    std::wstring path;   // 完整路径
};

// =============================================================================
//  资源结构
// =============================================================================
struct StateResource {
    HDC     hdc = NULL;
    HBITMAP hBmp = NULL;
    HBITMAP hOldBmp = NULL;
};

// =============================================================================
//  全局变量
// =============================================================================
HINSTANCE       g_hInst = NULL;
HWND            g_hwnd  = NULL;
std::vector<SkinInfo> g_skins;
int             g_currentSkinIdx = 0;

StateResource   g_stateRes[STATE_COUNT];      // 5 种基础状态
std::vector<StateResource> g_eatingRes;       // 吃东西帧 (新旧格式统一)
std::vector<std::wstring>  g_foodNames;       // 食物名称列表
bool            g_useCompositeEating = false;  // 是否使用合成模式

PetState        g_currentState = STATE_IDLE;
int             g_eatingIndex  = 0;
bool            g_isDragging   = false;
POINT           g_dragOffset   = {0, 0};
bool            g_alwaysOnTop  = true;
std::wstring    g_exeDir;

// 食物合成位置 (每皮肤可不同)
float g_foodPosX = DEFAULT_FOOD_POS_X;
float g_foodPosY = DEFAULT_FOOD_POS_Y;
float g_foodSize = DEFAULT_FOOD_SIZE;

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

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
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
}

void FreeAllResources() {
    for (int i = 0; i < STATE_COUNT; i++) FreeStateResource(g_stateRes[i]);
    for (size_t i = 0; i < g_eatingRes.size(); i++) FreeStateResource(g_eatingRes[i]);
    g_eatingRes.clear();
    g_foodNames.clear();
}

// =============================================================================
//  加载单张图片到 StateResource (缩放到 DISPLAY_SIZE x DISPLAY_SIZE)
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

    Graphics g(res.hdc);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.DrawImage(pImage, 0, 0, DISPLAY_SIZE, DISPLAY_SIZE);

    delete pImage;
    return true;
}

// =============================================================================
//  合成: 基础吃东西姿势 + 食物素材 → 预渲染到 StateResource
// =============================================================================
bool CompositeEatingFrame(Image* baseImg, Image* foodImg, StateResource& res) {
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

    // 1) 画基础吃东西姿势 (全幅)
    Graphics g(res.hdc);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.DrawImage(baseImg, 0, 0, DISPLAY_SIZE, DISPLAY_SIZE);

    // 2) 叠加食物素材到指定位置 (居中放置)
    int fsize = (int)(DISPLAY_SIZE * g_foodSize);
    int fx = (int)(DISPLAY_SIZE * g_foodPosX) - fsize / 2;
    int fy = (int)(DISPLAY_SIZE * g_foodPosY) - fsize / 2;
    g.DrawImage(foodImg, fx, fy, fsize, fsize);

    return true;
}

// =============================================================================
//  加载食物位置配置 (food_offset.txt)
//  格式: x,y,size (百分比 0-100)
//  例: 55,50,28 表示食物中心在 55%×50% 处，大小为 28%
// =============================================================================
void LoadFoodConfig(const std::wstring& skinPath) {
    g_foodPosX = DEFAULT_FOOD_POS_X;
    g_foodPosY = DEFAULT_FOOD_POS_Y;
    g_foodSize = DEFAULT_FOOD_SIZE;

    std::wstring cfgPath = skinPath + L"\\" + FOOD_CONFIG_FILE;
    FILE* fp = _wfopen(cfgPath.c_str(), L"r");
    if (!fp) return;

    float x = 0, y = 0, s = 0;
    if (fwscanf(fp, L"%f,%f,%f", &x, &y, &s) == 3) {
        if (x > 0 && x < 100 && y > 0 && y < 100 && s > 0 && s < 100) {
            g_foodPosX = x / 100.0f;
            g_foodPosY = y / 100.0f;
            g_foodSize = s / 100.0f;
        }
    }
    fclose(fp);
}

// =============================================================================
//  扫描可用皮肤
// =============================================================================
bool ScanSkins() {
    g_skins.clear();
    std::wstring animalDir = g_exeDir + L"\\animal";

    WIN32_FIND_DATAW fd;
    std::wstring searchPath = animalDir + L"\\*";
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);

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
//  加载指定皮肤的全部素材
//  自动检测新格式 (吃东西.png + food/) vs 旧格式 (吃XX.png)
// =============================================================================
bool LoadSkin(int idx) {
    if (idx < 0 || idx >= (int)g_skins.size()) return false;

    FreeAllResources();
    const SkinInfo& skin = g_skins[idx];

    // ---- 加载 5 种基础状态 ----
    for (int i = 0; i < STATE_COUNT; i++) {
        std::wstring fp = skin.path + L"\\" + STATE_FILENAMES[i];
        if (!LoadImageToResource(fp, g_stateRes[i])) {
            wchar_t msg[512];
            wsprintfW(msg, L"加载皮肤素材失败!\n皮肤: %s\n文件: %s",
                      skin.name.c_str(), fp.c_str());
            MessageBoxW(g_hwnd, msg, L"桌宠错误", MB_ICONERROR);
            FreeAllResources();
            return false;
        }
    }

    // ---- 检测吃东西格式 ----
    std::wstring eatingBasePath = skin.path + L"\\" + EATING_BASE_FILENAME;

    if (FileExists(eatingBasePath)) {
        // ===== 新格式: 合成模式 =====
        g_useCompositeEating = true;
        LoadFoodConfig(skin.path);

        // 加载基础吃东西姿势
        Image* baseImg = Image::FromFile(eatingBasePath.c_str());
        if (!baseImg || baseImg->GetLastStatus() != Ok) {
            delete baseImg;
            MessageBoxW(g_hwnd, L"加载吃东西基础图失败!", L"桌宠错误", MB_ICONERROR);
            FreeAllResources();
            return false;
        }

        // 扫描 food/ 子目录中的 PNG 文件
        std::wstring foodDir = skin.path + L"\\" + FOOD_DIR_NAME;
        WIN32_FIND_DATAW fd;
        std::wstring search = foodDir + L"\\*.png";
        HANDLE hFind = FindFirstFileW(search.c_str(), &fd);

        if (hFind == INVALID_HANDLE_VALUE) {
            // food/ 目录不存在或为空 → 只显示基础吃东西姿势
            delete baseImg;
            StateResource res;
            LoadImageToResource(eatingBasePath, res);
            g_eatingRes.push_back(res);
            g_foodNames.push_back(L"");
            g_currentSkinIdx = idx;
            return true;
        }

        // 收集食物文件 (名称, 完整路径)
        std::vector<std::pair<std::wstring, std::wstring>> foodFiles;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring filename = fd.cFileName;
                std::wstring name = filename;
                // 去掉 .png / .PNG 后缀
                size_t dot = name.rfind(L".png");
                if (dot == std::wstring::npos) dot = name.rfind(L".PNG");
                if (dot != std::wstring::npos) name = name.substr(0, dot);
                foodFiles.push_back({name, foodDir + L"\\" + filename});
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);

        // 按名称排序确保每次加载顺序一致
        std::sort(foodFiles.begin(), foodFiles.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        // 预合成每一帧: 基础姿势 + 食物
        for (size_t i = 0; i < foodFiles.size(); i++) {
            Image* foodImg = Image::FromFile(foodFiles[i].second.c_str());
            if (!foodImg || foodImg->GetLastStatus() != Ok) {
                delete foodImg;
                continue;
            }
            StateResource res;
            if (CompositeEatingFrame(baseImg, foodImg, res)) {
                g_eatingRes.push_back(res);
                g_foodNames.push_back(foodFiles[i].first);
            }
            delete foodImg;
        }

        delete baseImg;

        // 如果所有食物都加载失败，至少保留基础姿势
        if (g_eatingRes.empty()) {
            StateResource res;
            LoadImageToResource(eatingBasePath, res);
            g_eatingRes.push_back(res);
            g_foodNames.push_back(L"");
        }
    } else {
        // ===== 旧格式: 直接加载吃XX.png =====
        g_useCompositeEating = false;

        for (int i = 0; i < EATING_OLD_COUNT; i++) {
            std::wstring fp = skin.path + L"\\" + EATING_FILENAMES_OLD[i];
            StateResource res;
            if (!LoadImageToResource(fp, res)) {
                wchar_t msg[512];
                wsprintfW(msg, L"加载皮肤素材失败!\n皮肤: %s\n文件: %s",
                          skin.name.c_str(), fp.c_str());
                MessageBoxW(g_hwnd, msg, L"桌宠错误", MB_ICONERROR);
                FreeAllResources();
                return false;
            }
            g_eatingRes.push_back(res);
            g_foodNames.push_back(EATING_FOOD_NAMES_OLD[i]);
        }
    }

    g_currentSkinIdx = idx;
    return true;
}

// =============================================================================
//  更新窗口图片
// =============================================================================
void UpdatePetWindow() {
    HDC hdcSrc = NULL;

    if (g_currentState == STATE_EATING) {
        if (g_eatingIndex >= 0 && g_eatingIndex < (int)g_eatingRes.size())
            hdcSrc = g_eatingRes[g_eatingIndex].hdc;
    } else {
        hdcSrc = g_stateRes[g_currentState].hdc;
    }

    if (!hdcSrc) return;

    BLENDFUNCTION blend = {};
    blend.BlendOp             = AC_SRC_OVER;
    blend.BlendFlags          = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat         = AC_SRC_ALPHA;

    POINT ptSrc   = {0, 0};
    SIZE  sizeDst = {DISPLAY_SIZE, DISPLAY_SIZE};

    RECT rect;
    GetWindowRect(g_hwnd, &rect);
    POINT ptDst = {rect.left, rect.top};

    UpdateLayeredWindow(g_hwnd, NULL, &ptDst, &sizeDst,
                        hdcSrc, &ptSrc, 0, &blend, ULW_ALPHA);
}

// =============================================================================
//  状态切换
// =============================================================================
void SwitchToState(PetState newState) {
    g_currentState = newState;
    g_eatingIndex  = 0;

    KillTimer(g_hwnd, TIMER_EATING);
    KillTimer(g_hwnd, TIMER_STATE);

    if (newState == STATE_EATING) {
        SetTimer(g_hwnd, TIMER_EATING, EATING_INTERVAL, NULL);
    } else {
        SetTimer(g_hwnd, TIMER_STATE, STATE_INTERVAL, NULL);
    }

    UpdatePetWindow();
}

void SwitchToRandomState(bool includeEating = true) {
    int maxState = includeEating ? STATE_COUNT : (STATE_COUNT - 1);
    int r = rand() % maxState;
    SwitchToState((PetState)r);
}

// 投喂指定食物
void FeedFood(int foodIndex) {
    if (foodIndex < 0 || foodIndex >= (int)g_eatingRes.size()) return;
    g_currentState = STATE_EATING;
    g_eatingIndex = foodIndex;

    KillTimer(g_hwnd, TIMER_STATE);
    SetTimer(g_hwnd, TIMER_EATING, EATING_INTERVAL, NULL);
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

    // === 皮肤选择 ===
    if (!g_skins.empty()) {
        for (size_t i = 0; i < g_skins.size(); i++) {
            UINT flags = MF_STRING;
            if ((int)i == g_currentSkinIdx)
                flags |= MF_CHECKED;
            AppendMenuW(hMenu, flags, MENU_SKIN_BASE + (UINT)i,
                        (L"\U0001f3ae 皮肤: " + g_skins[i].name).c_str());
        }
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    }

    // === 投喂食物 (子菜单) ===
    if (!g_foodNames.empty()) {
        HMENU hFoodMenu = CreatePopupMenu();
        for (size_t i = 0; i < g_foodNames.size(); i++) {
            if (g_foodNames[i].empty()) continue;
            std::wstring label = L"\U0001f356 " + g_foodNames[i];  // 🍖 食物名
            // 如果当前正在吃这个食物，标记
            UINT flags = MF_STRING;
            if (g_currentState == STATE_EATING && g_eatingIndex == (int)i)
                flags |= MF_CHECKED;
            AppendMenuW(hFoodMenu, flags, MENU_FOOD_BASE + (UINT)i, label.c_str());
        }
        AppendMenuW(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hFoodMenu,
                    L"\U0001f37d 投喂食物");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    }

    // === 状态切换 ===
    AppendMenuW(hMenu, MF_STRING, MENU_STATE_BASE + STATE_IDLE,    L"\U0001f60a 开心");
    AppendMenuW(hMenu, MF_STRING, MENU_STATE_BASE + STATE_PLAYING, L"\U0001f3ae 玩耍");
    AppendMenuW(hMenu, MF_STRING, MENU_STATE_BASE + STATE_DRAWING, L"\U0001f3a8 画画");
    AppendMenuW(hMenu, MF_STRING, MENU_STATE_BASE + STATE_WRITING, L"\U0001f4dd 写日记");
    AppendMenuW(hMenu, MF_STRING, MENU_STATE_BASE + STATE_ANGRY,   L"\U0001f620 生气");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    // === 置顶 ===
    if (g_alwaysOnTop)
        AppendMenuW(hMenu, MF_STRING | MF_CHECKED, 1007, L"\U0001f4cc 始终置顶");
    else
        AppendMenuW(hMenu, MF_STRING, 1007, L"\U0001f4cc 始终置顶");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 1008, L"\U0001f6aa 退出");

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

        // 扫描可用皮肤
        if (!ScanSkins()) {
            MessageBoxW(hwnd,
                L"未找到任何皮肤！\n请在 animal/ 目录下创建皮肤子目录并放入素材。",
                L"桌宠错误", MB_ICONERROR);
            PostQuitMessage(1);
            return -1;
        }

        // 加载第一个皮肤
        if (!LoadSkin(0)) {
            MessageBoxW(hwnd,
                L"无法加载默认皮肤！\n请检查 animal/ 目录下的皮肤素材是否完整。",
                L"桌宠错误", MB_ICONERROR);
            PostQuitMessage(1);
            return -1;
        }

        // 初始状态: 开心
        SwitchToState(STATE_IDLE);

        // 设置窗口位置: 屏幕右下角
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
                SwitchToRandomState(true);
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

        // 食物投喂 (MENU_FOOD_BASE ~ MENU_FOOD_BASE + g_foodNames.size() - 1)
        if (id >= MENU_FOOD_BASE && id < MENU_FOOD_BASE + (UINT)g_foodNames.size()) {
            int foodIdx = id - MENU_FOOD_BASE;
            FeedFood(foodIdx);
            return 0;
        }

        // 皮肤切换
        if (id >= MENU_SKIN_BASE && id < MENU_SKIN_BASE + (UINT)g_skins.size()) {
            int skinIdx = id - MENU_SKIN_BASE;
            SwitchToSkin(skinIdx);
            return 0;
        }

        // 状态切换
        if (id >= MENU_STATE_BASE && id < MENU_STATE_BASE + STATE_COUNT) {
            int state = id - MENU_STATE_BASE;
            SwitchToState((PetState)state);
            return 0;
        }

        switch (id) {
        case 1007:  // 切换置顶
            g_alwaysOnTop = !g_alwaysOnTop;
            SetWindowPos(hwnd,
                         g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            break;
        case 1008:  // 退出
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    }

    case WM_TIMER: {
        if (wParam == TIMER_EATING && g_currentState == STATE_EATING) {
            // 循环切换食物
            g_eatingIndex = (g_eatingIndex + 1) % (int)g_eatingRes.size();
            UpdatePetWindow();
        } else if (wParam == TIMER_STATE) {
            SwitchToRandomState(true);
        }
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(hwnd, TIMER_STATE);
        KillTimer(hwnd, TIMER_EATING);
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
    // 初始化 GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) != Ok) {
        MessageBoxW(NULL, L"GDI+ init failed", L"Error", MB_ICONERROR);
        return 1;
    }

    g_hInst = hInstance;

    // 注册窗口类
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

    // 创建透明分层窗口
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
