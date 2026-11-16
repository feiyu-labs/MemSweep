// MemSweep · 内存速清 — 现代自绘 UI（GDI+）
#include "ui.h"
#include "core.h"

#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

using namespace Gdiplus;

#ifndef NIIF_RESPECT_NO_NOTIFY
#define NIIF_RESPECT_NO_NOTIFY 0x00000080
#endif

namespace ms {

// ---------------------------------------------------------------------------
// 主题
// ---------------------------------------------------------------------------
struct Theme {
    Color bg1, bg2;
    Color card, cardTop, border;
    Color text, text2, text3;
    Color accent, accent2, warn, danger;
    Color track, trackFill, trackOff;
    Color btnText;
};
static Theme T;

static Color C(BYTE a, BYTE r, BYTE g, BYTE b) { return Color(a, r, g, b); }

static void BuildTheme(bool dark) {
    if (dark) {
        T.bg1      = C(255, 13, 15, 20);
        T.bg2      = C(255, 17, 20, 27);
        T.card     = C(255, 24, 28, 36);
        T.cardTop  = C(255, 31, 36, 46);
        T.border   = C(255, 44, 50, 62);
        T.text     = C(255, 235, 238, 243);
        T.text2    = C(255, 158, 166, 178);
        T.text3    = C(255, 108, 116, 128);
        T.accent   = C(255, 52, 211, 153);
        T.accent2  = C(255, 16, 185, 129);
        T.warn     = C(255, 245, 158, 11);
        T.danger   = C(255, 239, 68, 68);
        T.track    = C(255, 45, 51, 62);
        T.trackOff = C(255, 58, 66, 80);
        T.trackFill= C(255, 39, 45, 56);
        T.btnText  = C(255, 8, 24, 18);
    } else {
        T.bg1      = C(255, 246, 248, 251);
        T.bg2      = C(255, 240, 243, 248);
        T.card     = C(255, 255, 255, 255);
        T.cardTop  = C(255, 252, 253, 255);
        T.border   = C(255, 226, 231, 238);
        T.text     = C(255, 17, 24, 39);
        T.text2    = C(255, 100, 112, 128);
        T.text3    = C(255, 148, 158, 172);
        T.accent   = C(255, 16, 185, 129);
        T.accent2  = C(255, 5, 150, 105);
        T.warn     = C(255, 217, 119, 6);
        T.danger   = C(255, 220, 38, 38);
        T.track    = C(255, 230, 235, 242);
        T.trackOff = C(255, 203, 213, 225);
        T.trackFill= C(255, 241, 245, 249);
        T.btnText  = C(255, 255, 255, 255);
    }
}

// ---------------------------------------------------------------------------
// 字体
// ---------------------------------------------------------------------------
static Font *f11 = nullptr, *f12 = nullptr, *f15b = nullptr, *f12b = nullptr,
            *f11b = nullptr, *f42b = nullptr, *f20b = nullptr,
            *fTrayNormal = nullptr, *fTrayClean = nullptr;
static int fTraySz = 0;
static std::wstring FontFamily() { return g_zh ? L"Microsoft YaHei UI" : L"Segoe UI"; }

static void CreateFonts() {
    auto del = [](Font*& f) { if (f) { delete f; f = nullptr; } };
    del(f11); del(f12); del(f15b); del(f12b); del(f11b);
    del(f42b); del(f20b); del(fTrayNormal); del(fTrayClean);
    fTraySz = 0;
    std::wstring fam = FontFamily();
    f11   = new Font(fam.c_str(), 12.0f, FontStyleRegular, UnitPixel);
    f12   = new Font(fam.c_str(), 13.0f, FontStyleRegular, UnitPixel);
    f15b  = new Font(fam.c_str(), 16.0f, FontStyleBold, UnitPixel);
    f12b  = new Font(fam.c_str(), 13.0f, FontStyleBold, UnitPixel);
    f11b  = new Font(fam.c_str(), 12.0f, FontStyleBold, UnitPixel);
    f42b  = new Font(fam.c_str(), 44.0f, FontStyleBold, UnitPixel);
    f20b  = new Font(fam.c_str(), 21.0f, FontStyleBold, UnitPixel);
}

// ---------------------------------------------------------------------------
// 绘图助手
// ---------------------------------------------------------------------------
static void AddRound(GraphicsPath& p, const RectF& r, float radius) {
    float d = radius * 2;
    if (d > r.Width) d = r.Width;
    if (d > r.Height) d = r.Height;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}
static void FillRound(Graphics& g, const RectF& r, float radius, const Color& c) {
    GraphicsPath p;
    AddRound(p, r, radius);
    SolidBrush b(c);
    g.FillPath(&b, &p);
}
static void StrokeRound(Graphics& g, const RectF& r, float radius, const Color& c, float w) {
    GraphicsPath p;
    AddRound(p, r, radius);
    Pen pen(c, w);
    g.DrawPath(&pen, &p);
}
static void FillRoundGrad(Graphics& g, const RectF& r, float radius, const Color& a, const Color& b) {
    GraphicsPath p;
    AddRound(p, r, radius);
    LinearGradientBrush br(r, a, b, LinearGradientModeVertical);
    g.FillPath(&br, &p);
}
static void TextAt(Graphics& g, const std::wstring& s, const RectF& r, Font* f,
                   const Color& c, StringAlignment ha = StringAlignmentNear,
                   StringAlignment va = StringAlignmentCenter, bool ellipsis = false) {
    StringFormat fmt;
    fmt.SetAlignment(ha);
    fmt.SetLineAlignment(va);
    fmt.SetTrimming(ellipsis ? StringTrimmingEllipsisCharacter : StringTrimmingNone);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush b(c);
    g.DrawString(s.c_str(), (INT)s.size(), f, r, &fmt, &b);
}

// ---------------------------------------------------------------------------
// 控件模型
// ---------------------------------------------------------------------------
enum CtlId {
    ID_NONE = 0,
    ID_BTN_CLEAN, ID_RING_CLEAN, ID_BTN_MIN, ID_BTN_CLOSE, ID_BTN_SETTINGS,
    ID_BTN_THEME, ID_BTN_LANG,
    ID_SHEET_CLOSE, ID_SHEET_DISMISS,
    ID_HOME_PROCESS, ID_HOME_SYSTEM,
    ID_TG_AUTO, ID_TG_HOTKEY, ID_TG_ECO, ID_TG_AUTOSTART, ID_TG_SILENT, ID_TG_IDLE_DEEP,
    ID_SL_THRESH,
    ID_SYS_MC, ID_SYS_PC, ID_SYS_PF, ID_SYS_WSL, ID_SYS_EXPORT, ID_SYS_MORE,
    ID_SET_ADVANCED, ID_ADV_BACK, ID_SET_ABOUT, ID_SET_PROTECTED,
    ID_ABOUT_REPO, ID_ABOUT_ISSUES, ID_ABOUT_UPDATES,
    ID_PROC_BASE = 2400,
    ID_ED_WL = 2500,
    ID_SET_COOL_BASE = 2510, ID_SET_STRENGTH_BASE = 2520,
    ID_ADV_MINWS_BASE = 2530, ID_ADV_IDLE_BASE = 2540
};
enum CtlKind { K_NONE, K_BUTTON, K_TOGGLE, K_SLIDER, K_SEG, K_EDIT, K_TITLEBTN };
enum class Sheet { None, Settings, Advanced, Processes, System, About };

struct Hit { CtlId id; CtlKind kind; RectF r; };

static std::vector<Hit> g_hits;
static int g_hover = 0;
static int g_pressed = 0;
static bool g_draggingSlider = false;
static bool g_snapshotMode = false;
static float g_sliderValue = 85.0f;
static std::unordered_map<int, float> g_anim;   // toggle -> 0..1
static float g_gaugeShown = 0.0f;
static float g_gaugeTarget = 0.0f;
static float g_spin = 0.0f;
static float g_ambientPhase = 0.0f;    // 环境高光相位 0..1
static float g_ringHalo = 0.0f;        // 悬停光晕 0..1（缓动）
static ULONGLONG g_lastAnimMs = 0;     // 动画步进时间戳（帧率无关）
static ULONGLONG g_lastAmbientMs = 0;

// 单主屏 + 一层 Sheet
static Sheet g_sheet = Sheet::None;
static bool g_sheetAnimating = false;
static bool g_procFromSettings = false;   // 进程表从设置页进入时显示返回箭头
static float g_sheetT = 1.0f;
static ULONGLONG g_sheetStart = 0;
static bool g_systemExpanded = false;
static int g_strength = 2;       // 0 温和 / 1 均衡 / 2 深度
static ULONGLONG g_savedAt = 0;
static ULONGLONG g_ringPulseAt = 0;
static std::wstring g_wlDraft;
static bool g_coolDirty = false, g_minDirty = false, g_idleDirty = false, g_strengthDirty = false;
static int g_coolSel = 2;        // 30 / 60 / 180 / 300
static int g_minWSSel = 1;       // 64 / 128 / 256
static int g_idleSel = 1;        // 60 / 300 / 600

// 逻辑状态（与配置同步）
static bool s_auto, s_hotkey, s_eco, s_autostart, s_silent;
static bool s_idleDeep = false;
static bool s_autostartInstalled = false;
static volatile LONG g_autoBusy = 0;
static int  s_threshold;

// 编辑框
static HWND g_edWL = nullptr;
static HBRUSH g_edBrush = nullptr;
static HFONT g_edFont = nullptr;
static WNDPROC g_edOldProc = nullptr;

static bool g_dark = true;
static int g_themeMode = 0;   // 0 auto, 1 light, 2 dark

static bool SystemPrefersDark() {
    bool dark = false;
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &hk) == ERROR_SUCCESS) {
        DWORD v = 1, sz = sizeof(v);
        RegQueryValueExW(hk, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&v, &sz);
        RegCloseKey(hk);
        dark = (v == 0);
    }
    return dark;
}
static void ApplyTheme() {
    g_dark = (g_themeMode == 1) ? false : (g_themeMode == 2) ? true : SystemPrefersDark();
    BuildTheme(g_dark);
    if (g_hwnd) {
        BOOL d = g_dark ? TRUE : FALSE;
        DwmSetWindowAttribute(g_hwnd, 20, &d, sizeof(d));
    }
    if (g_edBrush) { DeleteObject(g_edBrush); g_edBrush = nullptr; }
    g_edBrush = CreateSolidBrush(g_dark ? RGB(39, 45, 56) : RGB(241, 245, 249));
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) ux = LoadLibraryW(L"uxtheme.dll");
    if (ux) {
        typedef void (WINAPI* FnMode)(int);
        typedef void (WINAPI* FnFlush)();
        auto fn = (FnMode)GetProcAddress(ux, MAKEINTRESOURCEA(135));
        auto fl = (FnFlush)GetProcAddress(ux, MAKEINTRESOURCEA(136));
        if (fn) fn(g_dark ? 1 : 3);
        if (fl) fl();
    }
    if (g_hwnd) {
        InvalidateRect(g_hwnd, nullptr, FALSE);
        for (HWND e : { g_edWL })
            if (e) InvalidateRect(e, nullptr, TRUE);
    }
}
static void InitThemeFromConfig() {
    if (g_cfg.theme == L"light") g_themeMode = 1;
    else if (g_cfg.theme == L"dark") g_themeMode = 2;
    else g_themeMode = 0;
    ApplyTheme();
}

#include <winsvc.h>
#include <shlobj.h>
#include <imm.h>

struct SysState {
    bool loaded = false;
    bool compression = false, pageCombining = false, prelaunch = false, mmOk = false;
    bool sysmainRunning = false, fastStartupOn = false, wslEnabled = false;
    std::wstring pagingFiles = L"";
    std::wstring pfPath = L"";
    int pfInitMB = 0, pfMaxMB = 0;
    unsigned long long pfAllocMB = 0, pfUseMB = 0, pfPeakMB = 0;
    // 来自 Metrics 的实时值
    bool compOk = false;
    unsigned long long compBytes = 0, nonpaged = 0, paged = 0;
    double compRatio = 0, commitPct = 0;
    unsigned long long commitUsed = 0, commitLimit = 0;
};
static SysState g_sys;
static SRWLOCK g_sysLock = SRWLOCK_INIT;
static volatile LONG g_sysLoading = 0;

// 布局（96dpi 基准，绘制时按 dpi 缩放后的实际像素）
struct Layout {
    float w = 520, h = 600, dpi = 1.0f;
    RectF titlebar, logo, btnSettings, btnMin, btnClose, btnTheme, btnLang;

    // 主屏
    RectF statusTitle, statusSub, gaugeCenter, usedLine, metaLine;
    RectF btnClean, autoRow, lastClean, cardProcess, cardSystem;

    // Sheet
    RectF sheet, sheetTitle, sheetClose, sheetBody;

    // 设置
    RectF setAutoRow, setThreshLbl, setThresh, setThreshVal;
    RectF setCoolLbl, setCoolSeg[4], setStrengthLbl, setStrengthSeg[3], setStrengthHint;
    RectF setWlLbl, setWlEdit, setWlHint, setAutostartRow, setSilentRow;
    RectF setAdvancedRow, setAboutLink, setWlManage;

    // 关于
    RectF aboutLogo, aboutTitle, aboutVer, aboutDesc, aboutRows[3], aboutFoot;


    // 高级
    RectF advMinLbl, advMinSeg[3], advIdleLbl, advIdleSeg[3];
    RectF advHotkeyRow, advEcoRow;
    RectF advIdleDeepRow, advBack;

    // 进程
    RectF procRows[8], procHint, procSub, procHeader;

    // 系统
    RectF sysSummary, sysRows[9], sysMore, sysBtn[5];
};
static Layout L;

static float SD(float v) { return v * L.dpi; }
static void AddHit(CtlId id, CtlKind k, const RectF& r) { g_hits.push_back({ id, k, r }); }

static void BuildLayout(float w, float h, float dpi) {
    g_hits.clear();
    L = Layout();
    L.w = w / dpi; L.h = h / dpi; L.dpi = dpi;

    L.titlebar = RectF(0, 0, L.w, 44);
    L.logo = RectF(20, 9, 26, 26);
    L.btnClose = RectF(L.w - 44, 0, 44, 44);
    L.btnMin = RectF(L.w - 88, 0, 44, 44);
    L.btnSettings = RectF(L.w - 132, 0, 44, 44);
    L.btnTheme = RectF(L.w - 170, 5, 34, 34);
    L.btnLang = RectF(L.w - 216, 5, 40, 34);

    float sh = L.h - 32;
    if (g_sheet == Sheet::Advanced) sh = 380;
    else if (g_sheet == Sheet::Processes) sh = 480;
    else if (g_sheet == Sheet::System) sh = g_systemExpanded ? 520 : 360;
    else if (g_sheet == Sheet::About) sh = 360;
    L.sheet = RectF(16, (L.h - sh) / 2, L.w - 32, sh);
    L.sheetTitle = RectF(L.sheet.X + 24, L.sheet.Y + 12, 260, 28);
    L.sheetClose = RectF(L.sheet.GetRight() - 44, L.sheet.Y + 8, 32, 32);
    L.sheetBody = RectF(L.sheet.X + 24, L.sheet.Y + 62, L.sheet.Width - 48, L.sheet.Height - 82);
    const float bx = L.sheetBody.X, bw = L.sheetBody.Width;

    // 首页几何始终赋值：Sheet 打开时也要有有效矩形。
    // v1.9.3 只在无 Sheet 时赋值，DrawHome 仍会执行，未初始化的 RectF
    // 把首页元素画到随机位置——这就是“遮罩后元素偏移”的根因。
    L.statusTitle = RectF(32, 68, L.w - 64, 28);
    L.statusSub = RectF(32, 98, L.w - 64, 18);
    L.gaugeCenter = RectF((L.w - 160) / 2, 126, 160, 160);
    L.usedLine = RectF(32, 294, L.w - 64, 18);
    L.metaLine = RectF(32, 316, L.w - 64, 18);
    L.btnClean = RectF(32, 348, L.w - 64, 50);
    L.autoRow = RectF(32, 412, L.w - 64, 40);
    L.lastClean = RectF(32, 458, L.w - 64, 16);
    L.cardProcess = RectF(32, 492, 218, 68);
    L.cardSystem = RectF(L.w - 250, 492, 218, 68);

    if (g_sheet == Sheet::None) {
        AddHit(ID_BTN_SETTINGS, K_TITLEBTN, L.btnSettings);
        AddHit(ID_BTN_MIN, K_TITLEBTN, L.btnMin);
        AddHit(ID_BTN_CLOSE, K_TITLEBTN, L.btnClose);
        AddHit(ID_BTN_THEME, K_TITLEBTN, L.btnTheme);
        AddHit(ID_BTN_LANG, K_TITLEBTN, L.btnLang);
        AddHit(ID_BTN_CLEAN, K_BUTTON, L.btnClean);
        AddHit(ID_RING_CLEAN, K_BUTTON, RectF(L.gaugeCenter.X - 10, L.gaugeCenter.Y - 10,
                                             L.gaugeCenter.Width + 20, L.gaugeCenter.Height + 20));
        AddHit(ID_TG_AUTO, K_TOGGLE, L.autoRow);
        AddHit(ID_HOME_PROCESS, K_BUTTON, L.cardProcess);
        AddHit(ID_HOME_SYSTEM, K_BUTTON, L.cardSystem);
        return;
    }

    // 遮罩：只让 Sheet 外区域关闭，Sheet 内留白不误触
    AddHit(ID_SHEET_DISMISS, K_BUTTON, RectF(0, 0, L.w, L.sheet.Y));
    AddHit(ID_SHEET_DISMISS, K_BUTTON, RectF(0, L.sheet.GetBottom(), L.w, L.h - L.sheet.GetBottom()));
    AddHit(ID_SHEET_DISMISS, K_BUTTON, RectF(0, L.sheet.Y, L.sheet.X, L.sheet.Height));
    AddHit(ID_SHEET_DISMISS, K_BUTTON, RectF(L.sheet.GetRight(), L.sheet.Y, L.w - L.sheet.GetRight(), L.sheet.Height));
    AddHit(ID_SHEET_CLOSE, K_TITLEBTN, L.sheetClose);

    if (g_sheet == Sheet::Settings) {
        // v1.9.7：界面（主题/语言）只保留标题栏快切，设置页不再重复，腾出的空间用于分组留白。
        L.setAutoRow = RectF(bx, 76, bw, 36); AddHit(ID_TG_AUTO, K_TOGGLE, L.setAutoRow);
        L.setThreshLbl = RectF(bx, 130, 130, 20);
        L.setThresh = RectF(bx + 136, 130, bw - 196, 20);
        L.setThreshVal = RectF(bx + bw - 52, 128, 52, 22);
        AddHit(ID_SL_THRESH, K_SLIDER, RectF(bx + 126, 118, bw - 180, 44));

        L.setCoolLbl = RectF(bx, 172, 150, 18);
        const float cw = (bw - 24) / 4.0f;
        for (int i = 0; i < 4; i++) {
            L.setCoolSeg[i] = RectF(bx + i * (cw + 8), 192, cw, 30);
            AddHit((CtlId)(ID_SET_COOL_BASE + i), K_SEG, L.setCoolSeg[i]);
        }

        L.setStrengthLbl = RectF(bx, 244, 150, 18);
        const float sw = (bw - 16) / 3.0f;
        for (int i = 0; i < 3; i++) {
            L.setStrengthSeg[i] = RectF(bx + i * (sw + 8), 264, sw, 30);
            AddHit((CtlId)(ID_SET_STRENGTH_BASE + i), K_SEG, L.setStrengthSeg[i]);
        }
        L.setStrengthHint = RectF(bx, 300, bw, 16);

        L.setWlLbl = RectF(bx, 332, bw, 18);
        L.setWlEdit = RectF(bx, 352, bw, 30);
        AddHit(ID_ED_WL, K_EDIT, L.setWlEdit);
        L.setWlHint = RectF(bx, 388, bw, 16);

        L.setAutostartRow = RectF(bx, 418, bw, 34); AddHit(ID_TG_AUTOSTART, K_TOGGLE, L.setAutostartRow);
        L.setSilentRow = RectF(bx, 466, bw, 34); AddHit(ID_TG_SILENT, K_TOGGLE, L.setSilentRow);

        L.setWlManage = RectF(bx + bw - 96, 330, 96, 20); AddHit(ID_SET_PROTECTED, K_BUTTON, L.setWlManage);
        L.setAboutLink = RectF(bx, 532, 150, 34); AddHit(ID_SET_ABOUT, K_BUTTON, L.setAboutLink);
        L.setAdvancedRow = RectF(bx + 158, 532, bw - 158, 34);
        AddHit(ID_SET_ADVANCED, K_BUTTON, L.setAdvancedRow);
        return;
    }

    if (g_sheet == Sheet::Advanced) {
        const float sy = L.sheet.Y;
        L.advBack = RectF(L.sheet.X + 16, sy + 10, 32, 32);
        AddHit(ID_ADV_BACK, K_TITLEBTN, L.advBack);
        L.advMinLbl = RectF(bx, sy + 60, 160, 18);
        const float mw = (bw - 16) / 3.0f;
        for (int i = 0; i < 3; i++) {
            L.advMinSeg[i] = RectF(bx + i * (mw + 8), sy + 82, mw, 30);
            AddHit((CtlId)(ID_ADV_MINWS_BASE + i), K_SEG, L.advMinSeg[i]);
        }
        L.advIdleLbl = RectF(bx, sy + 124, 160, 18);
        for (int i = 0; i < 3; i++) {
            L.advIdleSeg[i] = RectF(bx + i * (mw + 8), sy + 146, mw, 30);
            AddHit((CtlId)(ID_ADV_IDLE_BASE + i), K_SEG, L.advIdleSeg[i]);
        }
        L.advHotkeyRow = RectF(bx, sy + 196, bw, 36); AddHit(ID_TG_HOTKEY, K_TOGGLE, L.advHotkeyRow);
        L.advEcoRow = RectF(bx, sy + 244, bw, 36); AddHit(ID_TG_ECO, K_TOGGLE, L.advEcoRow);
        L.advIdleDeepRow = RectF(bx, sy + 292, bw, 42); AddHit(ID_TG_IDLE_DEEP, K_TOGGLE, L.advIdleDeepRow);
        return;
    }

    if (g_sheet == Sheet::About) {
        const float sy = L.sheet.Y;
        L.aboutLogo = RectF((L.w - 56) / 2, sy + 40, 56, 56);
        L.aboutTitle = RectF(bx, sy + 106, bw, 30);
        L.aboutVer = RectF(bx, sy + 136, bw, 16);
        L.aboutDesc = RectF(bx, sy + 158, bw, 18);
        for (int i = 0; i < 3; i++) {
            L.aboutRows[i] = RectF(bx, sy + 192 + i * 38, bw, 34);
            AddHit((CtlId)(ID_ABOUT_REPO + i), K_BUTTON, L.aboutRows[i]);
        }
        L.aboutFoot = RectF(bx, sy + 318, bw, 16);
        return;
    }

    if (g_sheet == Sheet::Processes) {
        const float sy = L.sheet.Y;
        L.advBack = RectF(L.sheet.X + 16, sy + 10, 32, 32);
        if (g_procFromSettings) AddHit(ID_ADV_BACK, K_TITLEBTN, L.advBack);
        L.procSub = RectF(bx, sy + 50, bw, 16);
        L.procHeader = RectF(bx, sy + 80, bw, 14);
        for (int i = 0; i < 8; i++) {
            L.procRows[i] = RectF(bx, sy + 104 + i * 40, bw, 38);
            AddHit((CtlId)(ID_PROC_BASE + i), K_BUTTON, L.procRows[i]);
        }
        L.procHint = RectF(bx, sy + 432, bw, 16);
        return;
    }

    // 系统
    const float sy = L.sheet.Y;
    L.sysSummary = RectF(bx, sy + 54, bw, 42);
    for (int i = 0; i < 5; i++) L.sysRows[i] = RectF(bx, sy + 98 + i * 34, bw, 32);
    L.sysMore = RectF(bx, sy + 274, bw, 24);
    AddHit(ID_SYS_MORE, K_BUTTON, L.sysMore);
    if (g_systemExpanded) {
        for (int i = 5; i < 9; i++) L.sysRows[i] = RectF(bx, sy + 302 + (i - 5) * 34, bw, 32);
    }
    const float btw = (bw - 24) / 5.0f;
    float btnY = g_systemExpanded ? (L.sheet.GetBottom() - 52) : (sy + 306);
    for (int i = 0; i < 5; i++) {
        L.sysBtn[i] = RectF(bx + i * (btw + 6), btnY, btw, 36);
    }
    AddHit(ID_SYS_MC, K_BUTTON, L.sysBtn[0]);
    AddHit(ID_SYS_PC, K_BUTTON, L.sysBtn[1]);
    AddHit(ID_SYS_PF, K_BUTTON, L.sysBtn[2]);
    AddHit(ID_SYS_WSL, K_BUTTON, L.sysBtn[3]);
    AddHit(ID_SYS_EXPORT, K_BUTTON, L.sysBtn[4]);
}

