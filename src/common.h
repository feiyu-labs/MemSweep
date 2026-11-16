// MemSweep — 公共定义
#pragma once

// 单一版本源：UI 关于页、诊断导出、日志共用；资源文件 res/app.rc 仍需手动同步。
#define MEMSWEEP_VERSION      L"1.9.17"
#define MEMSWEEP_VERSION_FULL L"1.9.17.0"


#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define NOMINMAX

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <psapi.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <atomic>

namespace ms {

// ---- 窗口消息 ----
constexpr UINT WM_APP_TRAY      = WM_APP + 0x10;
constexpr UINT WM_APP_STATS     = WM_APP + 0x11;
constexpr UINT WM_APP_CLEAN     = WM_APP + 0x12;
constexpr UINT WM_APP_EXIT      = WM_APP + 0x13;
constexpr UINT WM_APP_CLEANING  = WM_APP + 0x14;
constexpr UINT WM_APP_SHOW      = WM_APP + 0x15;
constexpr UINT WM_APP_SAVED     = WM_APP + 0x16;
constexpr UINT WM_APP_BALLOON   = WM_APP + 0x17;
constexpr UINT WM_APP_PAUSE     = WM_APP + 0x18;
constexpr UINT WM_APP_COMBINE   = WM_APP + 0x19;
constexpr UINT WM_APP_SYSINFO   = WM_APP + 0x1A;
constexpr UINT WM_APP_AUTOSTART = WM_APP + 0x1B;

constexpr int  HOTKEY_ID        = 1;
constexpr wchar_t kClassName[]  = L"MemSweepMainWindow";
constexpr wchar_t kTaskName[]   = L"MemSweep";

// ---- 尺寸常量 ----
constexpr unsigned long long KB = 1024ULL;
constexpr unsigned long long MB = 1024ULL * 1024;
constexpr unsigned long long GB = 1024ULL * 1024 * 1024;

// ---- 字符串助手 ----
inline std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}
inline std::wstring TrimW(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
// 进程名统一口径：去空白、转小写、去 .exe；名单/内置保护/进程表共用。
inline std::wstring NormalizeProcName(const std::wstring& name) {
    std::wstring s = ToLower(TrimW(name));
    if (s.size() > 4 && s.compare(s.size() - 4, 4, L".exe") == 0) s.resize(s.size() - 4);
    return s;
}
inline std::vector<std::wstring> SplitCSV(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == L',' || c == L';' || c == L'，') {
            std::wstring t = ToLower(TrimW(cur));
            if (!t.empty()) out.push_back(t);
            cur.clear();
        } else cur.push_back(c);
    }
    std::wstring t = ToLower(TrimW(cur));
    if (!t.empty()) out.push_back(t);
    return out;
}
inline std::wstring JoinCSV(const std::vector<std::wstring>& v) {
    std::wstring s;
    for (size_t i = 0; i < v.size(); i++) {
        if (i) s += L", ";
        s += v[i];
    }
    return s;
}
inline std::wstring FormatBytes(unsigned long long b) {
    wchar_t buf[64];
    if (b >= GB) swprintf(buf, 64, L"%.1f GB", (double)b / GB);
    else if (b >= MB) swprintf(buf, 64, L"%.0f MB", (double)b / MB);
    else swprintf(buf, 64, L"%.0f KB", (double)b / KB);
    return buf;
}
inline std::wstring FormatInt(unsigned long long v) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%llu", v);
    int len = (int)wcslen(buf), pos = 0, trip = 0;
    wchar_t out[48];
    for (int i = len - 1; i >= 0; i--) {
        out[pos++] = buf[i];
        if (++trip == 3 && i > 0) { out[pos++] = L','; trip = 0; }
    }
    out[pos] = 0;
    for (int i = 0; i < pos / 2; i++) std::swap(out[i], out[pos - 1 - i]);
    return out;
}
inline int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
// 清理结果只看“净增量”，避免 after < before 时出现回绕或负数
inline unsigned long long Grow(unsigned long long after, unsigned long long before) {
    return after > before ? after - before : 0;
}
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float EaseOutCubic(float t) { float u = 1.0f - t; return 1.0f - u * u * u; }

