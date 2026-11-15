// MemSweep · 内存速清 — 公共定义
#pragma once

// 单一版本源：UI 关于页、诊断导出、日志共用；资源文件 res/app.rc 仍需手动同步。
#define MEMSWEEP_VERSION      L"1.9.6"
#define MEMSWEEP_VERSION_FULL L"1.9.6.0"


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
    bool  hardFaultGuard    = true;    // 颠簸时跳过待机清理

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