// 主窗口状态
// ---------------------------------------------------------------------------
static HINSTANCE g_inst = nullptr;
static NOTIFYICONDATAW g_nid{};
static bool g_trayOn = false;
static HICON g_trayIcon = nullptr;
static ULONGLONG g_lastToastMs = 0;
static ULONGLONG g_cleanDoneAt = 0;   // 清理完成后的界面确认（6s）
static std::wstring g_toast;
static int g_lastPct = -1;
static unsigned long long g_lastAvail = 0, g_lastStandby = 0;
static HANDLE g_monThread = nullptr;

// 缓存值（避免每秒全窗重绘）
struct StatCache {
    int pct = -1;
    unsigned long long avail = 0, freeB = 0, standby = 0, commit = 0;
    double hf = -1;
} g_sc;

// 绘制缓存（避免动画期间每秒复制整份进程快照）
static Snapshot g_drawSnap;
static bool g_drawDirty = true;
// 双缓冲
static Bitmap* g_back = nullptr;
static Graphics* g_backG = nullptr;
static int g_backW = 0, g_backH = 0;
static ULONGLONG g_hiddenAt = 0;

static ULONG_PTR g_gdiToken = 0;
static void InitGdiplus() {
    GdiplusStartupInput in;
    GdiplusStartup(&g_gdiToken, &in, nullptr);
}

// Forward
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static void DrawAll(Graphics& g, const RectF* clip = nullptr);
static void UpdateStats(bool force);
static void SyncFromConfig();
static void KickAnim();
static void ShowSystemCheck();
static void OpenSheet(Sheet s);
static void CloseSheet();
static void QuerySysInfo(bool deep);
static void QuerySysInfoAsync();
static void SysAction(int id);
static bool AnimationsEnabled();
static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
static void DestroyGdiObjects();
static void ReleaseBackBuffer();