// ---- 全局热键 ----
// 语法与 Windows Terminal / VS Code 一致：Win+Ctrl+Alt+Shift+<Key>，
// 顺序无关、大小写不敏感；输出固定 Win → Ctrl → Alt → Shift。
// 只接受能显示出来的键：字母、数字、F1–F24、Space；其余一律拒绝，
// 避免出现配置里能存、界面上显示不出来的组合。
struct HotkeySpec {
    UINT mods = MOD_CONTROL | MOD_SHIFT;
    UINT vk   = 'M';

    bool IsDefault() const { return mods == (MOD_CONTROL | MOD_SHIFT) && vk == 'M'; }

    static std::wstring KeyLabel(UINT vk) {
        if (vk >= 'A' && vk <= 'Z') return std::wstring(1, (wchar_t)vk);
        if (vk >= '0' && vk <= '9') return std::wstring(1, (wchar_t)vk);
        if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
        if (vk == VK_SPACE) return L"Space";
        if (vk == VK_ESCAPE) return L"Esc";   // 能显示，但会被 IsReservedHotkey 拒掉
        return L"";
    }

    // 修饰键的规范顺序，空集合返回空串（录制时用于实时预览）
    static std::wstring ModsToString(UINT mods) {
        std::wstring s;
        if (mods & MOD_WIN)     s += L"Win+";
        if (mods & MOD_CONTROL) s += L"Ctrl+";
        if (mods & MOD_ALT)     s += L"Alt+";
        if (mods & MOD_SHIFT)   s += L"Shift+";
        return s;
    }

    std::wstring ToString() const {
        std::wstring key = KeyLabel(vk);
        if (key.empty()) return L"";
        return ModsToString(mods) + key;
    }

    // 返回 false 表示字符串不构成合法热键，调用方应回落默认值。
    static bool Parse(const std::wstring& text, HotkeySpec& out) {
        HotkeySpec r;
        r.mods = 0;
        r.vk = 0;
        std::wstring cur;
        std::wstring s = text + L"+";   // 末尾补一个分隔符，省掉收尾分支
        for (wchar_t c : s) {
            if (c != L'+') { cur.push_back(c); continue; }
            std::wstring tok = ToLower(TrimW(cur));
            cur.clear();
            if (tok.empty()) return false;
            if (tok == L"ctrl" || tok == L"control") { r.mods |= MOD_CONTROL; continue; }
            if (tok == L"alt")                       { r.mods |= MOD_ALT; continue; }
            if (tok == L"shift")                     { r.mods |= MOD_SHIFT; continue; }
            if (tok == L"win" || tok == L"windows")  { r.mods |= MOD_WIN; continue; }
            if (r.vk) return false;   // 两个主键
            if (tok.size() == 1 && tok[0] >= L'a' && tok[0] <= L'z') r.vk = (UINT)(tok[0] - L'a' + L'A');
            else if (tok.size() == 1 && tok[0] >= L'0' && tok[0] <= L'9') r.vk = (UINT)tok[0];
            else if (tok == L"space") r.vk = VK_SPACE;
            else if (tok == L"esc" || tok == L"escape") r.vk = VK_ESCAPE;
            else if (tok[0] == L'f' && tok.size() >= 2 && tok.size() <= 3) {
                int n = 0;
                for (size_t i = 1; i < tok.size(); i++) {
                    if (tok[i] < L'0' || tok[i] > L'9') return false;
                    n = n * 10 + (tok[i] - L'0');
                }
                if (n < 1 || n > 24) return false;
                r.vk = (UINT)(VK_F1 + n - 1);
            } else return false;
        }
        if (r.mods == 0 || r.vk == 0) return false;
        out = r;
        return true;
    }
};

// 不适合做全局热键的组合：应用自身的窗口级快捷键（注册成全局会把键从窗口手里抢走），
// 以及 Alt+Tab / Alt+F4 这类人人依赖的系统习惯。录制时直接拒绝，而不是悄悄覆盖。
inline bool IsReservedHotkey(UINT mods, UINT vk) {
    if (vk == VK_ESCAPE) return true;
    if (vk == VK_F4 && mods == MOD_ALT) return true;
    if (vk == VK_TAB && (mods & MOD_ALT)) return true;
    if ((mods & (MOD_CONTROL | MOD_ALT | MOD_WIN | MOD_SHIFT)) != MOD_CONTROL) return false;
    return vk == 'S' || vk == VK_RETURN || vk == VK_SPACE;
}

// ---- 自动清理的提示策略 ----
// 自动清理每 30 秒就可能跑一次，全都弹气泡只会变成噪音：
// 只有真正回收了足够的内存才提醒，且十分钟内最多一次；面板开着时界面里已经能看到结果。
constexpr unsigned long long kAutoNotifyMinGain = 64ULL * 1024 * 1024;
constexpr unsigned long long kAutoNotifyGapMs   = 10ULL * 60 * 1000;
inline bool ShouldNotifyAutoClean(unsigned long long gain, unsigned long long sinceLastNotifyMs,
                                  bool windowVisible, bool silent) {
    if (silent || windowVisible) return false;
    if (gain < kAutoNotifyMinGain) return false;
    return sinceLastNotifyMs >= kAutoNotifyGapMs;
}

// 系统通知状态只放行“用户此刻接受通知”这一种：全屏游戏、演示模式、勿扰时段、
// 全屏应用都一律不弹，宁可不报也不挡画面。SHQueryUserNotificationState 是 Windows
// 为此提供的标准接口，Explorer 自己也用它。
inline bool NotificationStateAllows(QUERY_USER_NOTIFICATION_STATE st) {
    return st == QUNS_ACCEPTS_NOTIFICATIONS;
}


// ---- 语言 ----
extern std::atomic<bool> g_zh;
inline const wchar_t* Tx(const wchar_t* zh, const wchar_t* en) { return g_zh.load(std::memory_order_relaxed) ? zh : en; }

// ---- 配置 ----
struct Config {
    std::wstring lang       = L"zh";
    std::wstring theme      = L"auto";   // auto | light | dark

    bool  autoGuard         = true;
    int   threshold         = 85;      // 使用中百分比
    int   cooldownSec       = 180;
    int   freeFloorMB       = 1500;    // 空闲(不含缓存)低于此值才考虑清待机
    int   standbyFloorMB    = 1024;    // 待机缓存低于此值不清（收益太小）

    bool  purgeLow          = true;    // 自动/手动：低优先级待机
    bool  purgeFullManual   = true;    // 手动：全量待机
    bool  sysCache          = true;    // 手动：系统文件缓存
    bool  combinePages      = false;   // 手动深度：合并重复物理页（可能耗时较久，默认关闭）
    bool  idleDeep          = false;   // 离开时深度维护（空闲后修剪 + 合并页，可中止）
    int   idleDeepSecs      = 600;     // 空闲判定秒数（默认 10 分钟）
    int   idleDeepCooldownSec = 21600; // 两次离开维护的最小间隔（默认 6 小时）
    bool  trimWS            = true;
    int   minWSAutoMB       = 128;
    int   idleSecs          = 300;
    int   focusHistorySec   = 120;

    std::wstring whitelist  = L"qemu-system-i386, vmmemwsl";

    bool  hotkey            = true;
    std::wstring hotkeyCombo = L"Ctrl+Shift+M";   // 语法见 HotkeySpec，老配置缺省即默认值
    bool  ecoQoS            = false;   // 后台抑制（EcoQoS + 内存优先级）
    bool  autostart         = false;
    bool  silent            = false;
    bool  allSessions       = false;

    // 运行期（不落盘）
    int   trimMemorySec     = 600;
};

extern Config g_cfg;
extern CRITICAL_SECTION g_lock;

std::wstring ConfigDir();
std::wstring ConfigPath();
std::wstring LogPath();
void LoadConfig();
void SaveConfig();
void Log(const wchar_t* fmt, ...);

} // namespace ms