// ---------------------------------------------------------------------------
// 托盘
// ---------------------------------------------------------------------------
static int TrayIconSize() {
    int s = GetSystemMetrics(SM_CXSMICON);
    return s < 16 ? 16 : s;
}
static HICON MakeTrayIcon(int pct, bool cleaning) {
    int sz = TrayIconSize();
    HDC hdc = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(hdc);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sz;
    bi.bmiHeader.biHeight = -sz;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP mask = CreateBitmap(sz, sz, 1, 1, nullptr);
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        Color c1 = cleaning ? C(255, 120, 128, 140)
                 : pct < 70 ? C(255, 52, 211, 153)
                 : pct < 85 ? C(255, 245, 158, 11)
                 :            C(255, 239, 68, 68);
        Color c2 = cleaning ? C(255, 90, 96, 108)
                 : pct < 70 ? C(255, 16, 185, 129)
                 : pct < 85 ? C(255, 217, 119, 6)
                 :            C(255, 220, 38, 38);
        RectF rr(0, 0, (float)sz, (float)sz);
        FillRoundGrad(g, rr, sz / 4.0f, c1, c2);
        wchar_t txt[8];
        if (cleaning) wcscpy(txt, L"…");
        else swprintf(txt, 8, L"%d", pct > 99 ? 99 : pct);
        if (!fTrayNormal || fTraySz != sz) {
            delete fTrayNormal; fTrayNormal = nullptr;
            delete fTrayClean;  fTrayClean = nullptr;
            fTraySz = sz;
            fTrayNormal = new Font(L"Segoe UI", (float)sz * 0.56f, FontStyleBold, UnitPixel);
            fTrayClean  = new Font(L"Segoe UI", (float)sz * 0.68f, FontStyleBold, UnitPixel);
        }
        Font* tf = cleaning ? fTrayClean : fTrayNormal;
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        SolidBrush wb(Color(255, 255, 255, 255));
        if (tf) g.DrawString(txt, -1, tf, rr, &fmt, &wb);
    }
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(nullptr, hdc);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = bmp;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(bmp);
    DeleteObject(mask);
    return icon;
}
static void TrayUpdate(bool force) {
    int pct;
    unsigned long long avail, standby;
    EnterCriticalSection(&g_lock);
    pct = g_metrics.loadPct;
    avail = g_metrics.availPhys;
    standby = g_metrics.standbyBytes;
    LeaveCriticalSection(&g_lock);
    bool cleaning = g_cleaning != 0;
    // 图标只在百分比变化时重建（DIB+GDI+ 绘制开销大）；
    // tooltip 数值变化走轻量 NIF_TIP 更新，不再触发图标重绘。
    bool pctChanged = force || pct != g_lastPct;
    bool tipChanged = force || pctChanged
        || (avail / (8ULL * MB)) != (g_lastAvail / (8ULL * MB))
        || (standby / (16ULL * MB)) != (g_lastStandby / (16ULL * MB));
    if (!pctChanged && !tipChanged) return;
    g_lastPct = pct;
    g_lastAvail = avail;
    g_lastStandby = standby;
    if (pctChanged || !g_trayOn) {
        HICON icon = MakeTrayIcon(pct, cleaning);
        if (g_trayIcon) DestroyIcon(g_trayIcon);
        g_trayIcon = icon;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.hIcon = icon;
    } else {
        g_nid.uFlags = NIF_MESSAGE | NIF_TIP;
    }
    double commitPct;
    EnterCriticalSection(&g_lock);
    commitPct = g_metrics.commitPct;
    LeaveCriticalSection(&g_lock);
    wchar_t ct[32];
    swprintf(ct, 32, L"%.0f%%", commitPct);
    std::wstring tip = L"MemSweep " + std::to_wstring(pct) + L"% · " +
                       Tx(L"可用 ", L"free ") + FormatBytes(avail) +
                       L" · " + Tx(L"已缓存 ", L"cached ") + FormatBytes(standby) +
                       L" · " + Tx(L"提交 ", L"commit ") + ct;
    wcsncpy(g_nid.szTip, tip.c_str(), 127);
    Shell_NotifyIconW(g_trayOn ? NIM_MODIFY : NIM_ADD, &g_nid);
    g_trayOn = true;
}
static void TrayRemove() {
    if (g_trayOn) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_trayOn = false;
    }
    if (g_trayIcon) { DestroyIcon(g_trayIcon); g_trayIcon = nullptr; }
}
static void Balloon(const std::wstring& text, DWORD flags = NIIF_INFO) {
    if (!g_trayOn) return;
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    wcsncpy(n.szInfo, text.c_str(), 255);
    std::wstring title = L"MemSweep";
    wcsncpy(n.szInfoTitle, title.c_str(), 63);
    n.dwInfoFlags = flags | NIIF_RESPECT_NO_NOTIFY;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

// ---------------------------------------------------------------------------
// 开机自启
// ---------------------------------------------------------------------------
static int RunHidden(const std::wstring& exe, const std::wstring& args, DWORD* outCode = nullptr) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (outCode) *outCode = code;
    return (int)code;
}
static std::wstring ExePath() {
    wchar_t p[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return p;
}
bool AutostartInstalled() {
    return RunHidden(L"C:\\Windows\\System32\\schtasks.exe",
                     L"/Query /TN \"" + std::wstring(kTaskName) + L"\"") == 0;
}
bool AutostartSet(bool on) {
    if (on) {
        std::wstring tr = L"/Create /F /TN \"" + std::wstring(kTaskName) +
                          L"\" /TR \"\\\"" + ExePath() +
                          L"\\\" /tray\" /SC ONLOGON /RL HIGHEST";
        DWORD code = 0;
        RunHidden(L"C:\\Windows\\System32\\schtasks.exe", tr, &code);
        Log(L"创建开机自启任务：%s (code %d)", code == 0 ? L"成功" : L"失败", (int)code);
        if (code == 0) {
            // 笔记本电源策略与执行时限：默认设置会因电池供电 / 3 天限制而停止托盘程序
            std::wstring ps =
                L"-NoProfile -ExecutionPolicy Bypass -Command \"$s = New-ScheduledTaskSettingsSet "
                L"-AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable "
                L"-ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew; "
                L"Set-ScheduledTask -TaskName 'MemSweep' -Settings $s | Out-Null\"";
            RunHidden(L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", ps);
        }
        return code == 0;
    }
    DWORD code = 0;
    RunHidden(L"C:\\Windows\\System32\\schtasks.exe",
              L"/Delete /F /TN \"" + std::wstring(kTaskName) + L"\"", &code);
    Log(L"移除开机自启任务：%s", code == 0 ? L"成功" : L"失败");
    return code == 0;
}

static DWORD WINAPI AutostartThread(LPVOID p) {
    bool want = p != nullptr;
    bool ok = AutostartSet(want);
    InterlockedExchange(&g_autoBusy, 0);
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_AUTOSTART, ok ? 1 : 0, want ? 1 : 0);
    return 0;
}
static void SetAutostartAsync(bool want) {
    if (InterlockedCompareExchange(&g_autoBusy, 1, 0) != 0) {
        g_toast = Tx(L"正在设置开机自启…", L"Updating startup…");
        g_lastToastMs = GetTickCount64();
        KickAnim();
        return;
    }
    HANDLE h = CreateThread(nullptr, 0, AutostartThread, (LPVOID)(intptr_t)(want ? 1 : 0), 0, nullptr);
    if (h) CloseHandle(h);
    else InterlockedExchange(&g_autoBusy, 0);
}

// ---------------------------------------------------------------------------
// 配置同步
// ---------------------------------------------------------------------------
static int NearestIndex(int value, const int* values, int n) {
    int best = 0, bestd = 1 << 30;
    for (int i = 0; i < n; i++) {
        int d = value > values[i] ? value - values[i] : values[i] - value;
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

static void SyncFromConfig() {
    s_auto = g_cfg.autoGuard;
    s_hotkey = g_cfg.hotkey;
    s_eco = g_cfg.ecoQoS;
    s_silent = g_cfg.silent;
    s_threshold = g_cfg.threshold;
    s_autostart = g_cfg.autostart;
    s_autostartInstalled = g_cfg.autostart;
    g_sliderValue = (float)g_cfg.threshold;
    g_gaugeTarget = (float)g_metrics.loadPct;

    static const int cools[4] = { 30, 60, 180, 300 };
    static const int mins[3] = { 64, 128, 256 };
    static const int idles[3] = { 60, 300, 600 };
    g_coolSel = NearestIndex(g_cfg.cooldownSec, cools, 4);
    g_minWSSel = NearestIndex(g_cfg.minWSAutoMB, mins, 3);
    g_idleSel = NearestIndex(g_cfg.idleSecs, idles, 3);
    g_strength = (g_cfg.purgeFullManual && g_cfg.sysCache) ? 2
               : (g_cfg.sysCache ? 1 : 0);
    s_idleDeep = g_cfg.idleDeep;
    g_wlDraft = g_cfg.whitelist;
    if (g_edWL) SetWindowTextW(g_edWL, g_cfg.whitelist.c_str());
    g_coolDirty = g_minDirty = g_idleDirty = g_strengthDirty = false;
    g_anim.clear();
}

static void CommitSettings() {
    g_cfg.autoGuard = s_auto;
    g_cfg.hotkey = s_hotkey;
    g_cfg.ecoQoS = s_eco;
    g_cfg.silent = s_silent;
    g_cfg.threshold = s_threshold;

    static const int cools[4] = { 30, 60, 180, 300 };
    static const int mins[3] = { 64, 128, 256 };
    static const int idles[3] = { 60, 300, 600 };
    if (g_coolDirty) g_cfg.cooldownSec = cools[ClampInt(g_coolSel, 0, 3)];
    if (g_minDirty) g_cfg.minWSAutoMB = mins[ClampInt(g_minWSSel, 0, 2)];
    if (g_idleDirty) g_cfg.idleSecs = idles[ClampInt(g_idleSel, 0, 2)];

    g_cfg.idleDeep = s_idleDeep;
    // 只有用户主动改动力度时才覆盖范围开关，避免吞掉自定义配置；
    // 合并页跟随「深度」力度，不再单独暴露开关
    if (g_strengthDirty) {
        g_cfg.purgeLow = true;
        g_cfg.purgeFullManual = (g_strength >= 2);
        g_cfg.sysCache = (g_strength >= 1);
        g_cfg.trimWS = true;
        g_cfg.combinePages = (g_strength >= 2);
    }

    wchar_t buf[512] = {};
    if (g_edWL) GetWindowTextW(g_edWL, buf, 512);
    std::wstring wl = buf;
    if (wl.empty() && !g_wlDraft.empty()) wl = g_wlDraft;
    {
        std::vector<std::wstring> canon;
        for (auto raw : SplitCSV(wl)) {
            std::wstring n = NormalizeProcName(raw);
            if (!n.empty() && std::find(canon.begin(), canon.end(), n) == canon.end())
                canon.push_back(n);
        }
        g_cfg.whitelist = JoinCSV(canon);
    }
    g_cfg.autostart = s_autostart;
    SaveConfig();
    RebuildWhitelist();
    UnregisterHotKey(g_hwnd, HOTKEY_ID);
    if (g_cfg.hotkey) {
        if (!RegisterHotKey(g_hwnd, HOTKEY_ID, MOD_CONTROL | MOD_SHIFT, 'M'))
            Log(L"热键 Ctrl+Shift+M 注册失败（可能被其他程序占用）");
    }
    if (s_autostart != s_autostartInstalled) SetAutostartAsync(s_autostart);
    if (!g_cfg.ecoQoS) RevertBackgroundTaming();
    g_coolDirty = g_minDirty = g_idleDirty = g_strengthDirty = false;
    g_savedAt = GetTickCount64();
    Log(L"设置已自动保存");
}

// 绘制：卡片与控件
// ---------------------------------------------------------------------------
static std::wstring EditValue(HWND h, const wchar_t* fallback) {
    if (!h) return fallback;
    wchar_t buf[512] = {};
    GetWindowTextW(h, buf, 511);
    return buf[0] ? std::wstring(buf) : std::wstring(fallback);
}
static void DrawEditBox(Graphics& g, const RectF& r, const std::wstring& value) {
    if (r.Width <= 0 || r.Height <= 0) return;
    FillRound(g, r, 7, T.trackFill);
    StrokeRound(g, r, 7, T.border, 1);
    if (value.empty()) {
        TextAt(g, Tx(L"例如 chrome, idea64", L"e.g. chrome, idea64"),
               RectF(r.X + 9, r.Y, r.Width - 16, r.Height), f12, T.text3,
               StringAlignmentNear, StringAlignmentCenter);
        return;
    }
    TextAt(g, value, RectF(r.X + 9, r.Y, r.Width - 16, r.Height), f12, T.text,
           StringAlignmentNear, StringAlignmentCenter, true);
}
static void HideEdits() {
    if (g_edWL) ShowWindow(g_edWL, SW_HIDE);
}
static void PositionEdits() {
    const bool settings = !g_snapshotMode && !g_sheetAnimating && g_sheet == Sheet::Settings;
    if (!g_edWL) return;
    if (!settings || L.setWlEdit.Width <= 0) {
        ShowWindow(g_edWL, SW_HIDE);
        return;
    }
    MoveWindow(g_edWL, (int)SD(L.setWlEdit.X + 2), (int)SD(L.setWlEdit.Y + 2),
               (int)SD(L.setWlEdit.Width - 4), (int)SD(L.setWlEdit.Height - 4), TRUE);
    ShowWindow(g_edWL, SW_SHOWNA);
}
// 原生 Edit 会盖住 GDI+ 占位符；用系统 cue banner 显示示例，语言切换时同步。
static void UpdateWhitelistCue() {
    if (!g_edWL) return;
    SendMessageW(g_edWL, EM_SETCUEBANNER, TRUE,
                 (LPARAM)(g_zh.load(std::memory_order_relaxed) ? L"例如 chrome, idea64"
                                                                : L"e.g. chrome, idea64"));
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 绘制：One Screen + Sheet
// ---------------------------------------------------------------------------
static void DrawSystemSheet(Graphics& g);
static float ToggleValue(int id, bool on);
static void CommitSettings();

static void DrawBackground(Graphics& g) {
    LinearGradientBrush bg(RectF(0, 0, L.w, L.h), T.bg1, T.bg2, LinearGradientModeVertical);
    g.FillRectangle(&bg, RectF(0, 0, L.w, L.h));
}

// 标题栏图标：统一 1.5px 圆头描边、16px 光学尺寸、30px 悬停面。
// 主题与语言都显示“点击后”的状态：浅色→月亮（切深色），深色→太阳（切浅色）；
// 中文→EN，英文→中。这样两个快切按钮的语义一致。
static void DrawIconButton(Graphics& g, const RectF& r, int id, bool close, bool settings) {
    bool hv = g_hover == id;
    const bool danger = (id == ID_BTN_CLOSE);
    float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2;
    // 悬停面用不透明色：月牙的“咬口”需要精确匹配底色，半透明会露边。
    const Color hoverFill = danger
        ? C(255, g_dark ? 80 : 246, g_dark ? 30 : 230, g_dark ? 30 : 230)
        : C(255, g_dark ? 33 : 234, g_dark ? 36 : 236, g_dark ? 41 : 239);
    if (hv) {
        const float hs = 30.0f;
        FillRound(g, RectF(cx - hs / 2, cy - hs / 2, hs, hs), 8, hoverFill);
    }
    Color c = hv ? (danger ? T.danger : T.text) : T.text2;
    Pen pen(c, 1.5f);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    if (id == ID_BTN_THEME) {
        if (!g_dark) {
            // 月牙 = 实心圆 + 用当前表面色“咬”掉一块。13px 下比描边路径更干净；
            // 与太阳（纯描边）形成昼夜差异，但笔画重量一致。
            SolidBrush mb(c);
            g.FillEllipse(&mb, RectF(cx - 6.5f, cy - 6.5f, 13, 13));
            SolidBrush bite(hv ? hoverFill : T.bg1);
            g.FillEllipse(&bite, RectF(cx - 2.6f, cy - 7.2f, 11.4f, 11.4f));
        } else {
            g.DrawEllipse(&pen, RectF(cx - 4.2f, cy - 4.2f, 8.4f, 8.4f));
            for (int i = 0; i < 8; i++) {
                float a = i * 3.1415926f / 4.0f;
                float dx = cosf(a), dy = sinf(a);
                g.DrawLine(&pen, cx + dx * 6.8f, cy + dy * 6.8f, cx + dx * 8.8f, cy + dy * 8.8f);
            }
        }
        return;
    }
    if (id == ID_BTN_LANG) {
        TextAt(g, g_zh ? L"EN" : L"中", RectF(r.X, r.Y, r.Width, r.Height), f11b,
               c, StringAlignmentCenter);
        return;
    }
    if (close) {
        g.DrawLine(&pen, cx - 4.5f, cy - 4.5f, cx + 4.5f, cy + 4.5f);
        g.DrawLine(&pen, cx + 4.5f, cy - 4.5f, cx - 4.5f, cy + 4.5f);
    } else if (settings) {
        // 设置：双滑杆。线段在旋钮处断开，全部用同一笔画，不填任何底色。
        g.DrawLine(&pen, cx - 7, cy - 4, cx - 4.5f, cy - 4);
        g.DrawEllipse(&pen, RectF(cx - 4.5f, cy - 6.5f, 5, 5));
        g.DrawLine(&pen, cx + 0.5f, cy - 4, cx + 7, cy - 4);
        g.DrawLine(&pen, cx - 7, cy + 4, cx - 0.5f, cy + 4);
        g.DrawEllipse(&pen, RectF(cx - 0.5f, cy + 1.5f, 5, 5));
        g.DrawLine(&pen, cx + 4.5f, cy + 4, cx + 7, cy + 4);
    } else {
        g.DrawLine(&pen, cx - 6, cy + 0.5f, cx + 6, cy + 0.5f);
    }
}

// 品牌标记：与 res/icon.ico 同一套几何（256 网格、圆角 squircle、白色 M）。
// 图标生成脚本使用同样的坐标，改这里必须同步改小尺寸图标。
static void DrawBrandMark(Graphics& g, const RectF& box) {
    const float k = box.Width / 256.0f;
    GraphicsState st = g.Save();
    g.TranslateTransform(box.X, box.Y);
    g.ScaleTransform(k, k);
    GraphicsPath sq;
    AddRound(sq, RectF(4, 4, 248, 248), 56);
    LinearGradientBrush br(RectF(4, 4, 248, 248), C(255, 52, 211, 153), C(255, 4, 120, 87),
                          LinearGradientModeForwardDiagonal);
    g.FillPath(&br, &sq);
    Pen hl(C(28, 255, 255, 255), 1.5f);
    g.DrawPath(&hl, &sq);
    // M：圆头折线，和 UI 的圆角语言一致
    Pen mp(C(255, 255, 255, 255), 36.0f);
    mp.SetLineJoin(LineJoinRound);
    mp.SetStartCap(LineCapRound);
    mp.SetEndCap(LineCapRound);
    PointF pts[5] = { PointF(60, 194), PointF(60, 72), PointF(128, 146),
                      PointF(196, 72), PointF(196, 194) };
    g.DrawLines(&mp, pts, 5);
    g.Restore(st);
}

static void DrawChrome(Graphics& g) {
    DrawBrandMark(g, L.logo);
    TextAt(g, L"MemSweep", RectF(L.logo.GetRight() + 10, 8, 240, 28), f15b, T.text);
    DrawIconButton(g, L.btnLang, ID_BTN_LANG, false, false);
    DrawIconButton(g, L.btnTheme, ID_BTN_THEME, false, false);
    DrawIconButton(g, L.btnSettings, ID_BTN_SETTINGS, false, true);
    DrawIconButton(g, L.btnMin, ID_BTN_MIN, false, false);
    DrawIconButton(g, L.btnClose, ID_BTN_CLOSE, true, false);
}

static void DrawSwitch(Graphics& g, const RectF& r, bool on, float anim) {
    RectF track(r.GetRight() - 44, r.Y + (r.Height - 24) / 2, 44, 24);
    BYTE a = (BYTE)(255 * (0.35f + 0.65f * anim));
    FillRound(g, track, 12, on ? C(a, T.accent.GetR(), T.accent.GetG(), T.accent.GetB()) : T.trackOff);
    float kx = track.X + 3 + (track.Width - 24 - 6) * ClampF(anim, 0, 1);
    SolidBrush kb(C(255, 255, 255, 255));
    g.FillEllipse(&kb, RectF(kx, track.Y + 3, 18, 18));
    Pen kp(C(g_dark ? 60 : 40, 0, 0, 0), 1);
    g.DrawEllipse(&kp, RectF(kx, track.Y + 3, 18, 18));
}

static void DrawSwitchRow(Graphics& g, const RectF& r, const wchar_t* zh, const wchar_t* en,
                          bool on, float anim, const wchar_t* subZh = nullptr, const wchar_t* subEn = nullptr) {
    TextAt(g, Tx(zh, en), RectF(r.X, r.Y + (subZh ? 3 : 0), r.Width - 58, subZh ? 18 : r.Height),
           f12, T.text);
    if (subZh) {
        TextAt(g, Tx(subZh, subEn ? subEn : subZh), RectF(r.X, r.Y + 21, r.Width - 58, 15),
               f11, T.text3);
    }
    DrawSwitch(g, r, on, anim);
}

static void DrawSliderBar(Graphics& g, const RectF& r, float value) {
    float y = r.Y + r.Height / 2;
    FillRound(g, RectF(r.X, y - 3, r.Width, 6), 3, T.track);
    float t = ClampF((value - 60) / 35.0f, 0, 1);
    if (r.Width * t > 2) FillRound(g, RectF(r.X, y - 3, r.Width * t, 6), 3, T.accent);
    float kx = r.X + r.Width * t;
    SolidBrush kb(C(255, 255, 255, 255));
    g.FillEllipse(&kb, RectF(kx - 8, y - 8, 16, 16));
    Pen ring(T.accent, 2);
    g.DrawEllipse(&ring, RectF(kx - 8, y - 8, 16, 16));
}

static void DrawSeg(Graphics& g, const RectF* rs, const wchar_t* const* zh,
                    const wchar_t* const* en, int n, int active) {
    for (int i = 0; i < n; i++) {
        bool act = (i == active);
        FillRound(g, rs[i], 8, act ? T.accent : T.trackFill);
        TextAt(g, Tx(zh[i], en[i]), rs[i], act ? f11b : f11,
               act ? T.btnText : T.text2, StringAlignmentCenter);
    }
}

static unsigned long long ProcPrivateWS(const ProcInfo& p) {
    return p.wsPrivate ? p.wsPrivate : p.ws;
}

static void DrawHomeCard(Graphics& g, const RectF& r, int id,
                         const wchar_t* titleZh, const wchar_t* titleEn,
                         const std::wstring& subText, Color subColor) {
    bool hover = g_hover == id;
    bool press = g_pressed == id;
    RectF box = r;
    if (press) box.Y += 1.0f;
    Color fill = hover
        ? (g_dark ? C(255, 36, 40, 48) : C(255, 255, 255, 255))
        : (g_dark ? C(255, 26, 28, 34) : C(255, 245, 246, 249));
    Color border = hover ? (g_dark ? C(120, 52, 211, 153) : C(160, 16, 185, 129))
                         : (g_dark ? C(40, 255, 255, 255) : C(20, 0, 0, 0));
    FillRound(g, box, 12, fill);
    StrokeRound(g, box, 12, border, 1.0f);

    TextAt(g, Tx(titleZh, titleEn), RectF(box.X + 16, box.Y + 13, box.Width - 32, 20),
           f12b, hover ? T.accent : T.text);
    TextAt(g, subText, RectF(box.X + 16, box.Y + 37, box.Width - 32, 18),
           f11, subColor);
}

static void DrawHome(Graphics& g) {
    const Metrics& m = g_drawSnap.m;
    bool cleaning = g_cleaning != 0;
    bool tidying = cleaning && g_cleanPhase == 2;
    bool paused = AutoPaused();
    const wchar_t* title = L"内存充足";
    const wchar_t* titleEn = L"Plenty of memory";
    const wchar_t* sub = L"无需清理";
    const wchar_t* subEn = L"no cleaning needed";
    Color tc = T.accent;
    wchar_t doneSub[96] = {};
    const bool justCleaned = g_cleanDoneAt && GetTickCount64() - g_cleanDoneAt < 6000 && !cleaning;
    if (justCleaned) {
        unsigned long long gain = g_lastFreeGain.load() > g_lastAvailGain.load()
                                ? g_lastFreeGain.load() : g_lastAvailGain.load();
        title = L"清理完成"; titleEn = L"Cleaned";
        if (gain >= 16ULL * MB)
            swprintf(doneSub, 96, Tx(L"已释放 %s · 可用 %s", L"Freed %s · %s available"),
                     FormatBytes(gain).c_str(), FormatBytes(m.availPhys).c_str());
        else
            swprintf(doneSub, 96, Tx(L"可用 %s", L"%s available"),
                     FormatBytes(m.availPhys).c_str());
        sub = doneSub; subEn = doneSub;
        tc = T.accent;
    } else if (tidying) {
        title = L"正在合并内存页"; titleEn = L"Merging pages";
        sub = L"正在合并重复页面，请稍候"; subEn = L"merging duplicate pages, please wait";
        tc = T.accent;
    } else if (cleaning) {
        title = L"正在清理"; titleEn = L"Cleaning";
        sub = L"前台与焦点进程不会被触碰"; subEn = L"foreground and focused apps stay untouched";
        tc = T.accent;
    } else if (paused) {
        title = L"自动清理已暂停"; titleEn = L"Auto clean paused";
        sub = L"在托盘菜单中恢复自动清理"; subEn = L"resume from the tray menu";
        tc = T.warn;
    } else if (g_gameMode.load(std::memory_order_relaxed)) {
        title = L"全屏/演示模式"; titleEn = L"Fullscreen / presenting";
        sub = L"自动清理已暂停，不打扰当前应用"; subEn = L"auto clean paused to protect the foreground";
        tc = T.text2;
    } else if (m.loadPct >= 90) {
        title = L"内存紧张"; titleEn = L"Memory is tight";
        sub = L"建议立即清理后台进程"; subEn = L"clean background apps now";
        tc = T.danger;
    } else if (m.loadPct >= 80) {
        title = L"内存偏高"; titleEn = L"Memory is high";
        sub = L"建议清理后台进程"; subEn = L"clean background apps";
        tc = T.warn;
    }
    TextAt(g, Tx(title, titleEn), L.statusTitle, f20b, tc);
    TextAt(g, Tx(sub, subEn), L.statusSub, f12, T.text3);

    float shown = ClampF(g_gaugeShown, 0.0f, 100.0f);
    Color ring = shown >= 90 ? T.danger : (shown >= 80 ? T.warn : T.accent);
    bool ringHover = g_hover == ID_RING_CLEAN;
    float pulse = 0.0f;
    if (g_ringPulseAt) {
        float life = (float)(GetTickCount64() - g_ringPulseAt) / 420.0f;
        pulse = ClampF(1.0f - life, 0.0f, 1.0f);
    }
    // 悬停外圈：缓动光晕（g_ringHalo 0..1）
    float halo = g_ringHalo;
    if (halo > 0.01f && !cleaning) {
        Pen haloPen(C((BYTE)(28 * halo), ring.GetR(), ring.GetG(), ring.GetB()), 18.0f);
        haloPen.SetStartCap(LineCapRound);
        haloPen.SetEndCap(LineCapRound);
        g.DrawEllipse(&haloPen, L.gaugeCenter);
    }
    Pen track(T.track, 8.0f);
    track.SetStartCap(LineCapRound);
    track.SetEndCap(LineCapRound);
    g.DrawEllipse(&track, L.gaugeCenter);
    float arcW = 8.0f + 2.0f * halo;
    Pen arc(ring, arcW);
    arc.SetStartCap(LineCapRound);
    arc.SetEndCap(LineCapRound);
    float spanDeg = 360.0f * ClampF(shown / 100.0f, 0, 1);
    g.DrawArc(&arc, L.gaugeCenter, -90.0f, spanDeg);
    // 环境高光：沿进度弧从起点滑到终点（7s 一圈，静止时的生命感）
    if (spanDeg > 20.0f && AnimationsEnabled()) {
        float a = -90.0f + spanDeg * g_ambientPhase;
        float a0 = a - 9.0f, a1 = a + 9.0f;
        if (a0 < -90.0f) a0 = -90.0f;
        if (a1 > -90.0f + spanDeg) a1 = -90.0f + spanDeg;
        if (a1 - a0 > 1.0f) {
            Pen shine(C(70, 255, 255, 255), arcW);
            shine.SetStartCap(LineCapRound);
            shine.SetEndCap(LineCapRound);
            g.DrawArc(&shine, L.gaugeCenter, a0, a1 - a0);
        }
    }

    // 点击波纹
    if (pulse > 0.0f) {
        float inflate = (1.0f - pulse) * 14.0f;
        RectF pr(L.gaugeCenter.X - inflate, L.gaugeCenter.Y - inflate,
                 L.gaugeCenter.Width + inflate * 2, L.gaugeCenter.Height + inflate * 2);
        Pen pp(C((BYTE)(80 * pulse), ring.GetR(), ring.GetG(), ring.GetB()), 2.0f);
        g.DrawEllipse(&pp, pr);
    }
    // 清理中的旋转细弧
    if (cleaning) {
        RectF spin(L.gaugeCenter.X + 7, L.gaugeCenter.Y + 7,
                   L.gaugeCenter.Width - 14, L.gaugeCenter.Height - 14);
        Pen sp(C(210, T.accent2.GetR(), T.accent2.GetG(), T.accent2.GetB()), 2.4f);
        sp.SetStartCap(LineCapRound);
        sp.SetEndCap(LineCapRound);
        g.DrawArc(&sp, spin, g_spin, 62.0f);
    }

    wchar_t big[16];
    swprintf(big, 16, L"%d", (int)(shown + 0.5f));
    TextAt(g, big, RectF(L.gaugeCenter.X - 10, L.gaugeCenter.Y + 36, L.gaugeCenter.Width + 20, 64),
           f42b, T.text, StringAlignmentCenter);
    const wchar_t* ringLabel = tidying ? Tx(L"合并中", L"merging")
                            : cleaning ? Tx(L"清理中", L"cleaning")
                            : ringHover ? Tx(L"点击清理", L"click to clean")
                                        : Tx(L"使用中", L"in use");
    TextAt(g, ringLabel, RectF(L.gaugeCenter.X, L.gaugeCenter.Y + 102,
           L.gaugeCenter.Width, 18), f12, ringHover || cleaning ? T.accent : T.text3,
           StringAlignmentCenter);

    wchar_t used[96];
    swprintf(used, 96, L"%s / %s", FormatBytes(m.totalPhys - m.availPhys).c_str(),
             FormatBytes(m.totalPhys).c_str());
    TextAt(g, used, L.usedLine, f12, T.text2, StringAlignmentCenter);
    wchar_t meta[160];
    unsigned long long reclaim = g_reclaimableBytes.load();
    if (reclaim >= 16ULL * MB)
        swprintf(meta, 160, Tx(L"可释放约 %s · 可用 %s", L"~%s reclaimable · %s available"),
                 FormatBytes(reclaim).c_str(), FormatBytes(m.availPhys).c_str());
    else
        swprintf(meta, 160, Tx(L"可用 %s", L"%s available"),
                 FormatBytes(m.availPhys).c_str());
    TextAt(g, meta, L.metaLine, f11, T.text3, StringAlignmentCenter);

    {
        bool hover = g_hover == ID_BTN_CLEAN;
        bool press = g_pressed == ID_BTN_CLEAN;
        RectF r = L.btnClean;
        r.Y += press ? 1.0f : 0.0f;
        FillRound(g, r, 14, hover ? C(255, 30, 198, 138) : T.accent);
        if (cleaning) {
            // 清理中流光：1.6s 扫过一次，裁剪在按钮圆角内
            float ph = (float)(GetTickCount64() % 1600) / 1600.0f;
            float bx = r.X - 80.0f + (r.Width + 160.0f) * ph;
            Gdiplus::Region btnClip(r);
            GraphicsState stB = g.Save();
            g.SetClip(&btnClip);
            LinearGradientBrush lb(RectF(bx - 80.0f, r.Y, 240.0f, r.Height),
                                   C(0, 255, 255, 255), C(70, 255, 255, 255),
                                   LinearGradientModeHorizontal);
            g.FillRectangle(&lb, RectF(bx, r.Y + 4.0f, 80.0f, r.Height - 8.0f));
            g.Restore(stB);
        }
        TextAt(g, tidying ? Tx(L"合并中…", L"Merging…")
                          : cleaning ? Tx(L"清理中…", L"Cleaning…") : Tx(L"立即清理", L"Clean now"),
               RectF(r.X + 24, r.Y, r.Width - 160, r.Height), f15b, T.btnText);
        TextAt(g, L"Ctrl+Shift+M", RectF(r.X + r.Width - 140, r.Y, 118, r.Height),
               f11, C(210, 8, 24, 18), StringAlignmentFar);
    }

    TextAt(g, Tx(L"自动清理", L"Auto clean"), RectF(L.autoRow.X, L.autoRow.Y + 1,
           L.autoRow.Width - 58, 19), f12, T.text);
    {
        wchar_t sub2[64];
        swprintf(sub2, 64, s_auto ? Tx(L"已开启 · 高于 %d%% 时清理", L"on · cleans above %d%%")
                                  : Tx(L"已关闭", L"off"), s_threshold);
        TextAt(g, sub2, RectF(L.autoRow.X, L.autoRow.Y + 20, L.autoRow.Width - 58, 15), f11, T.text3);
    }
    DrawSwitch(g, L.autoRow, s_auto, ToggleValue(ID_TG_AUTO, s_auto));

    wchar_t last[256];
    if (g_lastCleanTickMs) {
        ULONGLONG ago = (GetTickCount64() - g_lastCleanTickMs) / 1000;
        wchar_t when[64];
        if (ago < 60) swprintf(when, 64, Tx(L"%llu 秒前", L"%llus ago"), (unsigned long long)ago);
        else if (ago < 3600) swprintf(when, 64, Tx(L"%llu 分钟前", L"%llum ago"),
                                      (unsigned long long)(ago / 60));
        else swprintf(when, 64, Tx(L"%llu 小时前", L"%lluh ago"), (unsigned long long)(ago / 3600));

        wchar_t gain[64] = {};
        unsigned long long freedShown = g_lastFreeGain.load() > g_lastAvailGain.load()
                                      ? g_lastFreeGain.load() : g_lastAvailGain.load();
        if (freedShown >= 16ULL * MB)
            swprintf(gain, 64, Tx(L"释放 %s", L"freed %s"), FormatBytes(freedShown).c_str());
        if (gain[0]) swprintf(last, 256, Tx(L"上次清理 %s · %s", L"last clean %s · %s"), when, gain);
        else swprintf(last, 256, Tx(L"上次清理 %s", L"last clean %s"), when);
    } else {
        wcscpy(last, Tx(L"尚未清理", L"not cleaned yet"));
    }
    TextAt(g, last, L.lastClean, f11, T.text3);

    // 底部基座卡片：Top1 进程与系统状态微摘要
    std::wstring topProcDesc;
    for (const auto& p : g_drawSnap.procs) {
        if (p.pid <= 4 || p.pid == GetCurrentProcessId() || p.name.empty()) continue;
        std::wstring n = NormalizeProcName(p.name);
        if (n.empty() || IsBuiltinProtectedName(n)) continue;
        topProcDesc = n + L" · " + FormatBytes(ProcPrivateWS(p));
        break;
    }
    if (topProcDesc.empty()) {
        topProcDesc = Tx(L"查看所有应用", L"View all apps");
    }
    DrawHomeCard(g, L.cardProcess, ID_HOME_PROCESS,
                 L"进程 ›", L"Processes ›",
                 topProcDesc, T.text3);

    std::wstring sysDesc;
    Color sysColor = T.text3;
    {
        AcquireSRWLockShared(&g_sysLock);
        bool mc = g_sys.compression;
        bool pc = g_sys.pageCombining;
        bool sm = g_sys.sysmainRunning;
        bool mm = g_sys.mmOk;
        ReleaseSRWLockShared(&g_sysLock);

        if (!mm) {
            sysDesc = Tx(L"点击查看状态", L"Click to inspect");
        } else if (!mc || !sm || !pc) {
            sysDesc = Tx(L"建议优化 1 项", L"1 recommendation");
            sysColor = T.warn;
        } else {
            sysDesc = Tx(L"状态良好 · 压缩开启", L"All optimal · Compressed");
        }
    }
    DrawHomeCard(g, L.cardSystem, ID_HOME_SYSTEM,
                 L"系统状态 ›", L"System status ›",
                 sysDesc, sysColor);
}

static void DrawSheetFrame(Graphics& g, const wchar_t* titleZh, const wchar_t* titleEn, bool back) {
    FillRound(g, L.sheet, 20, T.card);
    StrokeRound(g, L.sheet, 20, T.border, 1);
    if (back) {
        bool hv = g_hover == ID_ADV_BACK;
        if (hv) FillRound(g, RectF(L.advBack.X, L.advBack.Y, 32, 32), 8, T.trackFill);
        Pen p(hv ? T.accent : T.text2, 1.5f);
        float cx = L.advBack.X + 18, cy = L.advBack.Y + 16;
        g.DrawLine(&p, cx + 3, cy - 5, cx - 3, cy);
        g.DrawLine(&p, cx - 3, cy, cx + 3, cy + 5);
        TextAt(g, Tx(titleZh, titleEn), RectF(L.sheet.X + 56, L.sheet.Y + 12, 280, 28), f15b, T.text);
    } else {
        TextAt(g, Tx(titleZh, titleEn), L.sheetTitle, f15b, T.text);
    }
    if ((g_sheet == Sheet::Settings || g_sheet == Sheet::Advanced) &&
        g_savedAt && GetTickCount64() - g_savedAt < 1400) {
        TextAt(g, Tx(L"已保存", L"saved"), RectF(L.sheet.GetRight() - 130, L.sheet.Y + 16, 74, 18),
               f11, T.accent, StringAlignmentFar);
    }
    DrawIconButton(g, L.sheetClose, ID_SHEET_CLOSE, true, false);
}

static void RefreshDrawSnap();

enum class ProcState { Normal, Protected, Builtin };
struct ProcRow { std::wstring name, display; unsigned long long ws = 0; DWORD pid = 0; ProcState st = ProcState::Normal; };
static std::vector<ProcRow> g_procList;
static int g_procScroll = 0;

// 一张表同时承担：查看内存占用 / 切换保护 / 右键释放。
// 名字按精确名去 .exe、小写；系统与 VM 内置保护显示为锁，不可切换。
static void BuildProcessRows() {
    std::unordered_map<std::wstring, ProcRow> byName;
    byName.reserve(g_drawSnap.procs.size());
    for (const auto& p : g_drawSnap.procs) {
        if (p.pid <= 4 || p.pid == GetCurrentProcessId() || p.name.empty()) continue;
        std::wstring n = NormalizeProcName(p.name);
        if (n.empty()) continue;
        unsigned long long w = ProcPrivateWS(p);
        auto it = byName.find(n);
        if (it == byName.end() || w > it->second.ws) {
            ProcRow r;
            r.name = n; r.ws = w; r.pid = p.pid;
            r.display = p.name;
            if (r.display.size() > 4 &&
                _wcsicmp(r.display.c_str() + r.display.size() - 4, L".exe") == 0)
                r.display.resize(r.display.size() - 4);
            r.st = IsBuiltinProtectedName(n) ? ProcState::Builtin : ProcState::Normal;
            byName[n] = r;
        }
    }
    for (auto raw : SplitCSV(g_cfg.whitelist)) {
        std::wstring n = NormalizeProcName(raw);
        if (n.empty()) continue;
        auto it = byName.find(n);
        if (it != byName.end()) {
            if (it->second.st != ProcState::Builtin) it->second.st = ProcState::Protected;
        } else {
            ProcRow r;
            r.name = n; r.display = n; r.st = ProcState::Protected;   // 未运行但已保护，排最后
            byName[n] = r;
        }
    }
    g_procList.clear();
    g_procList.reserve(byName.size());
    for (auto& kv : byName) g_procList.push_back(std::move(kv.second));
    // 切换保护不改变排序，避免行在鼠标下跳走
    std::sort(g_procList.begin(), g_procList.end(), [](const ProcRow& a, const ProcRow& b) {
        if (a.ws != b.ws) return a.ws > b.ws;
        return a.name < b.name;
    });
}

static void CommitWhitelist(std::vector<std::wstring> v) {
    const std::wstring joined = JoinCSV(v);
    g_cfg.whitelist = joined;
    g_wlDraft = joined;
    SaveConfig();
    RebuildWhitelist();
    if (g_edWL) SetWindowTextW(g_edWL, joined.c_str());
    g_savedAt = GetTickCount64();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void ToggleProtected(const std::wstring& name) {
    std::wstring n = NormalizeProcName(name);
    if (n.empty()) return;
    std::vector<std::wstring> v = SplitCSV(g_cfg.whitelist);
    for (auto& e : v) e = NormalizeProcName(e);
    auto it = std::find(v.begin(), v.end(), n);
    if (it != v.end()) v.erase(it);
    else v.push_back(n);
    CommitWhitelist(std::move(v));
}

static void DrawProcessSheet(Graphics& g) {
    DrawSheetFrame(g, L"进程", L"Processes", g_procFromSettings);
    RefreshDrawSnap();
    BuildProcessRows();
    TextAt(g, Tx(L"点击右侧「保护」，清理时跳过该应用——不释放它的内存",
                 L"Click Protect to skip this app when cleaning - its memory is left alone"),
           L.procSub, f11, T.text3);
    TextAt(g, Tx(L"应用", L"App"), RectF(L.procHeader.X, L.procHeader.Y, 120, 14), f11, T.text3);
    TextAt(g, Tx(L"内存", L"Memory"), RectF(L.procHeader.GetRight() - 142, L.procHeader.Y, 80, 14),
           f11, T.text3, StringAlignmentFar);
    TextAt(g, Tx(L"保护", L"Protect"), RectF(L.procHeader.GetRight() - 46, L.procHeader.Y, 40, 14),
           f11, T.text3, StringAlignmentCenter);

    const int total = (int)g_procList.size();
    const int maxScroll = std::max(0, total - 8);
    if (g_procScroll > maxScroll) g_procScroll = maxScroll;
    if (g_procScroll < 0) g_procScroll = 0;
    for (int i = 0; i < 8; i++) {
        int idx = g_procScroll + i;
        if (idx >= total) break;
        const ProcRow& r = g_procList[idx];
        const RectF& box = L.procRows[i];
        bool hv = g_hover == (int)(ID_PROC_BASE + i);
        if (hv) FillRound(g, box, 8, T.trackFill);
        Color nameColor = (r.st == ProcState::Builtin) ? T.text3 : T.text;
        TextAt(g, r.display, RectF(box.X + 12, box.Y, box.Width - 168, box.Height), f12, nameColor,
               StringAlignmentNear, StringAlignmentCenter, true);
        if (r.ws)
            TextAt(g, FormatBytes(r.ws), RectF(box.GetRight() - 142, box.Y, 96, box.Height),
                   f11, T.text3, StringAlignmentFar);
        else
            TextAt(g, Tx(L"未运行", L"not running"), RectF(box.GetRight() - 152, box.Y, 106, box.Height),
                   f11, T.text3, StringAlignmentFar);

        float cx = box.GetRight() - 26, cy = box.Y + box.Height / 2;
        if (r.st == ProcState::Builtin) {
            Pen lp(T.text3, 1.5f);
            GraphicsPath body;
            AddRound(body, RectF(cx - 5.5f, cy - 2.5f, 11, 8), 2);
            g.DrawPath(&lp, &body);
            g.DrawArc(&lp, RectF(cx - 4.5f, cy - 8.5f, 9, 11), 180, 180);
        } else if (r.st == ProcState::Protected) {
            SolidBrush fill(T.accent);
            g.FillEllipse(&fill, RectF(cx - 8, cy - 8, 16, 16));
            Pen cp(C(255, 255, 255, 255), 1.8f);
            cp.SetStartCap(LineCapRound); cp.SetEndCap(LineCapRound);
            g.DrawLine(&cp, cx - 4, cy, cx - 1, cy + 3);
            g.DrawLine(&cp, cx - 1, cy + 3, cx + 4, cy - 3);
        } else {
            Pen ring(T.text3, 1.5f);
            g.DrawEllipse(&ring, RectF(cx - 7, cy - 7, 14, 14));
        }
    }
    if (total > 8) {
        float top = L.procRows[0].Y, h = L.procRows[7].GetBottom() - top;
        float th = std::max(24.0f, h * 8.0f / (float)total);
        float ty = top + (h - th) * (float)g_procScroll / (float)std::max(1, maxScroll);
        FillRound(g, RectF(L.sheet.GetRight() - 14, top, 3, h), 1.5f, T.track);
        FillRound(g, RectF(L.sheet.GetRight() - 14, ty, 3, th), 1.5f, T.text3);
    }
    TextAt(g, Tx(L"右键释放内存 · 滚轮浏览 · 系统与虚拟机始终保护",
                 L"Right-click to free · scroll · system/VM always protected"),
           L.procHint, f11, T.text3);
}

static void DrawSettingsSheet(Graphics& g) {
    DrawSheetFrame(g, L"设置", L"Settings", false);
    DrawSwitchRow(g, L.setAutoRow, L"自动清理", L"Auto clean", s_auto,
                  ToggleValue(ID_TG_AUTO, s_auto),
                  s_auto ? L"已开启" : L"已关闭", s_auto ? L"on" : L"off");

    TextAt(g, Tx(L"内存高于", L"Clean above"), L.setThreshLbl, f12, T.text2);
    DrawSliderBar(g, L.setThresh, g_sliderValue);
    wchar_t tv[16];
    swprintf(tv, 16, L"%d%%", (int)(g_sliderValue + 0.5f));
    TextAt(g, tv, L.setThreshVal, f12b, T.accent, StringAlignmentFar);

    TextAt(g, Tx(L"最短间隔", L"Min interval"), L.setCoolLbl, f12, T.text2);
    const wchar_t* coolZh[4] = { L"30 秒", L"1 分钟", L"3 分钟", L"5 分钟" };
    const wchar_t* coolEn[4] = { L"30s", L"1m", L"3m", L"5m" };
    DrawSeg(g, L.setCoolSeg, coolZh, coolEn, 4, g_coolSel);

    TextAt(g, Tx(L"清理力度", L"Cleanup level"), L.setStrengthLbl, f12, T.text2);
    const wchar_t* strZh[3] = { L"温和", L"均衡", L"深度" };
    const wchar_t* strEn[3] = { L"Mild", L"Balanced", L"Deep" };
    DrawSeg(g, L.setStrengthSeg, strZh, strEn, 3, g_strength);
    const wchar_t* hintZh = g_strength == 0 ? L"只清理空闲后台，切回应用不受影响"
                          : g_strength == 1 ? L"推荐：释放与流畅兼顾"
                                            : L"释放更多，应用切回可能稍慢";
    const wchar_t* hintEn = g_strength == 0 ? L"Only idle apps · switching back is unaffected"
                          : g_strength == 1 ? L"Recommended · balanced"
                                            : L"Frees more · apps may reload slower";
    TextAt(g, Tx(hintZh, hintEn), L.setStrengthHint, f11, T.text3);

    TextAt(g, Tx(L"保护名单", L"Protected list"), L.setWlLbl, f12, T.text2);
    bool protHover = g_hover == ID_SET_PROTECTED;
    TextAt(g, Tx(L"选择应用 ›", L"Choose apps ›"), RectF(L.setWlManage.X, L.setWlManage.Y,
           L.setWlManage.Width, L.setWlManage.Height), f11,
           protHover ? T.accent : T.text3, StringAlignmentFar);
    DrawEditBox(g, L.setWlEdit, EditValue(g_edWL, g_cfg.whitelist.c_str()));
    TextAt(g, Tx(L"清理时跳过这些进程；虚拟机始终受保护", L"Skipped when cleaning; VMs are always protected"),
           L.setWlHint, f11, T.text3);

    DrawSwitchRow(g, L.setAutostartRow, L"开机自启", L"Start with Windows", s_autostart,
                  ToggleValue(ID_TG_AUTOSTART, s_autostart));
    DrawSwitchRow(g, L.setSilentRow, L"静默模式", L"Silent mode", s_silent,
                  ToggleValue(ID_TG_SILENT, s_silent),
                  s_silent ? Tx(L"已开启 · 不弹完成通知", L"on · no notifications")
                           : Tx(L"已关闭 · 清理完成后通知", L"off · notify when a clean finishes"));

    bool aboutHover = g_hover == ID_SET_ABOUT;
    FillRound(g, L.setAboutLink, 8, aboutHover ? T.trackFill : C(0, 0, 0, 0));
    TextAt(g, Tx(L"关于 MemSweep", L"About MemSweep"), RectF(L.setAboutLink.X + 8,
           L.setAboutLink.Y, L.setAboutLink.Width - 8, L.setAboutLink.Height), f12,
           aboutHover ? T.accent : T.text2);
    bool advHover = g_hover == ID_SET_ADVANCED;
    FillRound(g, L.setAdvancedRow, 8, advHover ? T.trackFill : C(0, 0, 0, 0));
    TextAt(g, Tx(L"高级设置", L"Advanced"), RectF(L.setAdvancedRow.X, L.setAdvancedRow.Y,
           L.setAdvancedRow.Width - 26, L.setAdvancedRow.Height), f12,
           advHover ? T.accent : T.text2, StringAlignmentFar);
    TextAt(g, L"›", RectF(L.setAdvancedRow.GetRight() - 22, L.setAdvancedRow.Y, 16,
           L.setAdvancedRow.Height), f15b, T.text3, StringAlignmentFar);
}

static void DrawAdvancedSheet(Graphics& g) {
    DrawSheetFrame(g, L"高级设置", L"Advanced", true);
    TextAt(g, Tx(L"只清理大于", L"Clean above"), L.advMinLbl, f12, T.text2);
    const wchar_t* mwZh[3] = { L"64 MB", L"128 MB", L"256 MB" };
    const wchar_t* mwEn[3] = { L"64 MB", L"128 MB", L"256 MB" };
    DrawSeg(g, L.advMinSeg, mwZh, mwEn, 3, g_minWSSel);
    TextAt(g, Tx(L"后台闲置", L"Idle for"), L.advIdleLbl, f12, T.text2);
    const wchar_t* idZh[3] = { L"1 分钟", L"5 分钟", L"10 分钟" };
    const wchar_t* idEn[3] = { L"1m", L"5m", L"10m" };
    DrawSeg(g, L.advIdleSeg, idZh, idEn, 3, g_idleSel);
    DrawSwitchRow(g, L.advHotkeyRow, L"全局热键 Ctrl+Shift+M", L"Global hotkey Ctrl+Shift+M", s_hotkey,
                  ToggleValue(ID_TG_HOTKEY, s_hotkey));
    DrawSwitchRow(g, L.advEcoRow, L"后台效率模式", L"Efficiency mode", s_eco,
                  ToggleValue(ID_TG_ECO, s_eco));
    wchar_t idleSubZh[128] = {}, idleSubEn[128] = {};
    if (g_cfg.idleDeepSecs >= 60) {
        int m = g_cfg.idleDeepSecs / 60;
        swprintf(idleSubZh, 128, L"空闲 %d 分钟后自动清理并合并内存页，返回即中止", m);
        swprintf(idleSubEn, 128, L"auto clean when idle %dm; stops when you're back", m);
    } else {
        swprintf(idleSubZh, 128, L"空闲 %d 秒后自动清理并合并内存页，返回即中止", g_cfg.idleDeepSecs);
        swprintf(idleSubEn, 128, L"auto clean when idle %ds; stops when you're back", g_cfg.idleDeepSecs);
    }
    DrawSwitchRow(g, L.advIdleDeepRow, L"离开时自动清理", L"Clean when away", s_idleDeep,
                  ToggleValue(ID_TG_IDLE_DEEP, s_idleDeep), idleSubZh, idleSubEn);
}

static void OpenUrl(const wchar_t* url) {
    ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

static void DrawAboutSheet(Graphics& g) {
    DrawSheetFrame(g, L"关于", L"About", false);
    DrawBrandMark(g, L.aboutLogo);
    TextAt(g, L"MemSweep", L.aboutTitle, f20b, T.text, StringAlignmentCenter);
#ifdef _WIN64
    const wchar_t* arch = L"x64";
#else
    const wchar_t* arch = L"x86";
#endif
    wchar_t ver[64];
    swprintf(ver, 64, L"v%s · %s", MEMSWEEP_VERSION, arch);
    TextAt(g, ver, L.aboutVer, f11, T.text3, StringAlignmentCenter);
    TextAt(g, Tx(L"轻量、克制的 Windows 内存清理工具",
                 L"A lightweight, restrained Windows memory cleaner"),
           L.aboutDesc, f12, T.text2, StringAlignmentCenter);

    const wchar_t* rowZh[3] = { L"GitHub 仓库", L"问题反馈", L"检查更新" };
    const wchar_t* rowEn[3] = { L"GitHub repository", L"Report an issue", L"Check for updates" };
    for (int i = 0; i < 3; i++) {
        bool hv = g_hover == (int)(ID_ABOUT_REPO + i);
        if (hv) FillRound(g, L.aboutRows[i], 8, T.trackFill);
        TextAt(g, Tx(rowZh[i], rowEn[i]), RectF(L.aboutRows[i].X + 12, L.aboutRows[i].Y,
               L.aboutRows[i].Width - 40, L.aboutRows[i].Height), f12,
               hv ? T.accent : T.text);
        TextAt(g, L"›", RectF(L.aboutRows[i].GetRight() - 26, L.aboutRows[i].Y, 16,
               L.aboutRows[i].Height), f15b, T.text3, StringAlignmentFar);
    }
    TextAt(g, Tx(L"MIT License · 不联网 · 不收集数据 · © 2026 feiyu-labs",
                 L"MIT License · no network · no telemetry · © 2026 feiyu-labs"),
           L.aboutFoot, f11, T.text3, StringAlignmentCenter);
}

static float ToggleValue(int id, bool on) {
    auto it = g_anim.find(id);
    return it == g_anim.end() ? (on ? 1.0f : 0.0f) : it->second;
}

static void DrawToast(Graphics& g) {
    ULONGLONG now = GetTickCount64();
    if (g_toast.empty() || now - g_lastToastMs >= 1800) return;
    float life = (float)(now - g_lastToastMs) / 1800.0f;
    float inA = ClampF(life / 0.12f, 0.0f, 1.0f);
    float outA = life > 0.75f ? 1.0f - (life - 0.75f) / 0.25f : 1.0f;
    BYTE alpha = (BYTE)(255 * inA * outA);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentCenter);
    RectF bounds;
    g.MeasureString(g_toast.c_str(), (INT)g_toast.size(), f12b,
                    RectF(0, 0, 1000, 40), &fmt, &bounds);
    float tw = ClampF(bounds.Width + 36.0f, 200.0f, L.w - 32.0f);
    RectF tr((L.w - tw) / 2, L.h - 48 + (1.0f - inA) * 10.0f, tw, 40);
    FillRound(g, tr, 18, C((BYTE)(alpha * 0.92f), 30, 36, 46));
    StrokeRound(g, tr, 18, C((BYTE)(alpha * 0.5f), 70, 80, 96), 1);
    TextAt(g, g_toast, tr, f12b, C(alpha, 235, 238, 243), StringAlignmentCenter);
}

static void RefreshDrawSnap() {
    if (!g_drawDirty) return;
    EnterCriticalSection(&g_lock);
    g_drawSnap = g_snap;
    LeaveCriticalSection(&g_lock);
    g_drawDirty = false;
}

static void DrawAll(Graphics& g, const RectF* clip) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.ScaleTransform(L.dpi, L.dpi);
    if (clip) g.SetClip(*clip);   // 逻辑坐标；部分重绘时仅栅格化失效区

    RefreshDrawSnap();

    DrawBackground(g);
    if (g_sheet == Sheet::None) {
        DrawChrome(g);
        DrawHome(g);
    } else {
        // Sheet 是模态的：首页不参与绘制。16px 边缘只会露出被裁切的首页碎片，
        // 看起来像元素错位；只留遮罩更干净，也省掉整屏首页绘制。
        float et = EaseOutCubic(ClampF(g_sheetT, 0, 1));
        BYTE sa = (BYTE)(205 * et);
        SolidBrush scrim(C(sa, 8, 12, 18));
        g.FillRectangle(&scrim, RectF(0, 0, L.w, L.h));
        GraphicsState st = g.Save();
        g.TranslateTransform(0, 16.0f * (1.0f - et));
        if (g_sheet == Sheet::Settings) DrawSettingsSheet(g);
        else if (g_sheet == Sheet::Advanced) DrawAdvancedSheet(g);
        else if (g_sheet == Sheet::Processes) DrawProcessSheet(g);
        else if (g_sheet == Sheet::About) DrawAboutSheet(g);
        else DrawSystemSheet(g);
        g.Restore(st);
    }
    DrawToast(g);
}


// 状态刷新
// ---------------------------------------------------------------------------
static void UpdateStats(bool force) {
    EnterCriticalSection(&g_lock);
    Metrics m = g_metrics;
    LeaveCriticalSection(&g_lock);
    bool changed = force
        || m.loadPct != g_sc.pct
        || (m.availPhys / (8ULL * MB)) != (g_sc.avail / (8ULL * MB))
        || (m.freeBytes / (4ULL * MB)) != (g_sc.freeB / (4ULL * MB))
        || (m.standbyBytes / (16ULL * MB)) != (g_sc.standby / (16ULL * MB))
        || (m.commitUsed / (16ULL * MB)) != (g_sc.commit / (16ULL * MB))
        || ((int)(m.hardFaultsPerSec / 10.0)) != ((int)(g_sc.hf / 10.0));
    if (changed) {
        g_drawDirty = true;
        g_sc.pct = m.loadPct;
        g_sc.avail = m.availPhys;
        g_sc.freeB = m.freeBytes;
        g_sc.standby = m.standbyBytes;
        g_sc.commit = m.commitUsed;
        g_sc.hf = m.hardFaultsPerSec;
        g_gaugeTarget = (float)m.loadPct;
        KickAnim();   // 数值变化立即进入 33ms 缓动，不再 1s 步进
    }
    TrayUpdate(force);
    // 自身瘦身（每分钟一次）
    {
        static ULONGLONG lastSelfTrim = 0;
        ULONGLONG nowT = GetTickCount64();
        if (nowT - lastSelfTrim > 60000) {
            lastSelfTrim = nowT;
            EmptyWorkingSet(GetCurrentProcess());
        }
    }
}

// ---------------------------------------------------------------------------
// 动画步进（33ms 定时器专用；帧率无关，与数据刷新彻底分离）
// ---------------------------------------------------------------------------
static bool StepAnimations() {
    ULONGLONG now = GetTickCount64();
    float dt = g_lastAnimMs ? (float)(now - g_lastAnimMs) / 1000.0f : 0.016f;
    if (dt > 0.25f) dt = 0.25f;
    g_lastAnimMs = now;
    bool need = false;

    // 仪表缓动：指数平滑，任何掉帧后都能平滑收敛
    if (g_gaugeShown != g_gaugeTarget) {
        float diff = g_gaugeTarget - g_gaugeShown;
        float t = 1.0f - expf(-dt * 10.0f);
        if (fabsf(diff) < 0.05f) g_gaugeShown = g_gaugeTarget;
        else g_gaugeShown += diff * t;
        need = true;
    }
    // 开关缓动
    for (auto& kv : g_anim) {
        int id = kv.first;
        bool on = (id == ID_TG_AUTO) ? s_auto : (id == ID_TG_HOTKEY) ? s_hotkey
                : (id == ID_TG_ECO) ? s_eco
                : (id == ID_TG_AUTOSTART) ? s_autostart
                : (id == ID_TG_IDLE_DEEP) ? s_idleDeep : s_silent;
        float d = (on ? 1.0f : 0.0f) - kv.second;
        if (fabsf(d) < 0.02f) kv.second = on ? 1.0f : 0.0f;
        else { kv.second += d * (1.0f - expf(-dt * 14.0f)); need = true; }
    }
    // 清理旋转
    if (g_cleaning != 0) {
        g_spin += dt * 260.0f;
        if (g_spin > 360) g_spin -= 360;
        need = true;
    }
    // 悬停光晕缓动
    float haloTarget = (g_hover == ID_RING_CLEAN && g_cleaning == 0) ? 1.0f : 0.0f;
    if (fabsf(haloTarget - g_ringHalo) > 0.01f) {
        g_ringHalo += (haloTarget - g_ringHalo) * (1.0f - expf(-dt * 18.0f));
        need = true;
    } else if (g_ringHalo != haloTarget) {
        g_ringHalo = haloTarget;
    }
    // 涟漪 / Toast 存活期
    if (g_ringPulseAt) {
        if (now - g_ringPulseAt < 420) need = true;
        else g_ringPulseAt = 0;
    }
    if (g_toast.size() && now - g_lastToastMs < 1900) need = true;
    return need;
}

// ---------------------------------------------------------------------------
// 编辑框子类化（Enter 应用）
// ---------------------------------------------------------------------------
static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (h == g_edWL && msg == WM_SETTEXT) {
        LRESULT r = CallWindowProcW(g_edOldProc, h, msg, wp, lp);
        if (lp) g_wlDraft = (const wchar_t*)lp;
        return r;
    }
    if (h == g_edWL && (msg == WM_CHAR || msg == WM_KEYDOWN || msg == WM_PASTE || msg == WM_CUT || msg == WM_CLEAR || msg == WM_UNDO)) {
        LRESULT r = CallWindowProcW(g_edOldProc, h, msg, wp, lp);
        int len = GetWindowTextLengthW(h);
        if (len <= 0) g_wlDraft.clear();
        else {
            wchar_t b[512] = {};
            GetWindowTextW(h, b, 512);
            if (b[0]) g_wlDraft = b;
        }
        return r;
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        CommitSettings();
        SetFocus(g_hwnd);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        CommitSettings();
        SetFocus(g_hwnd);
        CloseSheet();
        return 0;
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == 27)) return 0;
    if (msg == WM_KILLFOCUS) CommitSettings();
    return CallWindowProcW(g_edOldProc, h, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// 命中与交互
// ---------------------------------------------------------------------------
static inline bool InRect(const RectF& r, float x, float y) {
    return x >= r.X && x <= r.X + r.Width && y >= r.Y && y <= r.Y + r.Height;
}
static CtlId HitTest(float x, float y, CtlKind* kind = nullptr) {
    for (auto& h : g_hits) {
        if (x >= h.r.X && x <= h.r.GetRight() && y >= h.r.Y && y <= h.r.GetBottom()) {
            if (kind) *kind = h.kind;
            return h.id;
        }
    }
    return ID_NONE;
}
static void KickAnim() {
    if (!g_hwnd) return;
    SetTimer(g_hwnd, 2, 33, nullptr);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}
static void BringMainToFront() {
    if (!g_hwnd) return;
    g_hiddenAt = 0;
    ShowWindow(g_hwnd, SW_SHOWNORMAL);
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    // 附加到前台线程以绕过前台锁
    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD myThread = GetCurrentThreadId();
    bool attached = false;
    if (fgThread && fgThread != myThread) attached = AttachThreadInput(myThread, fgThread, TRUE) != 0;
    SetForegroundWindow(g_hwnd);
    SetActiveWindow(g_hwnd);
    BringWindowToTop(g_hwnd);
    if (attached) AttachThreadInput(myThread, fgThread, FALSE);
    FLASHWINFO fi{ sizeof(fi), g_hwnd, FLASHW_ALL, 3, 120 };
    FlashWindowEx(&fi);
    // 仍被全屏应用挡住 → 气泡提示
    if (GetForegroundWindow() != g_hwnd)
        Balloon(Tx(L"面板已打开；若被全屏游戏遮挡，请按 Alt+Tab 切换",
                   L"Panel opened; if hidden by a fullscreen game, press Alt+Tab"));
}
static void ToggleAnimStart(int id) {
    if (!g_anim.count(id)) g_anim[id] = 0.0f;
    if (g_hwnd) SetTimer(g_hwnd, 2, 33, nullptr);
}
static bool AnimationsEnabled() {
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
    return enabled != FALSE;
}

static void FinishSheetAnim() {
    if (g_hwnd) KillTimer(g_hwnd, 3);
    g_sheetT = 1.0f;
    g_sheetAnimating = false;
    PositionEdits();
}

static void OpenSheet(Sheet s) {
    if (g_sheet == s) return;
    HWND focus = GetFocus();
    if (focus && focus != g_hwnd && GetParent(focus) == g_hwnd) SetFocus(g_hwnd);
    HideEdits();
    g_sheet = s;
    g_sheetT = AnimationsEnabled() && IsWindowVisible(g_hwnd) ? 0.0f : 1.0f;
    g_sheetStart = GetTickCount64();
    g_sheetAnimating = g_sheetT < 1.0f;
    g_systemExpanded = false;
    if (s == Sheet::Processes) { RefreshDrawSnap(); g_procScroll = 0; BuildProcessRows(); }
    if (s == Sheet::Settings && g_edWL) {
        SetWindowTextW(g_edWL, g_cfg.whitelist.c_str());
        g_wlDraft = g_cfg.whitelist;
    }
    RECT rc{};
    GetClientRect(g_hwnd, &rc);
    BuildLayout((float)rc.right, (float)rc.bottom, L.dpi);
    PositionEdits();
    if (s == Sheet::System) QuerySysInfoAsync();
    if (g_sheetAnimating) SetTimer(g_hwnd, 3, 16, nullptr);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void CloseSheet() {
    if (g_sheet == Sheet::None) return;
    HideEdits();
    g_sheet = Sheet::None;
    g_sheetAnimating = false;
    g_sheetT = 1.0f;
    if (g_hwnd) KillTimer(g_hwnd, 3);
    RECT rc{};
    GetClientRect(g_hwnd, &rc);
    BuildLayout((float)rc.right, (float)rc.bottom, L.dpi);
    PositionEdits();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void Activate(CtlId id) {
    int iv = (int)id;
    if (iv >= ID_PROC_BASE && iv < ID_PROC_BASE + 8) {
        int row = g_procScroll + (iv - ID_PROC_BASE);
        if (row >= 0 && row < (int)g_procList.size()) {
            const ProcRow& r = g_procList[row];
            if (r.st == ProcState::Builtin) {
                g_toast = Tx(L"系统进程始终受保护", L"System processes are always protected");
                g_lastToastMs = GetTickCount64();
                KickAnim();
            } else {
                ToggleProtected(r.name);
            }
        }
        return;
    }
    if (iv >= ID_SET_COOL_BASE && iv < ID_SET_COOL_BASE + 4) {
        g_coolSel = iv - ID_SET_COOL_BASE; g_coolDirty = true; CommitSettings(); KickAnim(); return;
    }
    if (iv >= ID_SET_STRENGTH_BASE && iv < ID_SET_STRENGTH_BASE + 3) {
        g_strength = iv - ID_SET_STRENGTH_BASE; g_strengthDirty = true; CommitSettings(); KickAnim(); return;
    }
    if (iv >= ID_ADV_MINWS_BASE && iv < ID_ADV_MINWS_BASE + 3) {
        g_minWSSel = iv - ID_ADV_MINWS_BASE; g_minDirty = true; CommitSettings(); KickAnim(); return;
    }
    if (iv >= ID_ADV_IDLE_BASE && iv < ID_ADV_IDLE_BASE + 3) {
        g_idleSel = iv - ID_ADV_IDLE_BASE; g_idleDirty = true; CommitSettings(); KickAnim(); return;
    }
    switch (id) {
    case ID_BTN_CLEAN:
    case ID_RING_CLEAN:
        g_ringPulseAt = GetTickCount64();
        RequestManualClean(false);
        KickAnim();
        break;
    case ID_BTN_SETTINGS:
        if (g_sheet == Sheet::Settings) CloseSheet();
        else OpenSheet(Sheet::Settings);
        break;
    case ID_SHEET_CLOSE:
    case ID_SHEET_DISMISS:
        if (g_sheet == Sheet::Settings || g_sheet == Sheet::Advanced) CommitSettings();
        CloseSheet();
        break;
    case ID_HOME_PROCESS:
        g_procFromSettings = false;
        OpenSheet(Sheet::Processes);
        break;
    case ID_HOME_SYSTEM:
        OpenSheet(Sheet::System);
        break;
    case ID_SET_ADVANCED:
        CommitSettings();
        OpenSheet(Sheet::Advanced);
        break;
    case ID_SET_ABOUT:
        OpenSheet(Sheet::About);
        break;
    case ID_SET_PROTECTED:
        g_procFromSettings = true;
        OpenSheet(Sheet::Processes);
        break;
    case ID_ABOUT_REPO:
        OpenUrl(L"https://github.com/feiyu-labs/MemSweep"); break;
    case ID_ABOUT_ISSUES:
        OpenUrl(L"https://github.com/feiyu-labs/MemSweep/issues"); break;
    case ID_ABOUT_UPDATES:
        OpenUrl(L"https://github.com/feiyu-labs/MemSweep/releases/latest"); break;
    case ID_ADV_BACK:
        if (g_sheet == Sheet::Advanced) {
            CommitSettings();
            OpenSheet(Sheet::Settings);
        } else if (g_sheet == Sheet::Processes && g_procFromSettings) {
            OpenSheet(Sheet::Settings);
        } else {
            CloseSheet();
        }
        break;
    case ID_SYS_MORE:
        g_systemExpanded = !g_systemExpanded;
        BuildLayout((float)(L.w * L.dpi), (float)(L.h * L.dpi), L.dpi);
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    case ID_SYS_MC: SysAction(1); break;
    case ID_SYS_PC: SysAction(2); break;
    case ID_SYS_PF: SysAction(3); break;
    case ID_SYS_WSL: SysAction(4); break;
    case ID_SYS_EXPORT: SysAction(5); break;
    case ID_BTN_THEME: {
        // 快切：显式浅↔深；跟随系统时按当前生效模式取反
        if (g_themeMode == 0) g_themeMode = g_dark ? 1 : 2;
        else g_themeMode = (g_themeMode == 1) ? 2 : 1;
        g_cfg.theme = (g_themeMode == 1) ? L"light" : L"dark";
        SaveConfig();
        ApplyTheme();
        g_toast = (g_themeMode == 1) ? Tx(L"已切换浅色主题", L"Light theme on")
                                     : Tx(L"已切换深色主题", L"Dark theme on");
        g_lastToastMs = GetTickCount64();
        KickAnim();
        break;
    }
    case ID_BTN_LANG: {
        g_zh.store(!g_zh.load(std::memory_order_relaxed), std::memory_order_relaxed);
        g_cfg.lang = g_zh.load(std::memory_order_relaxed) ? L"zh" : L"en";
        SaveConfig();
        CreateFonts();
        RECT rc{}; GetClientRect(g_hwnd, &rc);
        BuildLayout((float)rc.right, (float)rc.bottom, L.dpi);
        PositionEdits();
        UpdateWhitelistCue();
        g_toast = Tx(L"已切换中文界面", L"Switched to English");
        g_lastToastMs = GetTickCount64();
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    }
    case ID_BTN_MIN: ShowWindow(g_hwnd, SW_MINIMIZE); break;
    case ID_BTN_CLOSE: {
        ShowWindow(g_hwnd, SW_HIDE);
        static bool hinted = false;
        if (!hinted) {
            hinted = true;
            Balloon(Tx(L"已最小化到托盘，右键托盘图标可退出", L"Minimized to tray; right-click the tray icon to exit"));
        }
        break;
    }
    case ID_TG_AUTO:
        s_auto = !s_auto;
        ToggleAnimStart(ID_TG_AUTO);
        CommitSettings();
        break;
    case ID_TG_HOTKEY: s_hotkey = !s_hotkey; ToggleAnimStart(ID_TG_HOTKEY); CommitSettings(); break;
    case ID_TG_ECO: s_eco = !s_eco; ToggleAnimStart(ID_TG_ECO); CommitSettings(); break;
    case ID_TG_SILENT: s_silent = !s_silent; ToggleAnimStart(ID_TG_SILENT); CommitSettings(); break;
    case ID_TG_IDLE_DEEP: s_idleDeep = !s_idleDeep; ToggleAnimStart(ID_TG_IDLE_DEEP); CommitSettings(); break;
    case ID_TG_AUTOSTART: {
        bool want = !s_autostart;
        s_autostart = want;
        g_cfg.autostart = want;
        SaveConfig();
        ToggleAnimStart(ID_TG_AUTOSTART);
        SetAutostartAsync(want);
        g_savedAt = GetTickCount64();
        break;
    }
    default: break;
    }
    KickAnim();
}

// 菜单 / 托盘
// ---------------------------------------------------------------------------
static UINT g_msgTaskbarCreated = 0;

static DWORD WINAPI CombineThread(LPVOID) {
    InterlockedExchange(&g_cleanPhase, 2);
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_CLEANING, 2, 0);
    CombineResult r;
    CombinePages(r);
    unsigned long long shown = Grow(r.freeAfter, r.freeBefore);
    unsigned long long availShown = Grow(r.availAfter, r.availBefore);
    if (availShown > shown) shown = availShown;
    wchar_t msg[320];
    if (r.ok && r.aborted)
        swprintf(msg, 320, Tx(L"深度清理已中止，已释放约 %s（用时 %.0f 秒）",
                              L"Deep clean stopped, freed about %s (%.0f s)"),
                 FormatBytes(shown).c_str(), (double)r.durationMs / 1000.0);
    else if (r.ok)
        swprintf(msg, 320, Tx(L"深度清理完成，已释放约 %s（用时 %.0f 秒）",
                              L"Deep clean done, freed about %s (%.0f s)"),
                 FormatBytes(shown).c_str(), (double)r.durationMs / 1000.0);
    else
        swprintf(msg, 320, Tx(L"深度清理未能执行，详见日志",
                              L"Deep clean failed; see log"));
    if (r.ok) {
        g_lastFreeGain = Grow(r.freeAfter, r.freeBefore);
        g_lastAvailGain = Grow(r.availAfter, r.availBefore);
        g_lastCleanTickMs = GetTickCount64();
    }
    InterlockedExchange(&g_cleanPhase, 0);
    if (g_hwnd) {
        PostMessageW(g_hwnd, WM_APP_BALLOON, 0, (LPARAM)new std::wstring(msg));
        PostMessageW(g_hwnd, WM_APP_CLEANING, 0, 0);
        PostMessageW(g_hwnd, WM_APP_STATS, 0, 0);
    }
    InterlockedExchange(&g_cleaning, 0);
    return 0;
}
static void StartCombine(bool needConfirm) {
    if (g_cleaning != 0) {
        g_toast = Tx(L"正在清理，请稍候", L"Cleaning, please wait");
        g_lastToastMs = GetTickCount64();
        KickAnim();
        return;
    }
    if (needConfirm) {
        if (MessageBoxW(g_hwnd,
                        Tx(L"深度清理会合并重复的内存页，可能需要 10~60 秒，期间电脑会稍慢。\n\n继续？",
                           L"Deep clean merges duplicate memory pages. It can take 10-60s and the PC may slow down.\n\nContinue?"),
                        L"MemSweep", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
    }
    if (InterlockedCompareExchange(&g_cleaning, 1, 0) != 0) return;
    HANDLE h = CreateThread(nullptr, 0, CombineThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else InterlockedExchange(&g_cleaning, 0);
}
static void ShowProcMenu(int row) {
    if (row < 0 || row >= (int)g_procList.size()) return;
    const ProcRow& r = g_procList[row];
    const bool can = (r.pid != 0 && r.st == ProcState::Normal);
    wchar_t trimLabel[160];
    swprintf(trimLabel, 160, Tx(L"释放此进程内存（%s）", L"Free this app's memory (%s)"),
             FormatBytes(r.ws).c_str());
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (can ? 0 : MF_GRAYED), 1, trimLabel);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 3, Tx(L"取消", L"Cancel"));
    SetForegroundWindow(g_hwnd);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                  GET_X_LPARAM(GetMessagePos()), GET_Y_LPARAM(GetMessagePos()),
                                  0, g_hwnd, nullptr);
    DestroyMenu(m);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    if (cmd == 1) {
        bool ok = TrimPid(r.pid);
        g_toast = ok ? (Tx(L"已释放 ", L"Freed ") + r.display + L" · " + FormatBytes(r.ws))
                     : (Tx(L"释放失败：", L"Couldn't free: ") + r.display);
        g_lastToastMs = GetTickCount64();
        KickAnim();
    }
}

static void ShowTrayMenu(int x, int y) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (g_cleaning != 0 ? MF_DISABLED : 0), 1, Tx(L"立即清理", L"Clean now"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g_cfg.autoGuard ? MF_CHECKED : 0), 2, Tx(L"自动清理", L"Auto clean"));
    AppendMenuW(m, MF_STRING, 3, Tx(L"打开面板", L"Open panel"));
    AppendMenuW(m, MF_STRING | (s_autostartInstalled ? MF_CHECKED : 0), 4, Tx(L"开机自启", L"Start with Windows"));
    AppendMenuW(m, MF_STRING, 5, Tx(L"查看日志", L"Open log"));
    AppendMenuW(m, MF_STRING, 7, Tx(L"系统状态…", L"System status…"));
    AppendMenuW(m, MF_STRING | (AutoPaused() ? MF_CHECKED : 0), 8,
                AutoPaused() ? Tx(L"恢复自动清理", L"Resume auto clean")
                             : Tx(L"暂停自动清理 1 小时", L"Pause auto clean 1h"));
    AppendMenuW(m, MF_STRING, 9, Tx(L"深度清理（合并内存页）…", L"Deep clean (merge pages)…"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 10, Tx(L"关于 MemSweep…", L"About MemSweep…"));
    AppendMenuW(m, MF_STRING, 6, Tx(L"退出", L"Exit"));
    SetMenuDefaultItem(m, 1, FALSE);
    SetForegroundWindow(g_hwnd);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    switch (cmd) {
    case 1: RequestManualClean(false); break;
    case 2: g_cfg.autoGuard = !g_cfg.autoGuard; s_auto = g_cfg.autoGuard; SaveConfig();
            ToggleAnimStart(ID_TG_AUTO); InvalidateRect(g_hwnd, nullptr, FALSE); break;
    case 3: BringMainToFront(); break;
    case 4: { bool want = !s_autostartInstalled; s_autostart = want; g_cfg.autostart = want; SaveConfig(); SetAutostartAsync(want); InvalidateRect(g_hwnd, nullptr, FALSE); break; }
    case 5: ShellExecuteW(nullptr, L"open", LogPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case 6: PostMessageW(g_hwnd, WM_APP_EXIT, 0, 0); break;
    case 7: ShowSystemCheck(); break;
    case 10: BringMainToFront(); OpenSheet(Sheet::About); break;
    case 9: StartCombine(true); break;
    case 8: PauseAutoGuard(AutoPaused() ? 0 : 60);
            g_toast = AutoPaused() ? Tx(L"自动清理已暂停 1 小时", L"Auto clean paused 1h")
                                   : Tx(L"自动清理已恢复", L"Auto clean resumed");
            g_lastToastMs = GetTickCount64();
            KickAnim(); break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// 窗口过程
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        g_inst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
        ApplyTheme();   // 创建主题画刷、DWM 深色、菜单主题
        ImmAssociateContextEx(hwnd, nullptr, 0);   // 本窗口不需要中文输入，避免 IME/TSF 注入
        // 编辑框
        g_edFont = CreateFontW(-MulDiv(10, (int)(96 * L.dpi), 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, FontFamily().c_str());
        auto mkEdit = [&](DWORD style) {
            HWND h = CreateWindowExW(0, L"Edit", L"",
                                     WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | style,
                                     0, 0, 10, 10, hwnd, nullptr, g_inst, nullptr);
            SendMessageW(h, WM_SETFONT, (WPARAM)g_edFont, TRUE);
            ImmAssociateContextEx(h, nullptr, 0);
            return h;
        };
        g_edWL = mkEdit(0);
        UpdateWhitelistCue();
        g_edOldProc = (WNDPROC)SetWindowLongPtrW(g_edWL, GWLP_WNDPROC, (LONG_PTR)EditProc);

        SyncFromConfig();
        RECT rc;
        GetClientRect(hwnd, &rc);
        BuildLayout((float)rc.right, (float)rc.bottom, L.dpi);
        PositionEdits();

        SetTimer(hwnd, 1, 1000, nullptr);
        SetTimer(hwnd, 4, 150, nullptr);   // 环境动效心跳（6.7fps，可见主屏才工作）
        SetTimer(hwnd, 2, 33, nullptr);
        TrayUpdate(true);
        UpdateStats(true);
        return 0;
    }
    case WM_COMMAND: {
        if (HIWORD(wp) == EN_CHANGE && (HWND)lp == g_edWL) {
            int len = GetWindowTextLengthW(g_edWL);
            if (len <= 0) g_wlDraft.clear();
            else {
                wchar_t b[512] = {};
                GetWindowTextW(g_edWL, b, 512);
                if (b[0]) g_wlDraft = b;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right > 0 ? rc.right : 1;
        int h = rc.bottom > 0 ? rc.bottom : 1;
        if (!g_back || g_backW != w || g_backH != h) {
            delete g_backG; g_backG = nullptr;
            delete g_back; g_back = nullptr;
            g_back = new Bitmap(w, h, PixelFormat32bppPARGB);
            g_backG = new Graphics(g_back);
            g_backW = w; g_backH = h;
        }
        if (g_backG) {
            // 部分重绘：更新区小于客户区时，仅重绘该区域（背景缓冲保留上一帧完整内容）。
            RECT ru = ps.rcPaint;
            static bool forceFull = [] { wchar_t e[8] = {}; return GetEnvironmentVariableW(L"MEMSweep_FULLPAINT", e, 8) != 0; }();
            bool full = forceFull || (ru.left <= 0 && ru.top <= 0 && ru.right >= w && ru.bottom >= h);
            RectF clip{};
            const RectF* clipPtr = nullptr;
            if (!full) {
                float d = L.dpi;
                clip = RectF((float)ru.left / d, (float)ru.top / d,
                             (float)(ru.right - ru.left) / d,
                             (float)(ru.bottom - ru.top) / d);
                clipPtr = &clip;
            }
            g_backG->ResetTransform();
            g_backG->ResetClip();
            DrawAll(*g_backG, clipPtr);
            Graphics screen(hdc);   // BeginPaint 已按更新区裁剪
            screen.SetCompositingMode(CompositingModeSourceCopy);
            screen.DrawImage(g_back,
                             Rect(ru.left, ru.top, ru.right - ru.left, ru.bottom - ru.top),
                             ru.left, ru.top, ru.right - ru.left, ru.bottom - ru.top,
                             UnitPixel);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SHOWWINDOW:
        if (!wp) ReleaseBackBuffer();   // 托盘/最小化后不再占用 1–3MB 双缓冲
        else InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_PRINTCLIENT: {
        HDC dc = (HDC)wp;
        Graphics g(dc);
        DrawAll(g, nullptr);
        return 0;
    }
    case WM_SIZE: {
        if (wp == SIZE_MINIMIZED) { ReleaseBackBuffer(); return 0; }
        RECT rc;
        GetClientRect(hwnd, &rc);
        float dpi = L.dpi;
        BuildLayout((float)rc.right, (float)rc.bottom, dpi);
        PositionEdits();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_DPICHANGED: {
        float dpi = (float)HIWORD(wp) / 96.0f;
        L.dpi = dpi;
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        RECT rc;
        GetClientRect(hwnd, &rc);
        BuildLayout((float)rc.right, (float)rc.bottom, dpi);
        CreateFonts();
        if (g_edFont) DeleteObject(g_edFont);
        g_edFont = CreateFontW(-MulDiv(10, (int)(96 * dpi), 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, FontFamily().c_str());
        for (HWND e : { g_edWL })
            if (e) SendMessageW(e, WM_SETFONT, (WPARAM)g_edFont, TRUE);
        PositionEdits();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (g_sheet == Sheet::Processes) {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            g_procScroll += (delta > 0) ? -3 : 3;
            int total = (int)g_procList.size();
            int maxScroll = std::max(0, total - 8);
            if (g_procScroll < 0) g_procScroll = 0;
            if (g_procScroll > maxScroll) g_procScroll = maxScroll;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_MOUSEMOVE: {
        float x = (float)GET_X_LPARAM(lp) / L.dpi;
        float y = (float)GET_Y_LPARAM(lp) / L.dpi;
        if (g_draggingSlider && L.setThresh.Width > 0) {
            float t = ClampF((x - L.setThresh.X) / L.setThresh.Width, 0, 1);
            g_sliderValue = 60 + t * (95 - 60);
            s_threshold = (int)(g_sliderValue + 0.5f);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        int hv = (int)HitTest(x, y);
        int oldHv = g_hover;
        if (hv != oldHv) {
            g_hover = hv;
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            // 光晕缓动需要动画帧
            if ((CtlId)hv == ID_RING_CLEAN || (CtlId)oldHv == ID_RING_CLEAN) KickAnim();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hover) { g_hover = 0; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_SETCURSOR: {
        if (LOWORD(lp) == HTCLIENT) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            CtlKind k = K_NONE;
            HitTest((float)p.x / L.dpi, (float)p.y / L.dpi, &k);
            if (k == K_BUTTON || k == K_TOGGLE || k == K_TITLEBTN || k == K_SEG) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            if (k == K_SLIDER) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        float x = (float)GET_X_LPARAM(lp) / L.dpi;
        float y = (float)GET_Y_LPARAM(lp) / L.dpi;
        CtlKind k = K_NONE;
        CtlId id = HitTest(x, y, &k);
        g_pressed = (int)id;
        if (k == K_SLIDER && L.setThresh.Width > 0) {
            g_draggingSlider = true;
            SetCapture(hwnd);
            float t = ClampF((x - L.setThresh.X) / L.setThresh.Width, 0, 1);
            g_sliderValue = 60 + t * (95 - 60);
            s_threshold = (int)(g_sliderValue + 0.5f);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_RBUTTONUP: {
        float rx = (float)GET_X_LPARAM(lp) / L.dpi;
        float ry = (float)GET_Y_LPARAM(lp) / L.dpi;
        CtlId rid = HitTest(rx, ry);
        if (g_sheet == Sheet::Processes && rid >= ID_PROC_BASE && rid < ID_PROC_BASE + 8) {
            ShowProcMenu(g_procScroll + (int)rid - ID_PROC_BASE);
            return 0;
        }
        break;
    }
    case WM_LBUTTONUP: {
        float x = (float)GET_X_LPARAM(lp) / L.dpi;
        float y = (float)GET_Y_LPARAM(lp) / L.dpi;
        if (g_draggingSlider) {
            g_draggingSlider = false;
            ReleaseCapture();
            CommitSettings();
        }
        CtlId id = HitTest(x, y);
        CtlId pressed = (CtlId)g_pressed;
        g_pressed = 0;
        if (id != ID_NONE && id == pressed) Activate(id);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) {
            if (g_sheet == Sheet::System) QuerySysInfo(false);
            UpdateStats(false);
            if (g_cleanDoneAt && GetTickCount64() - g_cleanDoneAt >= 6000) {
                g_cleanDoneAt = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            // 隐藏超过 20 秒释放双缓冲，降低托盘常驻内存
            if (!IsWindowVisible(hwnd)) {
                if (!g_hiddenAt) g_hiddenAt = GetTickCount64();
                else if (g_back && GetTickCount64() - g_hiddenAt > 20000) ReleaseBackBuffer();
            } else {
                g_hiddenAt = 0;
            }
        }
        else if (wp == 2) {
            if (StepAnimations()) InvalidateRect(hwnd, nullptr, FALSE);
            else KillTimer(hwnd, 2);
        }
        else if (wp == 4) {
            // 环境动效（10fps 预算）：仅 可见 + 主屏 + 非清理；隐藏/Sheet 内 0 开销
            if (!IsWindowVisible(hwnd) || g_sheet != Sheet::None ||
                g_cleaning != 0 || !AnimationsEnabled()) return 0;
            ULONGLONG nowA = GetTickCount64();
            float dtA = g_lastAmbientMs ? (float)(nowA - g_lastAmbientMs) / 1000.0f : 0.1f;
            if (dtA > 0.5f) dtA = 0.5f;
            g_lastAmbientMs = nowA;
            g_ambientPhase += dtA / 7.0f;   // 7s 一圈
            if (g_ambientPhase > 1.0f) g_ambientPhase -= 1.0f;
            // 只失效仪表区（含高光/光晕余量），配合 WM_PAINT 部分重绘
            RECT gr{ (LONG)((L.gaugeCenter.X - 14) * L.dpi),
                     (LONG)((L.gaugeCenter.Y - 14) * L.dpi),
                     (LONG)((L.gaugeCenter.GetRight() + 14) * L.dpi),
                     (LONG)((L.gaugeCenter.GetBottom() + 14) * L.dpi) };
            InvalidateRect(hwnd, &gr, FALSE);
        }
        else if (wp == 3) {
            if (!g_sheetAnimating) {
                KillTimer(hwnd, 3);
            } else {
                float t = (float)(GetTickCount64() - g_sheetStart) / 200.0f;
                g_sheetT = ClampF(t, 0.0f, 1.0f);
                if (g_sheetT >= 1.0f) FinishSheetAnim();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_APP_SYSINFO:
        if (g_sheet == Sheet::System) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_APP_AUTOSTART: {
        bool ok = wp != 0, want = lp != 0;
        if (ok) {
            s_autostart = want;
            s_autostartInstalled = want;
            g_cfg.autostart = want;
            SaveConfig();
                g_toast = want ? Tx(L"开机自启已开启", L"Start with Windows enabled")
                           : Tx(L"开机自启已关闭", L"Start with Windows disabled");
        } else {
            s_autostart = s_autostartInstalled;
                g_toast = Tx(L"开机自启设置失败，请查看日志", L"Couldn't update startup; see log");
        }
        g_lastToastMs = GetTickCount64();
        if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
        KickAnim();
        return 0;
    }
    case WM_APP_STATS: UpdateStats(false); return 0;
    case WM_APP_CLEANING:
        if (wp == 0) {
            g_cleanDoneAt = GetTickCount64();
            g_ringPulseAt = g_cleanDoneAt;
        }
        KickAnim();
        TrayUpdate(true);
        return 0;
    case WM_APP_CLEAN: RequestManualClean(false); return 0;
    case WM_APP_COMBINE: StartCombine(false); return 0;
    case WM_APP_PAUSE:
        PauseAutoGuard((int)wp);
        g_toast = (wp > 0) ? Tx(L"自动清理已暂停 1 小时", L"Auto clean paused 1h")
                           : Tx(L"自动清理已恢复", L"Auto clean resumed");
        g_lastToastMs = GetTickCount64();
        KickAnim();
        return 0;
    case WM_APP_EXIT:
        if (g_sheet == Sheet::Settings || g_sheet == Sheet::Advanced) CommitSettings();
        g_quit = 1;
        if (g_monEvent) SetEvent(g_monEvent);
        DestroyWindow(hwnd);
        return 0;
    case WM_APP_SHOW: BringMainToFront(); return 0;
    case WM_APP_BALLOON: {
        std::wstring* s = (std::wstring*)lp;
        if (s) { Balloon(*s); delete s; }
        return 0;
    }
    case WM_APP_TRAY: {
        int ev = LOWORD(lp);
        if (ev == WM_CONTEXTMENU) {
            int x = GET_X_LPARAM(lp), yy = GET_Y_LPARAM(lp);
            ShowTrayMenu(x, yy);
        } else if (ev == WM_LBUTTONUP || ev == NIN_SELECT || ev == WM_LBUTTONDBLCLK) {
            static ULONGLONG lastShow = 0;
            ULONGLONG now = GetTickCount64();
            if (now - lastShow > 250) {   // 双击/重复事件去抖，且不再切换隐藏
                lastShow = now;
                BringMainToFront();
            }
        } else if (ev == WM_MBUTTONUP) {
            RequestManualClean(false);
        } else if (ev == NIN_BALLOONUSERCLICK) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        }
        return 0;
    }
    case WM_HOTKEY:
        if (wp == HOTKEY_ID) RequestManualClean(false);
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, g_dark ? RGB(22, 24, 28) : RGB(17, 24, 39));
        SetBkColor(hdc, g_dark ? RGB(39, 45, 56) : RGB(241, 245, 249));
        return (LRESULT)g_edBrush;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, g_dark ? RGB(235, 238, 243) : RGB(17, 24, 39));
        SetBkColor(hdc, g_dark ? RGB(39, 45, 56) : RGB(241, 245, 249));
        return (LRESULT)g_edBrush;
    }
    case WM_NCCALCSIZE:
        return 0;
    case WM_NCLBUTTONDBLCLK:
        return 0;   // 禁止双击标题栏最大化
    case WM_NCACTIVATE:
        return TRUE;
    case WM_NCHITTEST: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &p);
        float x = (float)p.x / L.dpi, y = (float)p.y / L.dpi;
        if (x < 0 || y < 0 || x > L.w || y > L.h) return HTCLIENT;
        if (g_sheet != Sheet::None) return HTCLIENT;
        if (InRect(L.btnSettings, x, y) || InRect(L.btnMin, x, y) ||
            InRect(L.btnClose, x, y) || InRect(L.btnTheme, x, y) ||
            InRect(L.btnLang, x, y)) return HTCLIENT;
        if (y < L.titlebar.Height) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_KEYDOWN: {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (wp == VK_ESCAPE) {
            if (g_sheet != Sheet::None) {
                if (g_sheet == Sheet::Settings || g_sheet == Sheet::Advanced) CommitSettings();
                CloseSheet();
            } else {
                g_hiddenAt = GetTickCount64();
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;
        }
        if (ctrl && (wp == VK_RETURN || wp == VK_SPACE)) { RequestManualClean(false); return 0; }
        if (ctrl && wp == 'S') { CommitSettings(); return 0; }
        break;
    }
    case WM_CLOSE:
        g_hiddenAt = GetTickCount64();
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_ENDSESSION:
        if (wp) {
            if (g_sheet == Sheet::Settings || g_sheet == Sheet::Advanced) CommitSettings();
            else SaveConfig();
            ExitProcess(0);
        }
        return 0;
    case WM_DESTROY:
        g_quit = 1;
        if (g_monEvent) SetEvent(g_monEvent);
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        KillTimer(hwnd, 3);
        TrayRemove();
        UnregisterHotKey(hwnd, HOTKEY_ID);
        PostQuitMessage(0);
        return 0;
    default:
        if (msg == g_msgTaskbarCreated) {
            g_trayOn = false;
            TrayUpdate(true);
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// 系统检查窗口
// ---------------------------------------------------------------------------

static std::wstring ReadTextFileW(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    std::string raw((size_t)(sz.QuadPart > 0 ? sz.QuadPart : 0), 0);
    DWORD rd = 0;
    if (!raw.empty()) ReadFile(h, raw.data(), (DWORD)raw.size(), &rd, nullptr);
    CloseHandle(h);
    raw.resize(rd);
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF) raw = raw.substr(3);
    int l = MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)raw.size(), nullptr, 0);
    std::wstring w(l, 0);
    if (l) MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)raw.size(), w.data(), l);
    return w;
}
static void RunPS(const std::wstring& cmd) {
    std::wstring args = L"-NoProfile -ExecutionPolicy Bypass -Command \"" + cmd + L"\"";
    RunHidden(L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", args);
}
static bool GetRegStr(const wchar_t* sub, const wchar_t* name, std::wstring& out) {
    wchar_t buf[1024] = {};
    DWORD sz = sizeof(buf), type = 0;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, sub, name, RRF_RT_REG_SZ | RRF_RT_REG_MULTI_SZ,
                     &type, buf, &sz) != ERROR_SUCCESS) return false;
    out = buf;
    return true;
}
static bool GetRegDword(const wchar_t* sub, const wchar_t* name, DWORD& v) {
    DWORD sz = sizeof(v);
    return RegGetValueW(HKEY_LOCAL_MACHINE, sub, name, RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS;
}
static bool RegKeyExists(HKEY root, const wchar_t* sub) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &h) != ERROR_SUCCESS) return false;
    RegCloseKey(h);
    return true;
}
static void QuerySysInfo(bool deep) {
    // 深层查询可能耗时数秒，先在本地快照上完成，最后一次性换入，避免 UI 读到半更新的 wstring。
    SysState n;
    if (deep) {
        {
            AcquireSRWLockShared(&g_sysLock);
            n = g_sys;
            ReleaseSRWLockShared(&g_sysLock);
        }
        // SysMain
        n.sysmainRunning = false;
        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (scm) {
            SC_HANDLE svc = OpenServiceW(scm, L"SysMain", SERVICE_QUERY_STATUS);
            if (svc) {
                SERVICE_STATUS st{};
                if (QueryServiceStatus(svc, &st))
                    n.sysmainRunning = (st.dwCurrentState == SERVICE_RUNNING);
                CloseServiceHandle(svc);
            }
            CloseServiceHandle(scm);
        }
        // 快速启动
        DWORD hb = 0;
        n.fastStartupOn = GetRegDword(L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power",
                                     L"HiberbootEnabled", hb) && hb != 0;
        // 页面文件设置
        std::wstring pf;
        if (GetRegStr(L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
                      L"PagingFiles", pf)) {
            n.pagingFiles = pf;
            int a = 0, b = 0;
            wchar_t path[512] = {};
            if (swscanf(pf.c_str(), L"%511[^ ] %d %d", path, &a, &b) >= 1) {
                n.pfPath = path;
                n.pfInitMB = a;
                n.pfMaxMB = b;
            }
        }
        // MMAgent / WSL / PageFileUsage → PowerShell 写入临时文件
        std::wstring outFile = ConfigDir() + L"\\sysinfo.txt";
        DeleteFileW(outFile.c_str());
        std::wstring script =
            L"& { $m=Get-MMAgent; 'MC='+$m.MemoryCompression; 'PC='+$m.PageCombining; "
            L"'PL='+$m.ApplicationPreLaunch; $u=Get-CimInstance Win32_PageFileUsage; "
            L"if($u){'PFALLOC='+$u.AllocatedBaseSize; 'PFUSE='+$u.CurrentUsage; 'PFPEAK='+$u.PeakUsage} "
            L"} | Out-File -Encoding utf8 '" + outFile + L"'";
        RunPS(script);
        std::wstring txt = ReadTextFileW(outFile);
        auto has = [&](const wchar_t* k, const wchar_t* v) {
            std::wstring needle = std::wstring(k) + L"=" + v;
            return txt.find(needle) != std::wstring::npos;
        };
        auto val = [&](const wchar_t* k) -> std::wstring {
            std::wstring key = std::wstring(k) + L"=";
            size_t p = txt.find(key);
            if (p == std::wstring::npos) return L"";
            size_t e = txt.find_first_of(L"\r\n", p);
            return TrimW(txt.substr(p + key.size(), e == std::wstring::npos ? std::wstring::npos : e - p - key.size()));
        };
        // 只有拿到非空布尔值才算查询成功；Get-MMAgent 在未提权时会失败，
        // 不能把空输出误判成“功能已关闭”并显示误导性的启用按钮。
        const std::wstring mc = val(L"MC"), pc = val(L"PC");
        n.mmOk = !mc.empty() && !pc.empty();
        n.compression = mc == L"True";
        n.pageCombining = pc == L"True";
        n.prelaunch = has(L"PL", L"True");
        n.wslEnabled = RegKeyExists(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\LxssManager");
        std::wstring a = val(L"PFALLOC"), u = val(L"PFUSE"), pk = val(L"PFPEAK");
        if (!a.empty()) n.pfAllocMB = _wtoi64(a.c_str());
        if (!u.empty()) n.pfUseMB = _wtoi64(u.c_str());
        if (!pk.empty()) n.pfPeakMB = _wtoi64(pk.c_str());
        n.loaded = true;
    }

    // 实时指标
    EnterCriticalSection(&g_lock);
    Metrics m = g_metrics;
    LeaveCriticalSection(&g_lock);
    auto applyMetrics = [&](SysState& s) {
        s.compOk = m.compressionOk;
        s.compBytes = m.compressedBytes;
        s.compRatio = m.compressionRatio;
        s.nonpaged = m.nonpagedBytes;
        s.paged = m.pagedBytes;
        s.commitPct = m.commitPct;
        s.commitUsed = m.commitUsed;
        s.commitLimit = m.commitLimit;
        if (s.pfAllocMB == 0 && m.commitLimit > m.totalPhys)
            s.pfAllocMB = (m.commitLimit - m.totalPhys) / MB;
    };
    if (deep) {
        applyMetrics(n);
        AcquireSRWLockExclusive(&g_sysLock);
        g_sys = std::move(n);
        ReleaseSRWLockExclusive(&g_sysLock);
    } else {
        AcquireSRWLockExclusive(&g_sysLock);
        applyMetrics(g_sys);
        ReleaseSRWLockExclusive(&g_sysLock);
    }
}

static std::wstring OnOff(bool v) { return v ? Tx(L"已开启", L"on") : Tx(L"已关闭", L"off"); }

static void DrawActionButton(Graphics& g, const RectF& r, const std::wstring& text,
                             bool enabled, bool primary, int id) {
    bool hover = enabled && g_hover == id;
    if (primary && enabled) {
        FillRoundGrad(g, r, 9, hover ? C(255, 74, 222, 168) : T.accent,
                      hover ? C(255, 22, 199, 140) : T.accent2);
        TextAt(g, text, r, f11b, T.btnText, StringAlignmentCenter);
    } else {
        FillRound(g, r, 9, enabled ? (hover ? T.cardTop : T.card) : T.trackFill);
        StrokeRound(g, r, 9, T.border, 1);
        TextAt(g, text, r, f11, enabled ? (hover ? T.text : T.text2) : T.text3,
               StringAlignmentCenter);
    }
}

static void DrawSysRow(Graphics& g, const RectF& r, bool ok, const std::wstring& label,
                       const std::wstring& value, bool warn) {
    SolidBrush dot(warn ? T.warn : (ok ? T.accent : T.text3));
    g.FillEllipse(&dot, RectF(r.X + 2, r.Y + 12, 7, 7));
    TextAt(g, label, RectF(r.X + 16, r.Y, r.Width - 120, r.Height), f12, T.text2);
    TextAt(g, value, RectF(r.X + 16, r.Y, r.Width - 16, r.Height), f12b,
           warn ? T.warn : T.text, StringAlignmentFar);
}

static void DrawSystemSheet(Graphics& g) {
    DrawSheetFrame(g, L"系统状态", L"System status", false);
    AcquireSRWLockShared(&g_sysLock);
    int warnings = 0;
    bool know = g_sys.mmOk;
    if (know && !g_sys.compression) warnings++;
    if (know && !g_sys.pageCombining) warnings++;
    if (!g_sys.sysmainRunning) warnings++;
    if (!(g_sys.pfInitMB >= 24576 && g_sys.pfMaxMB >= 65536)) warnings++;
    if (g_sys.commitPct > 90.0) warnings++;
    if (g_sys.nonpaged > 1024ULL * MB) warnings++;

    std::wstring summary;
    Color sc = T.accent;
    if (g_sysLoading) { summary = Tx(L"正在读取系统状态…", L"reading system state…"); sc = T.text3; }
    else if (!know) { summary = Tx(L"需要管理员权限才能读取系统机制", L"admin rights required to read system mechanisms"); sc = T.warn; }
    else if (warnings == 0) { summary = Tx(L"系统机制正常", L"System mechanisms look healthy"); }
    else { wchar_t b[96]; swprintf(b, 96, Tx(L"%d 项需关注", L"%d items need attention"), warnings); summary = b; sc = T.warn; }
    TextAt(g, summary, RectF(L.sysSummary.X, L.sysSummary.Y, L.sysSummary.Width, 20), f15b, sc);
    const wchar_t* summarySub = Tx(L"内存压缩 · 页面合并 · 已提交", L"compression · page merging · commit");
    if (know && g_sys.commitPct > 90.0)
        summarySub = Tx(L"已提交接近上限，建议关闭大型应用",
                        L"commit near limit; close heavy apps");
    else if (know && g_sys.nonpaged > 1024ULL * MB)
        summarySub = g_systemExpanded
            ? Tx(L"内核内存偏高", L"kernel memory is high")
            : Tx(L"内核内存偏高，展开查看明细", L"kernel memory is high; expand for details");
    TextAt(g, summarySub, RectF(L.sysSummary.X, L.sysSummary.Y + 22, L.sysSummary.Width, 16), f11, T.text3);

    auto row = [&](int i, bool ok, const wchar_t* zh, const wchar_t* en, const std::wstring& v, bool warn) {
        DrawSysRow(g, L.sysRows[i], ok, Tx(zh, en), v, warn);
    };
    wchar_t v[128];
    if (g_sys.compOk) {
        swprintf(v, 128, Tx(L"已开启 · %s · %.2f×", L"on · %s · %.2fx"),
                 FormatBytes(g_sys.compBytes).c_str(), g_sys.compRatio);
    } else {
        wcscpy(v, g_sys.mmOk ? (g_sys.compression ? Tx(L"已开启", L"on") : Tx(L"已关闭", L"off"))
                             : Tx(L"未知", L"unknown"));
    }
    row(0, g_sys.mmOk && g_sys.compression, L"内存压缩", L"Compression", v, g_sys.mmOk && !g_sys.compression);
    row(1, g_sys.mmOk && g_sys.pageCombining, L"页面合并", L"Page combining",
        g_sys.mmOk ? OnOff(g_sys.pageCombining) : Tx(L"未知", L"unknown"),
        g_sys.mmOk && !g_sys.pageCombining);
    row(2, g_sys.sysmainRunning, L"SysMain", L"SysMain",
        g_sys.sysmainRunning ? Tx(L"运行中", L"running") : Tx(L"已停止", L"stopped"),
        !g_sys.sysmainRunning);
    swprintf(v, 128, Tx(L"%d–%d MB · 已分配 %llu MB", L"%d–%d MB · allocated %llu MB"),
             g_sys.pfInitMB, g_sys.pfMaxMB, g_sys.pfAllocMB);
    bool pfWarn = !(g_sys.pfInitMB >= 24576 && g_sys.pfMaxMB >= 65536);
    row(3, !pfWarn, L"页面文件", L"Pagefile", v, pfWarn);
    swprintf(v, 128, Tx(L"%s / %s · %.0f%%", L"%s / %s · %.0f%%"),
             FormatBytes(g_sys.commitUsed).c_str(), FormatBytes(g_sys.commitLimit).c_str(), g_sys.commitPct);
    row(4, g_sys.commitPct <= 90.0, L"已提交", L"Commit", v, g_sys.commitPct > 90.0);

    if (g_systemExpanded) {
        row(5, g_sys.mmOk && g_sys.prelaunch, L"应用预加载", L"App prefetch", OnOff(g_sys.prelaunch), false);
        row(6, !g_sys.fastStartupOn, L"快速启动", L"Fast startup",
            g_sys.fastStartupOn ? Tx(L"已开启", L"on") : Tx(L"已关闭", L"off"), g_sys.fastStartupOn);
        swprintf(v, 128, Tx(L"非分页 %s · 分页 %s", L"nonpaged %s · paged %s"),
                 FormatBytes(g_sys.nonpaged).c_str(), FormatBytes(g_sys.paged).c_str());
        row(7, g_sys.nonpaged <= 1024ULL * MB, L"内核内存", L"Kernel memory", v, g_sys.nonpaged > 1024ULL * MB);
        row(8, true, L"WSL 内存回收", L"WSL memory reclaim",
            g_sys.wslEnabled ? Tx(L"可配置", L"available") : Tx(L"未启用", L"not enabled"), false);
    }

    TextAt(g, g_systemExpanded ? Tx(L"收起 ‹", L"less ‹") : Tx(L"更多状态 ›", L"more ›"),
           L.sysMore, f12, g_hover == ID_SYS_MORE ? T.accent : T.text2);

    const bool canMc = g_sys.mmOk && !g_sys.compression;
    const bool canPc = g_sys.mmOk && !g_sys.pageCombining;
    const bool canPf = (g_sys.pfInitMB != 24576 || g_sys.pfMaxMB != 65536);
    const bool canWsl = g_sys.wslEnabled;
    ReleaseSRWLockShared(&g_sysLock);

    const wchar_t* mcLabel = !g_sys.mmOk ? Tx(L"需要管理员", L"Admin only")
                           : canMc ? Tx(L"启用压缩", L"Compression")
                                   : Tx(L"压缩已开启", L"Compression on");
    const wchar_t* pcLabel = !g_sys.mmOk ? Tx(L"需要管理员", L"Admin only")
                           : canPc ? Tx(L"启用合并", L"Combining")
                                   : Tx(L"合并已开启", L"Combining on");
    DrawActionButton(g, L.sysBtn[0], mcLabel, canMc, canMc, ID_SYS_MC);
    DrawActionButton(g, L.sysBtn[1], pcLabel, canPc, canPc, ID_SYS_PC);
    DrawActionButton(g, L.sysBtn[2], Tx(L"页面文件", L"Pagefile"), canPf, canPf, ID_SYS_PF);
    DrawActionButton(g, L.sysBtn[3], canWsl ? Tx(L"WSL 回收", L"WSL reclaim")
                                           : Tx(L"WSL 未启用", L"WSL off"),
                     canWsl, false, ID_SYS_WSL);
    DrawActionButton(g, L.sysBtn[4], Tx(L"导出诊断", L"Diagnostics"), true, false, ID_SYS_EXPORT);
}

static bool WriteUtf8File(const std::wstring& path, const std::wstring& text) {
    int l = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string raw(l, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), raw.data(), l, nullptr, nullptr);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    static const BYTE bom[3] = { 0xEF, 0xBB, 0xBF };
    WriteFile(h, bom, 3, &wr, nullptr);
    WriteFile(h, raw.data(), (DWORD)raw.size(), &wr, nullptr);
    CloseHandle(h);
    return true;
}
static std::wstring DesktopDir() {
    wchar_t p[MAX_PATH] = {};
    if (SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, p) == S_OK && p[0])
        return p;
    wchar_t* up = _wgetenv(L"USERPROFILE");
    return std::wstring(up && *up ? up : L"C:") + L"\\Desktop";
}
static void ExportDiagnostics() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[128];
    swprintf(name, 128, L"MemSweep-diag-%04d%02d%02d-%02d%02d%02d.txt",
             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::wstring path = DesktopDir() + L"\\" + name;
    std::wstring out;
    wchar_t line[1024];
    swprintf(line, 1024,
             L"MemSweep v" MEMSWEEP_VERSION L" diagnostics\r\nTime: %04d-%02d-%02d %02d:%02d:%02d\r\n\r\n",
             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    out += line;
    Metrics m;
    EnterCriticalSection(&g_lock);
    m = g_metrics;
    LeaveCriticalSection(&g_lock);
    swprintf(line, 1024,
             L"[metrics]\r\nload=%d%%\r\navail=%llu MB\r\nfree=%llu MB\r\nstandby=%llu MB\r\n"
             L"commit=%llu/%llu (%.0f%%)\r\ncompressed=%llu MB ratio=%.2f\r\n"
             L"nonpaged=%llu MB paged=%llu MB\r\nhardfault=%.0f/s\r\n\r\n",
             m.loadPct, (unsigned long long)(m.availPhys / MB), (unsigned long long)(m.freeBytes / MB),
             (unsigned long long)(m.standbyBytes / MB), (unsigned long long)(m.commitUsed / MB),
             (unsigned long long)(m.commitLimit / MB), m.commitPct,
             (unsigned long long)(m.compressedBytes / MB), m.compressionRatio,
             (unsigned long long)(m.nonpagedBytes / MB), (unsigned long long)(m.pagedBytes / MB),
             m.hardFaultsPerSec);
    out += line;
    SysState st;
    {
        AcquireSRWLockShared(&g_sysLock);
        st = g_sys;
        ReleaseSRWLockShared(&g_sysLock);
    }
    swprintf(line, 1024,
             L"[system]\r\ncompression=%d pageCombining=%d prelaunch=%d\r\n"
             L"sysmain=%d fastStartup=%d wsl=%d\r\npagingFiles=%s init=%d max=%d alloc=%llu MB\r\n\r\n",
             (int)st.compression, (int)st.pageCombining, (int)st.prelaunch,
             (int)st.sysmainRunning, (int)st.fastStartupOn, (int)st.wslEnabled,
             st.pagingFiles.c_str(), st.pfInitMB, st.pfMaxMB, st.pfAllocMB);
    out += line;
    out += L"[config]\r\n" + ReadTextFileW(ConfigPath()) + L"\r\n[log tail]\r\n";
    std::wstring log = ReadTextFileW(LogPath());
    std::vector<std::wstring> lines;
    size_t pos = 0;
    while (pos <= log.size()) {
        size_t e = log.find(L'\n', pos);
        if (e == std::wstring::npos) e = log.size();
        std::wstring l = log.substr(pos, e - pos);
        while (!l.empty() && (l.back() == L'\r' || l.back() == L'\n')) l.pop_back();
        lines.push_back(l);
        pos = e + 1;
    }
    size_t startLine = lines.size() > 80 ? lines.size() - 80 : 0;
    for (size_t i = startLine; i < lines.size(); i++) { out += lines[i]; out += L"\r\n"; }
    WriteUtf8File(path, out);
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    Log(L"诊断已导出：%s", path.c_str());
}

static void SysActionWorker(int id) {
    SysState st;
    {
        AcquireSRWLockShared(&g_sysLock);
        st = g_sys;
        ReleaseSRWLockShared(&g_sysLock);
    }
    auto confirm = [&](const wchar_t* zh, const wchar_t* en) {
        return MessageBoxW(g_hwnd, Tx(zh, en), L"MemSweep",
                           MB_YESNO | MB_ICONQUESTION) == IDYES;
    };
    if (id == 1 && st.mmOk && !st.compression) {
        if (confirm(L"启用 Windows 内存压缩？\n\n会同时确保 SysMain 设为自动并启动。",
                    L"Enable Windows memory compression?\n\nSysMain will be set to Automatic and started too.")) {
            RunPS(L"Set-Service SysMain -StartupType Automatic; Start-Service SysMain; Enable-MMAgent -mc");
            Log(L"系统检查：确保 SysMain 自动启动，并启用内存压缩");
        }
    } else if (id == 2 && st.mmOk && !st.pageCombining) {
        if (confirm(L"启用页面合并？\n\n会同时确保 SysMain 设为自动并启动。",
                    L"Enable page combining?\n\nSysMain will be set to Automatic and started too.")) {
            RunPS(L"Set-Service SysMain -StartupType Automatic; Start-Service SysMain; Enable-MMAgent -pc");
            Log(L"系统检查：确保 SysMain 自动启动，并启用页面合并");
        }
    } else if (id == 3 && (st.pfInitMB != 24576 || st.pfMaxMB != 65536)) {
        if (confirm(L"将页面文件调整为 24576–65536 MB？\n\n需要重启生效。",
                    L"Set pagefile to 24576–65536 MB?\n\nRequires a reboot.")) {
            RunPS(L"Get-CimInstance Win32_PageFileSetting -Filter \"Name='d:\\\\pagefile.sys'\" | "
                  L"Set-CimInstance -Property @{InitialSize=24576;MaximumSize=65536}");
            Log(L"系统检查：页面文件调整为 24576–65536 MB（重启生效）");
        }
    } else if (id == 4 && st.wslEnabled) {
        if (confirm(L"为 WSL 写入 autoMemoryReclaim=gradual？", L"Set WSL autoMemoryReclaim=gradual?")) {
            wchar_t* up = _wgetenv(L"USERPROFILE");
            std::wstring path = std::wstring(up && *up ? up : L"C:\\") + L"\\.wslconfig";
            std::wstring txt = ReadTextFileW(path);
            std::vector<std::wstring> lines;
            size_t pos = 0;
            while (pos <= txt.size()) {
                size_t e = txt.find(L'\n', pos);
                if (e == std::wstring::npos) e = txt.size();
                lines.push_back(txt.substr(pos, e - pos));
                pos = e + 1;
            }
            bool inWsl2 = false, set = false, hasSection = false;
            for (auto& line : lines) {
                std::wstring t = TrimW(line);
                if (t.size() > 2 && t[0] == L'[') {
                    inWsl2 = (_wcsicmp(t.c_str(), L"[experimental]") == 0);
                    if (inWsl2) hasSection = true;
                } else if (inWsl2 && ToLower(t).rfind(L"automemoryreclaim", 0) == 0) {
                    line = L"autoMemoryReclaim=gradual";
                    set = true;
                }
            }
            if (!hasSection) { lines.push_back(L"[experimental]"); lines.push_back(L"autoMemoryReclaim=gradual"); }
            else if (!set) {
                for (size_t i = 0; i < lines.size(); i++) {
                    if (_wcsicmp(TrimW(lines[i]).c_str(), L"[experimental]") == 0) {
                        lines.insert(lines.begin() + i + 1, L"autoMemoryReclaim=gradual");
                        break;
                    }
                }
            }
            std::wstring out;
            for (auto& line : lines) { out += line; out += L"\r\n"; }
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                int l = WideCharToMultiByte(CP_UTF8, 0, out.c_str(), (int)out.size(), nullptr, 0, nullptr, nullptr);
                std::string raw(l, 0);
                WideCharToMultiByte(CP_UTF8, 0, out.c_str(), (int)out.size(), raw.data(), l, nullptr, nullptr);
                DWORD wr = 0;
                WriteFile(h, raw.data(), (DWORD)raw.size(), &wr, nullptr);
                CloseHandle(h);
                Log(L"系统检查：写入 .wslconfig autoMemoryReclaim=gradual");
            }
        }
    } else if (id == 5) {
        ExportDiagnostics();
        if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
        return;
    } else if (id == 6) {
        CloseSheet();
        return;
    }
    QuerySysInfo(true);
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

static DWORD WINAPI SysInfoThread(LPVOID) {
    QuerySysInfo(true);
    HeapCompact(GetProcessHeap(), 0);   // Get-MMAgent/PowerShell 解析的堆归还给系统
    InterlockedExchange(&g_sysLoading, 0);
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_SYSINFO, 0, 0);
    return 0;
}
static void QuerySysInfoAsync() {
    if (InterlockedCompareExchange(&g_sysLoading, 1, 0) != 0) return;
    HANDLE h = CreateThread(nullptr, 0, SysInfoThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else InterlockedExchange(&g_sysLoading, 0);
}

static DWORD WINAPI SysActionThread(LPVOID p) {
    SysActionWorker((int)(intptr_t)p);
    InterlockedExchange(&g_sysLoading, 0);
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_SYSINFO, 0, 0);
    return 0;
}
static void SysAction(int id) {
    if (id == 6) {
        CloseSheet();
        return;
    }
    if (InterlockedCompareExchange(&g_sysLoading, 1, 0) != 0) {
        g_toast = Tx(L"正在读取系统状态…", L"Reading system state…");
        g_lastToastMs = GetTickCount64();
        KickAnim();
        return;
    }
    HANDLE h = CreateThread(nullptr, 0, SysActionThread, (LPVOID)(intptr_t)id, 0, nullptr);
    if (h) CloseHandle(h);
    else InterlockedExchange(&g_sysLoading, 0);
}

static void ShowSystemCheck() {
    BringMainToFront();
    OpenSheet(Sheet::System);
}

int RenderSysCheckSnapshot(const std::wstring& path) {
    InitGdiplus();
    InitThemeFromConfig();
    {
        wchar_t env[16] = {};
        if (GetEnvironmentVariableW(L"MEMSweep_THEME", env, 16)) {
            if (_wcsicmp(env, L"dark") == 0) { g_themeMode = 2; ApplyTheme(); }
            else if (_wcsicmp(env, L"light") == 0) { g_themeMode = 1; ApplyTheme(); }
        }
    }
    CreateFonts();
    {
        Snapshot snap;
        TakeSnapshot(snap);
        EnterCriticalSection(&g_lock);
        g_snap = snap;
        g_metrics = snap.m;
        LeaveCriticalSection(&g_lock);
    }
    QuerySysInfo(true);
    Log(L"[checkpng] status loaded=%d mmOk=%d", (int)g_sys.loaded, (int)g_sys.mmOk);
    HeapCompact(GetProcessHeap(), 0);
    float scale = 1.0f;
    {
        wchar_t env[16] = {};
        if (GetEnvironmentVariableW(L"MEMSweep_DPISCALE", env, 16)) scale = (float)_wtof(env);
        if (scale < 1.0f || scale > 2.5f) scale = 1.0f;
    }
    g_snapshotMode = true;
    g_sheet = Sheet::System;
    g_sheetAnimating = false;
    g_sheetT = 1.0f;
    g_systemExpanded = true;
    s_auto = g_cfg.autoGuard;
    s_hotkey = g_cfg.hotkey; s_eco = g_cfg.ecoQoS; s_silent = g_cfg.silent;
    s_autostart = g_cfg.autostart; s_threshold = g_cfg.threshold;
    g_sliderValue = (float)g_cfg.threshold;
    L.dpi = scale;
    g_drawDirty = true;
    int pw = (int)(520 * scale), ph = (int)(600 * scale);
    BuildLayout((float)pw, (float)ph, scale);
    Log(L"[checkpng] layout ok (scale %.2f, %dx%d)", scale, pw, ph);
    int rc = 1;
    {
        Bitmap bmp(pw, ph, PixelFormat32bppPARGB);
        Graphics g(&bmp);
        DrawAll(g);
        CLSID png;
        GetEncoderClsid(L"image/png", &png);
        rc = (bmp.Save(path.c_str(), &png, nullptr) == Ok) ? 0 : 1;
    }
    DestroyGdiObjects();
    GdiplusShutdown(g_gdiToken);
    return rc;
}

// 单实例转发
// ---------------------------------------------------------------------------
struct FindCtx { HWND out; };
static BOOL CALLBACK FindMainProc(HWND h, LPARAM lp) {
    wchar_t cls[128] = {};
    GetClassNameW(h, cls, 128);
    if (_wcsicmp(cls, kClassName) == 0) {
        ((FindCtx*)lp)->out = h;
        return FALSE;
    }
    return TRUE;
}
static HWND FindMainWindow() {
    FindCtx c{ nullptr };
    EnumWindows(FindMainProc, (LPARAM)&c);
    return c.out;
}
void UI_ForwardShow() {
    for (int i = 0; i < 4; i++) {
        HWND h = FindMainWindow();
        if (h) {
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid) AllowSetForegroundWindow(pid);
            DWORD_PTR res = 0;
            SendMessageTimeoutW(h, WM_APP_SHOW, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1500, &res);
            return;
        }
        Sleep(200);
    }
}
void UI_ForwardClean() {
    HWND h = FindMainWindow();
    if (h) PostMessageW(h, WM_APP_CLEAN, 0, 0);
}
void UI_ForwardExit() {
    HWND h = FindMainWindow();
    if (h) PostMessageW(h, WM_APP_EXIT, 0, 0);
}
void UI_ForwardPause(int minutes) {
    HWND h = FindMainWindow();
    if (h) PostMessageW(h, WM_APP_PAUSE, (WPARAM)minutes, 0);
}
void UI_ForwardCombine() {
    HWND h = FindMainWindow();
    if (h) PostMessageW(h, WM_APP_COMBINE, 0, 0);
}

// ---------------------------------------------------------------------------
// 启动
// ---------------------------------------------------------------------------

static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (!size) return -1;
    std::vector<BYTE> buf(size);
    ImageCodecInfo* codecs = (ImageCodecInfo*)buf.data();
    GetImageEncoders(num, size, codecs);
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(codecs[i].MimeType, format) == 0) {
            *pClsid = codecs[i].Clsid;
            return (int)i;
        }
    }
    return -1;
}

static void ReleaseBackBuffer() {
    delete g_backG; g_backG = nullptr;
    delete g_back; g_back = nullptr;
    g_backW = g_backH = 0;
}
static void DestroyGdiObjects() {
    auto del = [](Font*& f) { if (f) { delete f; f = nullptr; } };
    del(f11); del(f12); del(f15b); del(f12b); del(f11b);
    del(f42b); del(f20b); del(fTrayNormal); del(fTrayClean);
    fTraySz = 0;
    ReleaseBackBuffer();
}

int RenderUISnapshot(const std::wstring& path) {
    Log(L"[uipng] start");
    InitGdiplus();
    Log(L"[uipng] gdiplus ok");
    InitThemeFromConfig();
    {
        wchar_t env[16] = {};
        if (GetEnvironmentVariableW(L"MEMSweep_THEME", env, 16)) {
            if (_wcsicmp(env, L"dark") == 0) { g_themeMode = 2; ApplyTheme(); }
            else if (_wcsicmp(env, L"light") == 0) { g_themeMode = 1; ApplyTheme(); }
        }
    }
    {
        wchar_t lenv[16] = {};
        if (GetEnvironmentVariableW(L"MEMSweep_LANG", lenv, 16)) {
            g_zh.store(_wcsicmp(lenv, L"en") != 0, std::memory_order_relaxed);
        }
    }
    CreateFonts();
    Log(L"[uipng] fonts ok");
    Snapshot s;
    TakeSnapshot(s);
    Log(L"[uipng] snapshot ok (%d procs)", (int)s.procs.size());
    EnterCriticalSection(&g_lock);
    g_snap = s;
    g_metrics = s.m;
    LeaveCriticalSection(&g_lock);
    QuerySysInfo(true);
    g_reclaimableBytes.store(ComputeReclaimable(s, g_cfg, nullptr));
    g_drawDirty = true;
    g_snapshotMode = true;
    g_sheet = Sheet::None;
    {
        wchar_t env[32] = {};
        if (GetEnvironmentVariableW(L"MEMSweep_SHEET", env, 32)) {
            if (_wcsicmp(env, L"settings") == 0) g_sheet = Sheet::Settings;
            else if (_wcsicmp(env, L"advanced") == 0) g_sheet = Sheet::Advanced;
            else if (_wcsicmp(env, L"processes") == 0) g_sheet = Sheet::Processes;
            else if (_wcsicmp(env, L"about") == 0) g_sheet = Sheet::About;
            else if (_wcsicmp(env, L"system") == 0) g_sheet = Sheet::System;
        }
    }
    g_sheetAnimating = false;
    g_sheetT = 1.0f;
    g_gaugeShown = (float)s.m.loadPct;
    g_gaugeTarget = (float)s.m.loadPct;
    s_auto = g_cfg.autoGuard; s_idleDeep = g_cfg.idleDeep;
    s_hotkey = g_cfg.hotkey; s_eco = g_cfg.ecoQoS; s_silent = g_cfg.silent;
    s_threshold = g_cfg.threshold; g_sliderValue = (float)g_cfg.threshold;
    s_autostart = g_cfg.autostart;
    float scale = 1.0f;
    {
        wchar_t env[16] = {};
        if (GetEnvironmentVariableW(L"MEMSweep_DPISCALE", env, 16)) scale = (float)_wtof(env);
        if (scale < 1.0f || scale > 2.5f) scale = 1.0f;
    }
    L.dpi = scale;
    int pw = (int)(520 * scale), ph = (int)(600 * scale);
    BuildLayout((float)pw, (float)ph, scale);
    Log(L"[uipng] layout ok (scale %.2f, %dx%d)", scale, pw, ph);
    int rc = 1;
    {
        Bitmap bmp(pw, ph, PixelFormat32bppPARGB);
        Graphics g(&bmp);
        DrawAll(g);
        Log(L"[uipng] draw ok");
        CLSID png;
        GetEncoderClsid(L"image/png", &png);
        Status st = bmp.Save(path.c_str(), &png, nullptr);
        rc = (st == Ok) ? 0 : 1;
        Log(L"[uipng] saved rc=%d", rc);
    }
    DestroyGdiObjects();
    Log(L"[uipng] cleanup ok");
    GdiplusShutdown(g_gdiToken);
    Log(L"[uipng] shutdown ok");
    return rc;
}

int RunUI(HINSTANCE hInst, bool showOnStart) {
    Log(L"UI start (v" MEMSWEEP_VERSION L", admin=%d)", (int)IsElevated());
    InitGdiplus();
    InitThemeFromConfig();
    CreateFonts();

    // 深色菜单
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (ux) {
        typedef void (WINAPI* FnMode)(int);
        typedef void (WINAPI* FnFlush)();
        auto fn = (FnMode)GetProcAddress(ux, MAKEINTRESOURCEA(135));
        auto fl = (FnFlush)GetProcAddress(ux, MAKEINTRESOURCEA(136));
        if (fn) fn(g_dark ? 1 : 3);   // AllowDark / ForceLight
        if (fl) fl();
    }

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    float dpi = 1.0f;
    {
        HDC dc = GetDC(nullptr);
        dpi = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
        ReleaseDC(nullptr, dc);
    }
    L.dpi = dpi;
    int winW = (int)(520 * dpi), winH = (int)(600 * dpi);
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int wx = wa.left + ((wa.right - wa.left) - winW) / 2;
    int wy = wa.top + ((wa.bottom - wa.top) - winH) / 2;
    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, kClassName, L"MemSweep",
                                WS_POPUP | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                                wx, wy, winW, winH,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    // DWM：圆角 + 深色标题栏
    {
        BOOL dark = g_dark ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));   // immersive dark
        int corner = 2;                                          // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));
        DWORD borderNone = 0xFFFFFFFEu;                          // DWMWA_COLOR_NONE
        DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &borderNone, sizeof(borderNone));
    }

    // 托盘
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
    g_trayOn = true;
    TrayUpdate(true);

    // 热键
    if (g_cfg.hotkey) {
        if (!RegisterHotKey(hwnd, HOTKEY_ID, MOD_CONTROL | MOD_SHIFT, 'M'))
            Log(L"热键 Ctrl+Shift+M 注册失败（可能被其他程序占用）");
    }

    // 首次运行或手动启动（无 /tray）→ 显示面板；开机计划任务带 /tray → 静默托盘
    bool firstRun = GetFileAttributesW(ConfigPath().c_str()) == INVALID_FILE_ATTRIBUTES;
    if (firstRun || showOnStart) {
        BringMainToFront();
        g_toast = Tx(L"欢迎使用 MemSweep · 按 Ctrl+Shift+M 随时清理", L"Welcome to MemSweep · press Ctrl+Shift+M");
        g_lastToastMs = GetTickCount64();
        if (firstRun) SaveConfig();
    }

    // 监控线程
    g_monThread = CreateThread(nullptr, 0, MonitorThreadProc, nullptr, 0, nullptr);
    MonitorTick();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_quit = 1;
    if (g_monThread) {
        WaitForSingleObject(g_monThread, 2000);
        CloseHandle(g_monThread);
    }
    if (g_edFont) DeleteObject(g_edFont);
    if (g_edBrush) DeleteObject(g_edBrush);
    TrayRemove();
    DestroyGdiObjects();
    GdiplusShutdown(g_gdiToken);
    return 0;
}

} // namespace ms
