// MemSweep · 内存速清 — 核心引擎实现
#include "core.h"
#include <array>

namespace ms {

// ---------------------------------------------------------------------------
// 全局状态
// ---------------------------------------------------------------------------
Config g_cfg;                       // UI 线程拥有；后台只读 g_cfgShared
CRITICAL_SECTION g_lock;            // 快照/白名单/焦点/空闲记录的锁
static SRWLOCK g_cfgLock = SRWLOCK_INIT;
static Config g_cfgShared;          // 后台线程读取的不可变配置副本
std::atomic<bool> g_zh{true};

Metrics g_metrics;
Snapshot g_snap;
HWND g_hwnd = nullptr;
volatile LONG g_cleaning = 0;
volatile LONG g_cleanPhase = 0;
volatile LONG g_quit = 0;
HANDLE g_monEvent = nullptr;
static HANDLE g_lowMemEvent = nullptr;
ULONGLONG g_lastAutoCleanMs = 0;
std::atomic<ULONGLONG> g_lastFreeGain{0};
std::atomic<ULONGLONG> g_lastAvailGain{0};
std::atomic<ULONGLONG> g_lastCleanTickMs{0};
std::atomic<double> g_hfPerSec{0.0};
std::atomic<bool> g_gameMode{false};
std::atomic<unsigned long long> g_reclaimableBytes{0};

Config GetConfigSnapshot() {
    AcquireSRWLockShared(&g_cfgLock);
    Config c = g_cfgShared;
    ReleaseSRWLockShared(&g_cfgLock);
    return c;
}
void PublishConfig(const Config& c) {
    AcquireSRWLockExclusive(&g_cfgLock);
    g_cfgShared = c;
    ReleaseSRWLockExclusive(&g_cfgLock);
}

static DWORD g_selfPid = 0;
static DWORD g_selfSession = 0;
static DWORD g_fgPid = 0;
static std::deque<std::pair<DWORD, ULONGLONG>> g_focusHist;
static std::unordered_map<DWORD, std::array<unsigned long long, 3>> g_idleMap; // cpu, lastMoveMs, createTime
static std::unordered_map<DWORD, std::array<unsigned long long, 3>> g_trimRec; // createTime, lastTrimMs, wsAtTrim
static std::unordered_set<DWORD> g_ecoMarked;
static unsigned long long g_prevHardFaults = 0;
static ULONGLONG g_prevTickMs = 0;
static std::atomic<int> g_runtimeIdleSecs{300};
static std::atomic<ULONGLONG> g_pauseUntil{0};

// ---------------------------------------------------------------------------
// NT 定义（数值来源：phnt ntexapi.h，已核对）
// ---------------------------------------------------------------------------
typedef LONG NTSTATUS;
extern "C" {
NTSTATUS NTAPI NtQuerySystemInformation(ULONG cls, PVOID info, ULONG len, PULONG retLen);
NTSTATUS NTAPI NtSetSystemInformation(ULONG cls, PVOID info, ULONG len);
}

static constexpr ULONG SIC_SystemProcessInformation      = 5;
static constexpr ULONG SIC_SystemMemoryListInformation   = 80;
static constexpr ULONG SIC_SystemFileCacheInformationEx  = 81;
static constexpr ULONG SMLC_MemoryPurgeStandbyList       = 4;
static constexpr ULONG SMLC_MemoryPurgeLowPriorityList   = 5;
static constexpr ULONG STATUS_INFO_LENGTH_MISMATCH       = 0xC0000004;

#pragma pack(push, 8)
struct SI_UNICODE_STRING { USHORT Length; USHORT MaximumLength; PWSTR Buffer; };
struct SI_PROC {
    ULONG  NextEntryOffset;
    ULONG  NumberOfThreads;
    ULONGLONG WorkingSetPrivateSize;
    ULONG  HardFaultCount;
    ULONG  NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    SI_UNICODE_STRING ImageName;
    LONG   BasePriority;
    LONG   pad0;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
    ULONG  HandleCount;
    ULONG  SessionId;
    ULONG_PTR UniqueProcessKey;
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    ULONG  PageFaultCount;
    ULONG  pad1;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivatePageCount;
    LARGE_INTEGER ReadOperationCount;
};
#pragma pack(pop)
static_assert(offsetof(SI_PROC, ImageName) == 56, "layout");
static_assert(offsetof(SI_PROC, UniqueProcessId) == 80, "layout");
static_assert(offsetof(SI_PROC, SessionId) == 100, "layout");
static_assert(offsetof(SI_PROC, WorkingSetSize) == 144, "layout");
static_assert(offsetof(SI_PROC, PagefileUsage) == 184, "layout");

struct SI_MEMORY_LIST {
    SIZE_T ZeroPageCount;
    SIZE_T FreePageCount;
    SIZE_T ModifiedPageCount;
    SIZE_T ModifiedNoWritePageCount;
    SIZE_T BadPageCount;
    SIZE_T PageCountByPriority[8];
    SIZE_T RepurposedPagesByPriority[8];
    SIZE_T ModifiedPageCountPageFile;
};

struct SI_STORE_INFO {
    ULONG Version;
    LONG  StoreInformationClass;
    PVOID Data;
    ULONG Length;
};
struct SI_COMPRESSION_INFO {
    ULONG VersionSpare;
    ULONG CompressionPid;
    ULONG WorkingSetSize;
    ULONG Pad;
    SIZE_T TotalDataCompressed;
    SIZE_T TotalCompressedSize;
    SIZE_T TotalUniqueDataCompressed;
    HANDLE PartitionHandle;
};

struct SI_FILECACHE {
    SIZE_T CurrentSize;
    SIZE_T PeakSize;
    ULONG  PageFaultCount;
    SIZE_T MinimumWorkingSet;
    SIZE_T MaximumWorkingSet;
    SIZE_T CurrentSizeIncludingTransitionInPages;
    SIZE_T PeakSizeIncludingTransitionInPages;
    ULONG  TransitionRePurposeCount;
    ULONG  Flags;
};

// ---------------------------------------------------------------------------
// 配置 / 日志路径
// ---------------------------------------------------------------------------
std::wstring ConfigDir() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring exe(path);
    size_t p = exe.find_last_of(L'\\');
    std::wstring exeDir = (p == std::wstring::npos) ? exe : exe.substr(0, p);
    if (GetFileAttributesW((exeDir + L"\\MemSweep.ini").c_str()) != INVALID_FILE_ATTRIBUTES)
        return exeDir;   // 便携模式
    wchar_t* ad = _wgetenv(L"APPDATA");
    return std::wstring((ad && *ad) ? ad : exeDir.c_str()) + L"\\MemSweep";
}
std::wstring ConfigPath() { return ConfigDir() + L"\\config.ini"; }
std::wstring LogPath() { return ConfigDir() + L"\\log.txt"; }

static std::wstring ReadTextUtf8(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > (16 << 20)) { CloseHandle(h); return L""; }
    std::string raw((size_t)sz.QuadPart, 0);
    DWORD rd = 0;
    ReadFile(h, raw.data(), (DWORD)raw.size(), &rd, nullptr);
    CloseHandle(h);
    raw.resize(rd);
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF)
        raw = raw.substr(3);
    int wl = MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)raw.size(), nullptr, 0);
    std::wstring w(wl, 0);
    if (wl) MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)raw.size(), w.data(), wl);
    return w;
}
static bool WriteTextUtf8(const std::wstring& path, const std::wstring& data) {
    CreateDirectoryW(ConfigDir().c_str(), nullptr);
    int l = WideCharToMultiByte(CP_UTF8, 0, data.c_str(), (int)data.size(), nullptr, 0, nullptr, nullptr);
    std::string raw(l, 0);
    if (l) WideCharToMultiByte(CP_UTF8, 0, data.c_str(), (int)data.size(), raw.data(), l, nullptr, nullptr);
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    WriteFile(h, "\xEF\xBB\xBF", 3, &wr, nullptr);
    WriteFile(h, raw.data(), (DWORD)raw.size(), &wr, nullptr);
    CloseHandle(h);
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

void LoadConfig() {
    std::wstring txt = ReadTextUtf8(ConfigPath());
    std::unordered_map<std::wstring, std::wstring> kv;
    size_t pos = 0;
    while (pos < txt.size()) {
        size_t e = txt.find(L'\n', pos);
        if (e == std::wstring::npos) e = txt.size();
        std::wstring line = TrimW(txt.substr(pos, e - pos));
        pos = e + 1;
        if (line.empty() || line[0] == L'#' || line[0] == L'[') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        kv[ToLower(TrimW(line.substr(0, eq)))] = TrimW(line.substr(eq + 1));
    }
    auto get  = [&](const wchar_t* k) { auto it = kv.find(k); return it == kv.end() ? std::wstring() : it->second; };
    auto has  = [&](const wchar_t* k) { return kv.find(k) != kv.end(); };
    auto getB = [&](const wchar_t* k, bool d) { auto v = get(k); if (v.empty()) return d; return v == L"1" || ToLower(v) == L"true"; };
    auto getI = [&](const wchar_t* k, int d) { auto v = get(k); return v.empty() ? d : _wtoi(v.c_str()); };

    g_cfg.lang            = get(L"lang").empty() ? L"zh" : get(L"lang");
    g_cfg.theme           = get(L"theme").empty() ? L"auto" : get(L"theme");
    g_cfg.autoGuard       = getB(L"autoguard", true);
    g_cfg.threshold       = ClampInt(getI(L"threshold", 85), 50, 99);
    g_cfg.cooldownSec     = ClampInt(getI(L"cooldownsec", 180), 30, 3600);
    g_cfg.freeFloorMB     = ClampInt(getI(L"freefloormb", 1500), 128, 32768);
    g_cfg.standbyFloorMB  = ClampInt(getI(L"standbyfloormb", 1024), 64, 32768);
    g_cfg.purgeLow        = getB(L"purgelow", true);
    g_cfg.purgeFullManual = getB(L"purgefullmanual", true);
    g_cfg.sysCache        = getB(L"syscache", true);
    g_cfg.combinePages    = getB(L"combinepages", false);
    g_cfg.idleDeep        = getB(L"idledeep", false);
    g_cfg.idleDeepSecs    = ClampInt(getI(L"idledeepsecs", 600), 5, 7200);
    g_cfg.idleDeepCooldownSec = ClampInt(getI(L"idledeepcooldownsec", 21600), 60, 86400);
    g_cfg.trimWS          = getB(L"trimws", true);
    g_cfg.minWSAutoMB     = ClampInt(getI(L"minwsautomb", 128), 16, 4096);
    g_cfg.idleSecs        = ClampInt(getI(L"idlesecs", 300), 30, 7200);
    g_cfg.focusHistorySec = ClampInt(getI(L"focushistorysec", 120), 15, 600);
    // 空字符串是合法值：用户清空保护名单时必须保留，不能回退到默认 VM 名单
    if (has(L"whitelist")) g_cfg.whitelist = get(L"whitelist");
    g_cfg.hotkey          = getB(L"hotkey", true);
    g_cfg.ecoQoS          = getB(L"ecoqos", false);
    g_cfg.autostart       = getB(L"autostart", false);
    g_cfg.silent          = getB(L"silent", false);
    g_cfg.allSessions     = getB(L"allsessions", false);
    // 抖动保护是内置安全护栏（高缺页时跳过待机清理），不暴露开关，也不落盘。
    // v1.8.1 起合并页跟随「清理力度=深度」；老配置里的 0 自动升级，无需用户干预
    if (g_cfg.purgeFullManual && g_cfg.sysCache) g_cfg.combinePages = true;
    g_zh.store(g_cfg.lang != L"en", std::memory_order_relaxed);
    PublishConfig(g_cfg);
}

void SaveConfig() {
    Config c = g_cfg;
    PublishConfig(c);
    std::wstring s;
    s += L"# MemSweep config (UTF-8)\n[general]\n";
    s += L"lang=" + c.lang + L"\n";
    s += L"theme=" + c.theme + L"\n";
    s += std::wstring(L"silent=") + (c.silent ? L"1" : L"0") + L"\n";
    s += std::wstring(L"autostart=") + (c.autostart ? L"1" : L"0") + L"\n";
    s += std::wstring(L"allsessions=") + (c.allSessions ? L"1" : L"0") + L"\n\n";
    s += L"[guard]\n";
    s += std::wstring(L"autoGuard=") + (c.autoGuard ? L"1" : L"0") + L"\n";
    s += L"threshold=" + std::to_wstring(c.threshold) + L"\n";
    s += L"cooldownSec=" + std::to_wstring(c.cooldownSec) + L"\n";
    s += L"freeFloorMB=" + std::to_wstring(c.freeFloorMB) + L"\n";
    s += L"standbyFloorMB=" + std::to_wstring(c.standbyFloorMB) + L"\n";
    s += L"[clean]\n";
    s += std::wstring(L"purgeLow=") + (c.purgeLow ? L"1" : L"0") + L"\n";
    s += std::wstring(L"purgeFullManual=") + (c.purgeFullManual ? L"1" : L"0") + L"\n";
    s += std::wstring(L"sysCache=") + (c.sysCache ? L"1" : L"0") + L"\n";
    s += std::wstring(L"combinePages=") + (c.combinePages ? L"1" : L"0") + L"\n";
    s += std::wstring(L"idleDeep=") + (c.idleDeep ? L"1" : L"0") + L"\n";
    s += L"idleDeepSecs=" + std::to_wstring(c.idleDeepSecs) + L"\n";
    s += L"idleDeepCooldownSec=" + std::to_wstring(c.idleDeepCooldownSec) + L"\n";
    s += std::wstring(L"trimWS=") + (c.trimWS ? L"1" : L"0") + L"\n";
    s += L"minWSAutoMB=" + std::to_wstring(c.minWSAutoMB) + L"\n";
    s += L"idleSecs=" + std::to_wstring(c.idleSecs) + L"\n";
    s += L"focusHistorySec=" + std::to_wstring(c.focusHistorySec) + L"\n";
    s += L"whitelist=" + c.whitelist + L"\n\n";
    s += L"[advanced]\n";
    s += std::wstring(L"hotkey=") + (c.hotkey ? L"1" : L"0") + L"\n";
    s += std::wstring(L"ecoQoS=") + (c.ecoQoS ? L"1" : L"0") + L"\n";
    WriteTextUtf8(ConfigPath(), s);
}

void Log(const wchar_t* fmt, ...) {
    CreateDirectoryW(ConfigDir().c_str(), nullptr);
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetFileAttributesExW(LogPath().c_str(), GetFileExInfoStandard, &fa) &&
        fa.nFileSizeHigh == 0 && fa.nFileSizeLow > 262144) {
        MoveFileExW(LogPath().c_str(), (ConfigDir() + L"\\log.old.txt").c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    wchar_t buf[1200];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 1199, fmt, ap);
    va_end(ap);
    buf[1199] = 0;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t line[1300];
    swprintf(line, 1300, L"[%04d-%02d-%02d %02d:%02d:%02d] %s\r\n",
             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, buf);
    int l = WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
    std::string raw(l > 0 ? l - 1 : 0, 0);
    if (l > 0) WideCharToMultiByte(CP_UTF8, 0, line, -1, raw.data(), l, nullptr, nullptr);
    HANDLE h = CreateFileW(LogPath().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        WriteFile(h, raw.data(), (DWORD)raw.size(), &wr, nullptr);
        CloseHandle(h);
    }
}

// ---------------------------------------------------------------------------
// 特权 / 提权检查
// ---------------------------------------------------------------------------
static bool SetPrivilegeOne(const wchar_t* name, bool enable) {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid);
    tp.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;
    BOOL ok = AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    DWORD err = GetLastError();
    CloseHandle(tok);
    return ok && err == ERROR_SUCCESS;
}
void EnablePrivileges() {
    SetPrivilegeOne(L"SeProfileSingleProcessPrivilege", true);
    SetPrivilegeOne(L"SeIncreaseQuotaPrivilege", true);
    SetPrivilegeOne(L"SeDebugPrivilege", true);
}
void DisablePrivileges() {
    SetPrivilegeOne(L"SeProfileSingleProcessPrivilege", false);
    SetPrivilegeOne(L"SeIncreaseQuotaPrivilege", false);
    SetPrivilegeOne(L"SeDebugPrivilege", false);
}

void PauseAutoGuard(int minutes) {
    g_pauseUntil.store((minutes > 0) ? GetTickCount64() + (ULONGLONG)minutes * 60000 : 0);
    if (minutes > 0) Log(L"自动清理暂停：%d 分钟", minutes);
    else Log(L"自动清理已恢复");
}
bool AutoPaused() {
    ULONGLONG until = g_pauseUntil.load();
    return until != 0 && GetTickCount64() < until;
}
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    if (ep && ep->ExceptionRecord)
        Log(L"CRASH exception=0x%08X addr=%p",
            (unsigned)ep->ExceptionRecord->ExceptionCode,
            (void*)ep->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_CONTINUE_SEARCH;
}

void InitCore() {
    InitializeCriticalSection(&g_lock);
    if (!g_monEvent) g_monEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    // 系统级低内存事件只作为“立即重新评估”的唤醒；真正的策略阈值仍由
    // ShouldTriggerAuto 决定，避免依赖 OS 较晚的通知阈值。
    if (!g_lowMemEvent) g_lowMemEvent = CreateMemoryResourceNotification(LowMemoryResourceNotification);
    g_selfPid = GetCurrentProcessId();
    DWORD sid = 0;
    if (ProcessIdToSessionId(g_selfPid, &sid)) g_selfSession = sid;
    SetUnhandledExceptionFilter(CrashFilter);
    LoadConfig();
    RebuildWhitelist();
}
bool IsElevated() {
    BOOL admin = FALSE;
    PSID group = nullptr;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0, &group)) {
        CheckTokenMembership(nullptr, group, &admin);
        FreeSid(group);
    }
    return admin != FALSE;
}
bool IsCurrentSession(DWORD pid) {
    DWORD sid = 0;
    if (!ProcessIdToSessionId(pid, &sid)) return true;
    return sid == g_selfSession;
}

// ---------------------------------------------------------------------------
// 快照
// ---------------------------------------------------------------------------
bool QueryMemoryLists(Metrics& m) {
    SI_MEMORY_LIST ml{};
    ULONG ret = 0;
    NTSTATUS st = NtQuerySystemInformation(SIC_SystemMemoryListInformation, &ml, sizeof(ml), &ret);
    if (st != 0) {
        m.freeBytes = m.standbyBytes = m.modifiedBytes = 0;
        return false;
    }
    unsigned long long page = 4096;
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    if (si.dwPageSize) page = si.dwPageSize;
    m.freeBytes = (unsigned long long)(ml.ZeroPageCount + ml.FreePageCount) * page;
    m.modifiedBytes = (unsigned long long)ml.ModifiedPageCount * page;
    unsigned long long sum = 0;
    for (int i = 0; i < 8; i++) {
        m.standbyByPrio[i] = (unsigned long long)ml.PageCountByPriority[i] * page;
        sum += m.standbyByPrio[i];
    }
    m.standbyBytes = sum;
    return true;
}

bool QueryCompression(Metrics& m) {
    m.compressionOk = false;
    m.compressedBytes = m.compressedLogical = 0;
    m.compressionRatio = 0.0;
    SI_COMPRESSION_INFO ci{};
    ci.VersionSpare = 4;   // SYSTEM_STORE_COMPRESSION_INFORMATION_VERSION_V2 (24H2+)
    SI_STORE_INFO si{};
    si.Version = 1;
    si.StoreInformationClass = 22;   // MemCompressionInfoRequest
    si.Data = &ci;
    si.Length = (ULONG)sizeof(ci);
    ULONG ret = 0;
    NTSTATUS st = NtQuerySystemInformation(109 /* SystemStoreInformation */, &si, sizeof(si), &ret);
    if (st != 0) return false;
    m.compressionOk = true;
    m.compressedBytes = (unsigned long long)ci.TotalCompressedSize;
    m.compressedLogical = (unsigned long long)(ci.TotalUniqueDataCompressed ? ci.TotalUniqueDataCompressed
                                                                           : ci.TotalDataCompressed);
    if (ci.TotalCompressedSize)
        m.compressionRatio = (double)ci.TotalUniqueDataCompressed / (double)ci.TotalCompressedSize;
    return true;
}
bool QueryPools(Metrics& m) {
    PERFORMANCE_INFORMATION pi{};
    if (!GetPerformanceInfo(&pi, sizeof(pi))) return false;
    m.nonpagedBytes = (unsigned long long)pi.KernelNonpaged * pi.PageSize;
    m.pagedBytes = (unsigned long long)pi.KernelPaged * pi.PageSize;
    return true;
}

bool TakeSnapshot(Snapshot& s) {
    MEMORYSTATUSEX msx{};
    msx.dwLength = sizeof(msx);
    if (!GlobalMemoryStatusEx(&msx)) return false;
    Metrics& m = s.m;
    m.loadPct = (int)msx.dwMemoryLoad;
    m.totalPhys = msx.ullTotalPhys;
    m.availPhys = msx.ullAvailPhys;
    m.commitLimit = msx.ullTotalPageFile;
    m.commitUsed = msx.ullTotalPageFile > msx.ullAvailPageFile ? msx.ullTotalPageFile - msx.ullAvailPageFile : 0;
    QueryMemoryLists(m);
    QueryCompression(m);
    QueryPools(m);
    if (m.commitLimit)
        m.commitPct = 100.0 * (double)m.commitUsed / (double)m.commitLimit;
    m.hardFaultsPerSec = g_hfPerSec.load(std::memory_order_relaxed);

    // 监控线程、清理线程和离线快照可能并发调用；每线程独立缓冲区，避免共享
    // vector 的 resize/内核写入竞争。
    thread_local std::vector<unsigned char> buf;
    ULONG len = 1 << 20;
    NTSTATUS st = 0;
    for (int i = 0; i < 9; i++) {
        buf.resize(len);
        ULONG ret = 0;
        st = NtQuerySystemInformation(SIC_SystemProcessInformation, buf.data(), len, &ret);
        if (st == (NTSTATUS)STATUS_INFO_LENGTH_MISMATCH) { len = (ULONG)(len * 1.5) + (1 << 20); continue; }
        break;
    }
    if (st != 0) {
        Log(L"TakeSnapshot 失败: 0x%08X", (unsigned)st);
        return false;
    }
    s.procs.clear();
    s.procs.reserve(512);
    unsigned long long hfSum = 0;
    auto* p = (SI_PROC*)buf.data();
    for (;;) {
        ProcInfo pi;
        pi.pid = (DWORD)(uintptr_t)p->UniqueProcessId;
        pi.ppid = (DWORD)(uintptr_t)p->InheritedFromUniqueProcessId;
        pi.sessionId = p->SessionId;
        pi.ws = (unsigned long long)p->WorkingSetSize;
        pi.wsPrivate = (unsigned long long)p->WorkingSetPrivateSize;
        if (!pi.wsPrivate) pi.wsPrivate = pi.ws;   // 极少数系统进程该字段为 0
        pi.commit = (unsigned long long)p->PagefileUsage;
        pi.createTime = (unsigned long long)p->CreateTime.QuadPart;
        pi.cpu100ns = (unsigned long long)p->UserTime.QuadPart + (unsigned long long)p->KernelTime.QuadPart;
        hfSum += p->HardFaultCount;
        if (p->ImageName.Length && p->ImageName.Buffer && p->ImageName.Length < 520)
            pi.name.assign(p->ImageName.Buffer, p->ImageName.Length / sizeof(wchar_t));
        if (pi.pid > 4) s.procs.push_back(std::move(pi));
        if (p->NextEntryOffset == 0) break;
        p = (SI_PROC*)((unsigned char*)p + p->NextEntryOffset);
    }
    // 硬缺页累计值（速率由 MonitorTick 差分计算）
    s.m.hardFaultsCumulative = hfSum;
    s.m.hardFaultsPerSec = g_hfPerSec.load(std::memory_order_relaxed);
    std::sort(s.procs.begin(), s.procs.end(),
              [](const ProcInfo& a, const ProcInfo& b) { return a.ws > b.ws; });
    return true;
}

// ---------------------------------------------------------------------------
// 焦点 / 空闲 / 修剪记忆
// ---------------------------------------------------------------------------
void NoteFocus(DWORD pid) {
    if (!pid || pid == g_selfPid) return;
    ULONGLONG now = GetTickCount64();
    EnterCriticalSection(&g_lock);
    g_fgPid = pid;
    g_focusHist.push_back({ pid, now });
    while (g_focusHist.size() > 96) g_focusHist.pop_front();
    LeaveCriticalSection(&g_lock);
}
void PruneFocusHistory(const Config& c) {
    ULONGLONG now = GetTickCount64();
    ULONGLONG ttl = (ULONGLONG)c.focusHistorySec * 1000;
    EnterCriticalSection(&g_lock);
    while (!g_focusHist.empty() && now - g_focusHist.front().second > ttl)
        g_focusHist.pop_front();
    LeaveCriticalSection(&g_lock);
}
static void UpdateIdleMap(const Snapshot& s) {
    ULONGLONG now = GetTickCount64();
    EnterCriticalSection(&g_lock);
    std::unordered_set<DWORD> alive;
    alive.reserve(s.procs.size() * 2);
    for (const auto& p : s.procs) {
        alive.insert(p.pid);
        auto& rec = g_idleMap[p.pid];
        if (rec[2] != p.createTime) {
            rec[0] = p.cpu100ns; rec[1] = now; rec[2] = p.createTime;
        } else {
            unsigned long long delta = p.cpu100ns > rec[0] ? p.cpu100ns - rec[0] : 0;
            unsigned long long dtMs = now > rec[1] ? now - rec[1] : 1;
            // 仅当 CPU 增量超过单核 0.5%（dtMs*50）才算“有活动”，
            // 忽略聊天心跳/浏览器定时器等微量抖动，避免大进程永不被判空闲。
            if (delta > dtMs * 50) rec[1] = now;
            rec[0] = p.cpu100ns;
        }
    }
    for (auto it = g_idleMap.begin(); it != g_idleMap.end();) {
        if (!alive.count(it->first)) it = g_idleMap.erase(it); else ++it;
    }
    for (auto it = g_trimRec.begin(); it != g_trimRec.end();) {
        if (!alive.count(it->first)) it = g_trimRec.erase(it); else ++it;
    }
    LeaveCriticalSection(&g_lock);
}
static bool IsIdle(DWORD pid) {
    EnterCriticalSection(&g_lock);
    auto it = g_idleMap.find(pid);
    bool idle = false;
    if (it != g_idleMap.end())
        idle = (GetTickCount64() - it->second[1]) >=
               (ULONGLONG)g_runtimeIdleSecs.load(std::memory_order_relaxed) * 1000;
    LeaveCriticalSection(&g_lock);
    return idle;
}

// ---------------------------------------------------------------------------
// 保护判定
// ---------------------------------------------------------------------------
static const std::unordered_set<std::wstring>& BuiltinProtect() {
    static const std::unordered_set<std::wstring> s = {
        L"system", L"smss", L"csrss", L"wininit", L"winlogon", L"services",
        L"lsass", L"lsm", L"svchost", L"dwm", L"explorer", L"fontdrvhost",
        L"audiodg", L"sihost", L"ctfmon", L"memcompression", L"memory compression", L"conhost",
        L"securityhealthservice", L"msmpeng", L"securityhealthsystray",
        // VM/虚拟化内存是客户机热数据，修剪会导致 guest 大范围缺页，始终硬保护
        L"qemu-system-i386", L"qemu-system-x86_64", L"vmmemwsl"
    };
    return s;
}
static std::unordered_set<std::wstring> g_whitelistSet;
void RebuildWhitelist() {
    Config c = GetConfigSnapshot();
    std::unordered_set<std::wstring> next;
    for (auto& n : SplitCSV(c.whitelist)) next.insert(n);
    EnterCriticalSection(&g_lock);
    g_whitelistSet.swap(next);
    LeaveCriticalSection(&g_lock);
}
static std::unordered_set<DWORD> BuildProtectedPids(const Snapshot& s) {
    std::unordered_set<DWORD> set;
    EnterCriticalSection(&g_lock);
    set.insert(g_fgPid);
    for (auto& f : g_focusHist) set.insert(f.first);
    LeaveCriticalSection(&g_lock);
    std::unordered_map<DWORD, DWORD> parent;
    parent.reserve(s.procs.size() * 2);
    for (auto& p : s.procs) parent[p.pid] = p.ppid;
    // 焦点进程父链
    std::vector<DWORD> seeds(set.begin(), set.end());
    for (DWORD pid : seeds) {
        DWORD cur = pid;
        for (int i = 0; i < 8; i++) {
            auto it = parent.find(cur);
            if (it == parent.end() || it->second <= 4) break;
            cur = it->second;
            set.insert(cur);
        }
    }
    return set;
}
bool IsProtectedProcess(const ProcInfo& p, const std::unordered_set<DWORD>& protectedPids) {
    if (p.pid <= 4 || p.pid == g_selfPid) return true;
    if (p.name.empty()) return true;
    std::wstring low = ToLower(p.name);
    if (low.size() > 4 && low.compare(low.size() - 4, 4, L".exe") == 0)
        low.resize(low.size() - 4);          // 快照自带 .exe 后缀，名单不带
    if (BuiltinProtect().count(low)) return true;
    EnterCriticalSection(&g_lock);
    bool wl = g_whitelistSet.count(low) != 0;
    LeaveCriticalSection(&g_lock);
    if (wl) return true;
    if (protectedPids.count(p.pid)) return true;
    return false;
}

// 单一候选判定（策略计数与执行共用，保证一致）
static bool EligibleForTrim(const ProcInfo& p, const std::unordered_set<DWORD>& protPids,
                            const Config& c, bool requireIdle, int minWSMB, bool checkTrimMemory) {
    if (!c.allSessions && !IsCurrentSession(p.pid)) return false;
    if (p.ws < (unsigned long long)minWSMB * MB) return false;
    if (IsProtectedProcess(p, protPids)) return false;
    if (requireIdle && !IsIdle(p.pid)) return false;
    if (checkTrimMemory) {
        EnterCriticalSection(&g_lock);
        auto it = g_trimRec.find(p.pid);
        bool blocked = false;
        if (it != g_trimRec.end() && it->second[0] == p.createTime) {
            ULONGLONG last = it->second[1];
            unsigned long long wsAt = it->second[2];
            ULONGLONG now = GetTickCount64();
            if (now - last < (ULONGLONG)c.trimMemorySec * 1000 &&
                p.ws < wsAt + wsAt / 2)   // 增长不足 1.5 倍则跳过
                blocked = true;
        }
        LeaveCriticalSection(&g_lock);
        if (blocked) return false;
    }
    return true;
}

size_t CountTrimCandidates(const Snapshot& s, bool requireIdle, int minWSMB, const Config& c) {
    auto prot = BuildProtectedPids(s);
    size_t n = 0;
    for (const auto& p : s.procs) {
        if (p.ws < (unsigned long long)minWSMB * MB) break;
        if (EligibleForTrim(p, prot, c, requireIdle, minWSMB, true)) n++;
    }
    return n;
}

unsigned long long ComputeReclaimable(const Snapshot& s, const Config& c, size_t* candidatesOut) {
    auto prot = BuildProtectedPids(s);
    size_t candidates = 0;
    unsigned long long idlePriv = 0;
    for (const auto& p : s.procs) {
        if (p.ws < (unsigned long long)c.minWSAutoMB * MB) break;
        if (!EligibleForTrim(p, prot, c, true, c.minWSAutoMB, true)) continue;
        candidates++;
        idlePriv += p.wsPrivate ? p.wsPrivate : p.ws;
    }
    if (candidatesOut) *candidatesOut = candidates;
    return (c.purgeFullManual && c.sysCache) ? s.m.standbyBytes + idlePriv
                                             : s.m.standbyByPrio[0] + idlePriv;
}

// ---------------------------------------------------------------------------
// 策略
// ---------------------------------------------------------------------------
bool IsSeverePressure(const Metrics& m) {
    // free 低是 Windows 正常使用待机缓存的结果；真实压力看“可用”是否枯竭。
    return m.availPhys < 512ULL * MB || m.loadPct >= 95;
}

bool ShouldTriggerAuto(const Config& c, const Metrics& m, ULONGLONG nowMs, ULONGLONG lastAutoMs) {
    if (!c.autoGuard) return false;
    if (lastAutoMs && nowMs - lastAutoMs < (ULONGLONG)c.cooldownSec * 1000) return false;
    if (m.loadPct >= c.threshold) return true;
    if (IsSeverePressure(m)) return true;
    return false;
}

CleanPlan PlanAuto(const Config& c, const Metrics& m, bool hasCandidates) {
    CleanPlan p;
    p.isAuto = true;
    const bool severe = IsSeverePressure(m);
    const bool starved = m.availPhys < 512ULL * MB || m.freeBytes < 128ULL * MB;
    const bool lowFree = m.freeBytes < (unsigned long long)c.freeFloorMB * MB;
    const bool enoughStandby = m.standbyBytes >= (unsigned long long)c.standbyFloorMB * MB;

    // 有候选时仍可顺手清低优先级缓存；旧策略“二选一”会让空闲页长期停在 0。
    if (c.trimWS && (hasCandidates || severe)) {
        p.trimWS = true;
        p.minWSMB = c.minWSAutoMB;
        p.requireIdle = !severe;
    }
    if (c.purgeLow && enoughStandby && (lowFree || severe)) {
        const bool hfOk = g_hfPerSec.load(std::memory_order_relaxed) < 4000.0 || severe;
        if (hfOk) {
            // 只有真正枯竭才全量清缓存；高负载但仍有可用页时保留高优先级缓存，
            // 避免每次冷却结束都重复清空、让缓存永远无法预热。
            p.purgeFull = severe && starved;
            p.purgeLow = !p.purgeFull;
        }
    }
    p.eco = c.ecoQoS;
    p.minWSMB = c.minWSAutoMB;
    p.requireIdle = !severe;
    return p;
}

CleanPlan PlanIdle(const Config& c) {
    CleanPlan p;
    p.isAuto = false;          // 离开时维护允许 6s 修剪预算
    p.trimWS = c.trimWS;
    p.minWSMB = c.minWSAutoMB;
    p.requireIdle = true;
    p.purgeLow = c.purgeLow;
    p.combine = true;          // 用户显式开启“离开时维护”即包含页面合并
    p.eco = false;             // 不持久改变后台能效状态
    return p;
}

CleanPlan PlanManual(const Config& c) {
    CleanPlan p;
    p.isAuto = false;
    p.trimWS = c.trimWS;
    p.minWSMB = 8;
    p.requireIdle = false;
    if (c.purgeFullManual) p.purgeFull = true;
    else p.purgeLow = c.purgeLow;
    p.sysCache = c.sysCache;
    p.combine = c.combinePages;
    p.eco = c.ecoQoS;
    return p;
}

// ---------------------------------------------------------------------------
// 清理动作
// ---------------------------------------------------------------------------
static bool PurgeStandby(bool lowOnly) {
    ULONG cmd = lowOnly ? SMLC_MemoryPurgeLowPriorityList : SMLC_MemoryPurgeStandbyList;
    NTSTATUS st = NtSetSystemInformation(SIC_SystemMemoryListInformation, &cmd, sizeof(cmd));
    if (st != 0) { Log(L"PurgeStandby(%s) 失败: 0x%08X", lowOnly ? L"低优先级" : L"全量", (unsigned)st); return false; }
    return true;
}
static bool TrimSystemFileCache() {
    SI_FILECACHE fc{};
    fc.MinimumWorkingSet = (SIZE_T)-1;
    fc.MaximumWorkingSet = (SIZE_T)-1;
    NTSTATUS st = NtSetSystemInformation(SIC_SystemFileCacheInformationEx, &fc, sizeof(fc));
    if (st != 0) { Log(L"TrimSystemFileCache 失败: 0x%08X", (unsigned)st); return false; }
    return true;
}
static bool SetEcoQoS(HANDLE h, bool on) {
    PROCESS_POWER_THROTTLING_STATE st{};
    st.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    st.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    st.StateMask = on ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
    BOOL ok = SetProcessInformation(h, ProcessPowerThrottling, &st, sizeof(st));
    return ok != 0;
}
static bool SetMemPriorityLow(HANDLE h) {
    MEMORY_PRIORITY_INFORMATION info{};
    info.MemoryPriority = MEMORY_PRIORITY_LOW;
    BOOL ok = SetProcessInformation(h, ProcessMemoryPriority, &info, sizeof(info));
    return ok != 0;
}
static bool SetMemPriorityNormal(HANDLE h) {
    MEMORY_PRIORITY_INFORMATION info{};
    info.MemoryPriority = MEMORY_PRIORITY_NORMAL;
    BOOL ok = SetProcessInformation(h, ProcessMemoryPriority, &info, sizeof(info));
    return ok != 0;
}

static void NoteTrim(const ProcInfo& p) {
    EnterCriticalSection(&g_lock);
    auto& rec = g_trimRec[p.pid];
    rec[0] = p.createTime;
    rec[1] = GetTickCount64();
    rec[2] = p.ws;
    LeaveCriticalSection(&g_lock);
}

void RunClean(const CleanPlan& plan, const Config& c, CleanResult& r, HANDLE cancel) {
    EnablePrivileges();
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    InterlockedExchange(&g_cleanPhase, 1);
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_CLEANING, 1, 0);
    if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) {
        r.aborted = true;
        InterlockedExchange(&g_cleanPhase, 0);
        DisablePrivileges();
        return;
    }

    // 清理前补种当前焦点（CLI/无 UI 场景下同样保护前台）
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD fpid = 0;
        GetWindowThreadProcessId(fg, &fpid);
        if (fpid) NoteFocus(fpid);
    }

    Snapshot before;
    TakeSnapshot(before);
    r.availBefore = before.m.availPhys;
    r.freeBefore = before.m.freeBytes;
    r.standbyBefore = before.m.standbyBytes;

    ULONGLONG t0 = GetTickCount64();
    auto prot = BuildProtectedPids(before);

    // 1) 待机 / 文件缓存（先做，避免刚修剪出的页被立刻丢弃）
    if (!r.aborted) {
        if (plan.purgeLow)  r.standbyLow = PurgeStandby(true);
        if (plan.purgeFull) r.standbyFull = PurgeStandby(false);
        if (plan.sysCache)  r.cache = TrimSystemFileCache();
    }

    // 2) 修剪工作集
    if (plan.trimWS) {
        for (const auto& p : before.procs) {
            if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) {
                r.aborted = true;
                break;
            }
            if (p.ws < (unsigned long long)plan.minWSMB * MB) break;   // 已降序
            if (!EligibleForTrim(p, prot, c, plan.requireIdle, plan.minWSMB, true)) continue;
            r.candidates++;
            HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION |
                                   PROCESS_SET_INFORMATION, FALSE, p.pid);
            if (!h) continue;
            r.attempted++;
            if (EmptyWorkingSet(h)) {
                r.trimmed++;
                NoteTrim(p);
                if (plan.eco) {
                    if (SetMemPriorityLow(h)) { SetEcoQoS(h, true); }
                    EnterCriticalSection(&g_lock);
                    g_ecoMarked.insert(p.pid);
                    LeaveCriticalSection(&g_lock);
                    r.eco++;
                }
            }
            CloseHandle(h);
            ULONGLONG budget = plan.isAuto ? 2000 : 6000;
            if (GetTickCount64() - t0 > budget) {
                Log(L"修剪预算用尽（%llums），提前结束：已处理 %d 个", (unsigned long long)budget, r.trimmed);
                break;
            }
        }
    } else if (plan.eco) {
        // 仅驯服、不修剪
        for (const auto& p : before.procs) {
            if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) {
                r.aborted = true;
                break;
            }
            if (p.ws < (unsigned long long)plan.minWSMB * MB) break;
            if (!EligibleForTrim(p, prot, c, plan.requireIdle, plan.minWSMB, false)) continue;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION, FALSE, p.pid);
            if (!h) continue;
            if (SetMemPriorityLow(h) && SetEcoQoS(h, true)) {
                EnterCriticalSection(&g_lock);
                g_ecoMarked.insert(p.pid);
                LeaveCriticalSection(&g_lock);
                r.eco++;
            }
            CloseHandle(h);
            if (GetTickCount64() - t0 > 2000) break;  // 仅驯服模式固定 2s
        }
    }

    // 3) 深度手动：请求内核合并内容相同的物理页。该操作会扫描全内存，
    //    放在后台线程且仅深度手动模式执行；自动模式永不触发。
    if (plan.combine && !r.aborted) {
        InterlockedExchange(&g_cleanPhase, 2);
        if (g_hwnd) PostMessageW(g_hwnd, WM_APP_CLEANING, 2, 0);
        CombineResult cr;
        bool ok = CombinePages(cr, cancel ? 30000 : 60000, cancel);
        r.combineAborted = cr.aborted;
        if (cr.aborted) r.aborted = true;
        if (ok) {
            r.combined = true;
            r.pagesCombined = cr.pagesCombined;
        }
    }

    Snapshot after;
    TakeSnapshot(after);
    r.availAfter = after.m.availPhys;
    r.freeAfter = after.m.freeBytes;
    r.standbyAfter = after.m.standbyBytes;
    r.availGain = Grow(r.availAfter, r.availBefore);
    r.freeGain = Grow(r.freeAfter, r.freeBefore);
    r.durationMs = GetTickCount64() - t0;
    InterlockedExchange(&g_cleanPhase, 0);

    EmptyWorkingSet(GetCurrentProcess());
    HeapCompact(GetProcessHeap(), 0);
    DisablePrivileges();
}

void LogCleanResult(const wchar_t* mode, size_t candidates, const CleanResult& r) {
    Log(L"%s清理%s：候选 %d，修剪 %d/%d，低优先级缓存:%s 全量缓存:%s 文件缓存:%s 合并页:%s(%llu)，后台抑制+%d，空闲 +%llu MB，可用 +%llu MB（可用 %s → %s，空闲 %s → %s，耗时 %llums）",
        mode, r.aborted ? L"（已中止）" : L"", (int)candidates, r.trimmed, r.attempted,
        r.standbyLow ? L"✔" : L"-", r.standbyFull ? L"✔" : L"-", r.cache ? L"✔" : L"-",
        r.combined ? (r.combineAborted ? L"中止" : L"✔") : L"-", r.pagesCombined, r.eco,
        (unsigned long long)(r.freeGain / MB), (unsigned long long)(r.availGain / MB),
        FormatBytes(r.availBefore).c_str(), FormatBytes(r.availAfter).c_str(),
        FormatBytes(r.freeBefore).c_str(), FormatBytes(r.freeAfter).c_str(),
        (unsigned long long)r.durationMs);
}

void RevertBackgroundTaming() {
    std::unordered_set<DWORD> marked;
    EnterCriticalSection(&g_lock);
    marked.swap(g_ecoMarked);
    LeaveCriticalSection(&g_lock);
    for (DWORD pid : marked) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION, FALSE, pid);
        if (!h) continue;
        SetEcoQoS(h, false);
        SetMemPriorityNormal(h);
        CloseHandle(h);
    }
    if (!marked.empty()) Log(L"已撤销 %u 个进程的后台抑制标记", (unsigned)marked.size());
}

// ---------------------------------------------------------------------------
// 清理线程
// ---------------------------------------------------------------------------
static DWORD WINAPI CleanThreadProc(LPVOID param) {
    bool isAuto = param != nullptr;
    InterlockedExchange(&g_cleanPhase, 1);
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_CLEANING, 1, 0);

    Snapshot s;
    TakeSnapshot(s);
    Config cfg = GetConfigSnapshot();
    size_t candidates = 0;
    if (!isAuto) {
        candidates = CountTrimCandidates(s, false, 8, cfg);
    } else {
        candidates = CountTrimCandidates(s, true, cfg.minWSAutoMB, cfg);
    }
    CleanPlan plan = isAuto ? PlanAuto(cfg, s.m, candidates > 0) : PlanManual(cfg);

    CleanResult r;
    RunClean(plan, cfg, r);

    if (isAuto) {
        g_lastAutoCleanMs = GetTickCount64();
    }
    g_lastFreeGain = r.freeGain;
    g_lastAvailGain = r.availGain;
    g_lastCleanTickMs = GetTickCount64();

    LogCleanResult(isAuto ? L"自动" : L"手动", (size_t)r.candidates, r);

    if (!isAuto && g_hwnd && !cfg.silent) {
        // 通知口径对齐同赛道产品（WinMemoryCleaner: "freed ~X"）：
        // “已释放约 X”取空闲/可用净增的较大者，单指标、不堆术语。
        unsigned long long shown = r.freeGain > r.availGain ? r.freeGain : r.availGain;
        wchar_t info[320];
        if (r.combined && r.pagesCombined)
            swprintf(info, 320, Tx(r.combineAborted ? L"深度清理已中止，已释放约 %s，可用 %s"
                                                    : L"深度清理完成，已释放约 %s，可用 %s",
                                   r.combineAborted ? L"Deep clean stopped, freed about %s, %s available"
                                                    : L"Deep clean done, freed about %s, %s available"),
                     FormatBytes(shown).c_str(), FormatBytes(r.availAfter).c_str());
        else if (shown >= 16ULL * MB)
            swprintf(info, 320, Tx(L"已释放约 %s，可用 %s", L"Freed about %s, %s available"),
                     FormatBytes(shown).c_str(), FormatBytes(r.availAfter).c_str());
        else
            swprintf(info, 320, Tx(L"清理完成，可用 %s", L"Done, %s available"),
                     FormatBytes(r.availAfter).c_str());
        PostMessageW(g_hwnd, WM_APP_BALLOON, 0, (LPARAM)new std::wstring(info));
    }
    if (g_hwnd) {
        PostMessageW(g_hwnd, WM_APP_CLEANING, 0, 0);
        PostMessageW(g_hwnd, WM_APP_STATS, 0, 0);
    }
    InterlockedExchange(&g_cleaning, 0);
    return 0;
}

bool StartCleanAsync(bool isAuto) {
    if (InterlockedCompareExchange(&g_cleaning, 1, 0) != 0) return false;
    HANDLE h = CreateThread(nullptr, 0, CleanThreadProc,
                            (LPVOID)(uintptr_t)(isAuto ? 1 : 0), 0, nullptr);
    if (!h) {
        InterlockedExchange(&g_cleaning, 0);
        return false;
    }
    CloseHandle(h);
    return true;
}

// 手动清理（含演练）
void RequestManualClean(bool dryRun) {
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD fpid = 0;
        GetWindowThreadProcessId(fg, &fpid);
        if (fpid) NoteFocus(fpid);
    }
    // 非 dry-run 的快照/策略由 StartCleanAsync 的工作线程完成；
    // UI 线程这里不能再整表遍历，否则点击瞬间会掉帧。
    if (!dryRun) {
        StartCleanAsync(false);
        return;
    }

    Snapshot s;
    TakeSnapshot(s);
    Config cfg = GetConfigSnapshot();
    size_t candidates = CountTrimCandidates(s, false, 8, cfg);
    CleanPlan plan = PlanManual(cfg);
    Log(L"【演练】计划：修剪=%d(%dMB) 全量缓存=%d 低优先级=%d 文件缓存=%d 合并页=%d 后台抑制=%d 候选=%d",
        (int)plan.trimWS, plan.minWSMB, (int)plan.purgeFull, (int)plan.purgeLow,
        (int)plan.sysCache, (int)plan.combine, (int)plan.eco, (int)candidates);
    auto prot = BuildProtectedPids(s);
    int shown = 0;
    for (const auto& p : s.procs) {
        if (p.ws < 8ULL * MB) break;
        if (!EligibleForTrim(p, prot, cfg, false, 8, true)) continue;
        Log(L"  将修剪: %s (PID %u, WS %s)", p.name.c_str(), p.pid, FormatBytes(p.ws).c_str());
        if (++shown >= 20) break;
    }
}

// ---------------------------------------------------------------------------
// 监控
// ---------------------------------------------------------------------------
static bool IsFullscreenOrPresenting() {
    QUERY_USER_NOTIFICATION_STATE st{};
    if (FAILED(SHQueryUserNotificationState(&st))) return false;
    return st == QUNS_RUNNING_D3D_FULL_SCREEN || st == QUNS_PRESENTATION_MODE;
}

// 会话级用户空闲；GetLastInputInfo 的 dwTime 是 32 位 tick，用无符号差值避免回绕。
static bool IsUserIdle(DWORD seconds) {
    LASTINPUTINFO li{};
    li.cbSize = sizeof(li);
    if (!GetLastInputInfo(&li)) return false;
    DWORD idle = (DWORD)(GetTickCount() - li.dwTime);
    return idle >= seconds * 1000;
}
static bool IsBatteryLow() {
    SYSTEM_POWER_STATUS ps{};
    if (!GetSystemPowerStatus(&ps)) return false;
    return ps.ACLineStatus == 0 && ps.BatteryLifePercent != 255 && ps.BatteryLifePercent < 30;
}

struct IdleCancelCtx { HANDLE stop; HANDLE cancel; };
static DWORD WINAPI IdleCancelWatch(LPVOID param) {
    auto* c = (IdleCancelCtx*)param;
    if (!c || !c->stop || !c->cancel) return 0;
    while (WaitForSingleObject(c->stop, 1000) == WAIT_TIMEOUT) {
        if (!IsUserIdle(3)) { SetEvent(c->cancel); break; }
    }
    return 0;
}

static std::atomic<ULONGLONG> g_lastIdleDeepMs{0};
static DWORD WINAPI IdleDeepThread(LPVOID) {
    Config cfg = GetConfigSnapshot();
    Snapshot s;
    TakeSnapshot(s);
    size_t candidates = CountTrimCandidates(s, true, cfg.minWSAutoMB, cfg);

    HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    IdleCancelCtx ctx{ stop, cancel };
    HANDLE watcher = (cancel && stop) ? CreateThread(nullptr, 0, IdleCancelWatch, &ctx, 0, nullptr) : nullptr;

    CleanPlan plan = PlanIdle(cfg);
    Log(L"离开时维护开始：空闲候选 %d，计划 修剪=%d(%dMB) 低优先级=%d 合并页=%d",
        (int)candidates, (int)plan.trimWS, plan.minWSMB, (int)plan.purgeLow, (int)plan.combine);
    CleanResult r;
    RunClean(plan, cfg, r, cancel);

    if (watcher) {
        if (stop) SetEvent(stop);
        WaitForSingleObject(watcher, 1000);
        CloseHandle(watcher);
    }
    if (stop) CloseHandle(stop);
    if (cancel) CloseHandle(cancel);

    // 无净增益的中止不覆盖用户上一次清理记录，避免“上次清理”信息丢失
    if (r.freeGain > 0 || r.availGain > 0) {
        g_lastFreeGain.store(r.freeGain);
        g_lastAvailGain.store(r.availGain);
        g_lastCleanTickMs.store(GetTickCount64());
    }
    LogCleanResult(L"离开时维护", (size_t)r.candidates, r);
    if (g_hwnd) {
        PostMessageW(g_hwnd, WM_APP_STATS, 0, 0);
        PostMessageW(g_hwnd, WM_APP_CLEANING, 0, 0);
    }
    InterlockedExchange(&g_cleaning, 0);
    return 0;
}

static bool StartIdleDeepAsync() {
    if (InterlockedCompareExchange(&g_cleaning, 1, 0) != 0) return false;
    g_lastIdleDeepMs.store(GetTickCount64());
    HANDLE h = CreateThread(nullptr, 0, IdleDeepThread, nullptr, 0, nullptr);
    if (!h) {
        g_lastIdleDeepMs.store(0);
        InterlockedExchange(&g_cleaning, 0);
        return false;
    }
    CloseHandle(h);
    return true;
}

void MonitorTick() {
    ULONGLONG tickT0 = GetTickCount64();
    static ULONGLONG tickPrev = 0;
    static bool tickLog = [] { wchar_t e[8] = {}; return GetEnvironmentVariableW(L"MEMSweep_TICKLOG", e, 8) != 0; }();
    Snapshot s;
    if (!TakeSnapshot(s)) return;
    Config cfg = GetConfigSnapshot();
    UpdateIdleMap(s);
    PruneFocusHistory(cfg);

    bool game = IsFullscreenOrPresenting();
    if (g_gameMode.exchange(game) != game) {
        Log(game ? L"检测到全屏/演示模式，自动清理暂停"
                 : L"退出全屏/演示模式，自动清理恢复");
    }

    // 焦点兜底
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid) NoteFocus(pid);
    }

    ULONGLONG now = GetTickCount64();
    // 高压时放宽空闲阈值：负载 ≥ 阈值+5 或 空闲 < 512MB → 最长 60s
    int idleCfg = cfg.idleSecs;
    if (s.m.loadPct >= cfg.threshold + 5 || s.m.freeBytes < 512ULL * MB)
        if (idleCfg > 60) idleCfg = 60;
    g_runtimeIdleSecs = idleCfg;
    if (g_prevTickMs) {
        double dt = (double)(now - g_prevTickMs) / 1000.0;
        if (dt > 0.5 && s.m.hardFaultsCumulative >= g_prevHardFaults)
            g_hfPerSec.store((double)(s.m.hardFaultsCumulative - g_prevHardFaults) / dt,
                             std::memory_order_relaxed);
        g_prevHardFaults = s.m.hardFaultsCumulative;
    }
    g_prevTickMs = now;
    s.m.hardFaultsPerSec = g_hfPerSec.load(std::memory_order_relaxed);

    bool idleWanted = cfg.idleDeep && !game && !AutoPaused() && !IsBatteryLow() &&
                      IsUserIdle((DWORD)cfg.idleDeepSecs) &&
                      (s.m.loadPct >= cfg.threshold || s.m.commitPct >= 85.0);
    ULONGLONG lastIdle = g_lastIdleDeepMs.load();
    if (idleWanted && (!lastIdle ||
        now - lastIdle >= (ULONGLONG)cfg.idleDeepCooldownSec * 1000)) {
        if (StartIdleDeepAsync())
            Log(L"离开时维护触发：空闲 ≥%d 秒，使用中 %d%%，提交 %.0f%%",
                cfg.idleDeepSecs, s.m.loadPct, s.m.commitPct);
    }

    // 可回收估算（每 tick 一次，纯内存计算）：空闲候选私有WS
    // + 低优先级缓存（常规力度）或全量缓存（深度力度）。
    // 仅面板可见时计算——它只是 UI 显示指标，托盘/后台不需要，
    // 且省去每 5s 对 500 进程的保护判定与字符串分配。
    size_t candidates = 0;
    if (g_hwnd && IsWindowVisible(g_hwnd))
        g_reclaimableBytes.store(ComputeReclaimable(s, cfg, &candidates));
    else
        candidates = CountTrimCandidates(s, true, cfg.minWSAutoMB, cfg);

    if (!g_cleaning && !game && !AutoPaused() &&
        ShouldTriggerAuto(cfg, s.m, now, g_lastAutoCleanMs)) {
        CleanPlan plan = PlanAuto(cfg, s.m, candidates > 0);
        if (plan.trimWS || plan.purgeLow || plan.purgeFull) {
            Log(L"自动清理触发：使用中 %d%%（阈值 %d%%），可用 %s，空闲 %s，缓存 %s，"
                L"硬缺页 %.0f/s，候选 %d，计划 修剪=%d(%s) 低优先级=%d 全量=%d",
                s.m.loadPct, cfg.threshold, FormatBytes(s.m.availPhys).c_str(),
                FormatBytes(s.m.freeBytes).c_str(), FormatBytes(s.m.standbyBytes).c_str(),
                g_hfPerSec.load(std::memory_order_relaxed), (int)candidates, (int)plan.trimWS,
                !plan.trimWS ? L"-" : (plan.requireIdle ? L"空闲" : L"紧急"),
                (int)plan.purgeLow, (int)plan.purgeFull);
            StartCleanAsync(true);
        }
    }

    EnterCriticalSection(&g_lock);
    g_snap = std::move(s);
    g_metrics = g_snap.m;
    LeaveCriticalSection(&g_lock);
    if (tickLog)
        Log(L"[ticklog] interval=%llums cost=%llums",
            tickPrev ? tickT0 - tickPrev : 0, GetTickCount64() - tickT0);
    tickPrev = tickT0;
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_STATS, 0, 0);
}

DWORD WINAPI MonitorThreadProc(LPVOID) {
    while (!g_quit) {
        HANDLE hs[2] = {};
        DWORD n = 0;
        int lowIndex = -1;
        if (g_monEvent) hs[n++] = g_monEvent;
        if (g_lowMemEvent) { lowIndex = (int)n; hs[n++] = g_lowMemEvent; }
        DWORD w = n ? WaitForMultipleObjects(n, hs, FALSE, 5000)
                    : (Sleep(5000), WAIT_TIMEOUT);
        if (g_quit) break;
        MonitorTick();
        // LowMemoryResourceNotification 在条件持续期间保持 signaled；
        // 加 2 秒节流，避免事件循环空转。
        if (lowIndex >= 0 && w == WAIT_OBJECT_0 + (DWORD)lowIndex) Sleep(2000);
    }
    return 0;
}

struct CombineWatch {
    HANDLE stop = nullptr;
    HANDLE cancel = nullptr;
    DWORD timeoutMs = 0;
};
static DWORD WINAPI CombineWatchdog(LPVOID param) {
    auto* w = (CombineWatch*)param;
    if (!w || !w->stop || !w->cancel || !w->timeoutMs) return 0;
    if (WaitForSingleObject(w->stop, w->timeoutMs) == WAIT_TIMEOUT)
        SetEvent(w->cancel);
    return 0;
}

bool CombinePages(CombineResult& out, DWORD timeoutMs, HANDLE externalCancel) {
    EnablePrivileges();
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    out.availBefore = ms.ullAvailPhys;
    Metrics lists;
    QueryMemoryLists(lists);
    out.freeBefore = lists.freeBytes;

    bool ownCancel = false;
    HANDLE cancel = externalCancel;
    if (!cancel) {
        cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ownCancel = true;
    }
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CombineWatch watch{ stop, cancel, timeoutMs };
    HANDLE watchdog = (stop && cancel && timeoutMs)
                    ? CreateThread(nullptr, 0, CombineWatchdog, &watch, 0, nullptr)
                    : nullptr;

    struct SI_COMBINE { HANDLE Handle; SIZE_T PagesCombined; ULONG Flags; ULONG Pad; };
    SI_COMBINE info{};
    info.Handle = cancel;
    ULONGLONG t0 = GetTickCount64();
    NTSTATUS st = NtSetSystemInformation(130 /* SystemCombinePhysicalMemoryInformation */,
                                         &info, sizeof(info));
    out.durationMs = GetTickCount64() - t0;
    out.aborted = cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0;
    out.ok = (st == 0);
    out.pagesCombined = (unsigned long long)info.PagesCombined;
    if (stop) SetEvent(stop);
    if (watchdog) {
        WaitForSingleObject(watchdog, 1000);
        CloseHandle(watchdog);
    }
    if (stop) CloseHandle(stop);
    if (ownCancel && cancel) CloseHandle(cancel);
    GlobalMemoryStatusEx(&ms);
    out.availAfter = ms.ullAvailPhys;
    QueryMemoryLists(lists);
    out.freeAfter = lists.freeBytes;
    Log(L"深度整理：合并 %llu 页（≈%s），可用 %s → %s，空闲 %s → %s，耗时 %llums，%sstatus=0x%08X",
        out.pagesCombined,
        FormatBytes(out.pagesCombined * 4096ULL).c_str(),
        FormatBytes(out.availBefore).c_str(), FormatBytes(out.availAfter).c_str(),
        FormatBytes(out.freeBefore).c_str(), FormatBytes(out.freeAfter).c_str(),
        out.durationMs, out.aborted ? L"已中止（超时/取消），" : L"", (unsigned)st);
    EmptyWorkingSet(GetCurrentProcess());
    HeapCompact(GetProcessHeap(), 0);
    DisablePrivileges();
    return out.ok;
}
bool TrimPid(DWORD pid) {
    if (pid <= 4 || pid == g_selfPid) return false;
    EnablePrivileges();
    HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) { DisablePrivileges(); return false; }
    BOOL ok = EmptyWorkingSet(h);
    CloseHandle(h);
    DisablePrivileges();
    return ok != 0;
}
bool IsBuiltinProtectedName(const std::wstring& name) {
    std::wstring low = ToLower(name);
    if (low.size() > 4 && low.compare(low.size() - 4, 4, L".exe") == 0) low.resize(low.size() - 4);
    return !low.empty() && BuiltinProtect().count(low) != 0;
}

bool IsProtectedName(const std::wstring& name) {
    std::wstring low = ToLower(name);
    if (low.size() > 4 && low.compare(low.size() - 4, 4, L".exe") == 0) low.resize(low.size() - 4);
    if (low.empty()) return true;
    if (BuiltinProtect().count(low)) return true;
    EnterCriticalSection(&g_lock);
    bool wl = g_whitelistSet.count(low) != 0;
    LeaveCriticalSection(&g_lock);
    return wl;
}

void DumpIdleDiagnostics() {
    Snapshot s;
    TakeSnapshot(s);
    Config cfg = GetConfigSnapshot();
    auto prot = BuildProtectedPids(s);
    ULONGLONG now = GetTickCount64();
    Log(L"[idlecheck] load=%d free=%s standby=%s commit=%.0f%% procs=%d idleSecs=%d minWS=%dMB",
        s.m.loadPct, FormatBytes(s.m.freeBytes).c_str(), FormatBytes(s.m.standbyBytes).c_str(),
        s.m.commitPct, (int)s.procs.size(), cfg.idleSecs, cfg.minWSAutoMB);
    int shown = 0;
    for (auto& p : s.procs) {
        if (p.ws < 64ULL * MB) break;
        bool pd = IsProtectedProcess(p, prot);
        ULONGLONG idleMs = 0;
        bool blocked = false;
        EnterCriticalSection(&g_lock);
        auto it = g_idleMap.find(p.pid);
        if (it != g_idleMap.end() && now >= it->second[1]) idleMs = now - it->second[1];
        auto tr = g_trimRec.find(p.pid);
        if (tr != g_trimRec.end() && tr->second[0] == p.createTime) {
            if (now - tr->second[1] < (ULONGLONG)cfg.trimMemorySec * 1000 &&
                p.ws < tr->second[2] + tr->second[2] / 2)
                blocked = true;
        }
        LeaveCriticalSection(&g_lock);
        Log(L"[idlecheck] %-26s pid=%6u ws=%8s private=%8s idle=%5llus protected=%d trimBlocked=%d",
            p.name.c_str(), p.pid, FormatBytes(p.ws).c_str(), FormatBytes(p.wsPrivate).c_str(),
            (unsigned long long)(idleMs / 1000), (int)pd, (int)blocked);
        if (++shown >= 30) break;
    }
}

// ---------------------------------------------------------------------------
// 自检（纯逻辑）
// ---------------------------------------------------------------------------
bool SelfTest() {
    bool ok = true;
    auto chk = [&](bool cond, const wchar_t* name) {
        if (!cond) { ok = false; Log(L"[selftest] FAIL: %s", name); }
        return cond;
    };
    Config c;
    Metrics m;

    // 触发语义：free 低不等于压力，只有可用枯竭才算紧急
    m.totalPhys = 24ULL * GB; m.availPhys = 4ULL * GB;
    m.loadPct = 84; m.freeBytes = 2ULL * GB; m.standbyBytes = 2ULL * GB;
    chk(!ShouldTriggerAuto(c, m, 1000000, 0), L"load 84% should not trigger");
    m.loadPct = 85;
    chk(ShouldTriggerAuto(c, m, 1000000, 0), L"load 85% should trigger");
    chk(!ShouldTriggerAuto(c, m, 1000000, 1000000 - 60 * 1000), L"cooldown must block");
    chk(ShouldTriggerAuto(c, m, 1000000, 1000000 - 181 * 1000), L"after cooldown should trigger");
    m.loadPct = 50; m.availPhys = 200ULL * MB; m.freeBytes = 50ULL * MB;
    chk(IsSeverePressure(m), L"avail<512MB is severe");
    chk(ShouldTriggerAuto(c, m, 5000000, 0), L"emergency avail<512MB should trigger");
    m.availPhys = 4ULL * GB; m.freeBytes = 20ULL * MB;
    chk(!ShouldTriggerAuto(c, m, 5000000, 0), L"low free with ample avail should not trigger");

    // 自动计划：有候选也要顺手清低优先级缓存；常规压力保留高优先级缓存
    m.loadPct = 90; m.availPhys = 4ULL * GB;
    m.freeBytes = 800ULL * MB; m.standbyBytes = 2ULL * GB;
    CleanPlan p1 = PlanAuto(c, m, true);
    chk(p1.trimWS && p1.purgeLow && !p1.purgeFull, L"auto with candidates trims + low purge");
    chk(p1.requireIdle && p1.minWSMB == c.minWSAutoMB, L"auto trim params");
    // 无候选 + 低空闲 + 缓存足够 → 低优先级（不全量）
    CleanPlan p2 = PlanAuto(c, m, false);
    chk(!p2.trimWS && p2.purgeLow && !p2.purgeFull, L"auto without candidates purges low only");
    // 缓存不足 → 不清理
    m.standbyBytes = 100ULL * MB;
    CleanPlan p3 = PlanAuto(c, m, false);
    chk(!p3.trimWS && !p3.purgeLow && !p3.purgeFull, L"small standby should be skipped");
    // 空闲充足 → 不清理
    m.freeBytes = 4ULL * GB; m.standbyBytes = 3ULL * GB;
    CleanPlan p4 = PlanAuto(c, m, false);
    chk(!p4.purgeLow && !p4.purgeFull, L"enough free should skip purge");

    // 真实枯竭：允许非空闲后台修剪 + 全量缓存，但仍不做页合并
    m.availPhys = 200ULL * MB; m.loadPct = 80;
    m.freeBytes = 40ULL * MB; m.standbyBytes = 2ULL * GB;
    CleanPlan pe = PlanAuto(c, m, false);
    chk(pe.trimWS && !pe.requireIdle && pe.purgeFull && !pe.purgeLow, L"severe auto escalates");
    chk(!pe.combine, L"auto never combines pages");
    CleanPlan pec = PlanAuto(c, m, true);
    chk(pec.trimWS && !pec.requireIdle && pec.purgeFull, L"severe auto ignores idle gate");
    // 高负载但仍有可用页：只做紧急修剪 + 低优先级缓存，不重复全量清空
    m.availPhys = 2ULL * GB; m.loadPct = 96;
    m.freeBytes = 2ULL * GB; m.standbyBytes = 2ULL * GB;
    CleanPlan ph = PlanAuto(c, m, false);
    chk(ph.trimWS && !ph.requireIdle && !ph.purgeFull && ph.purgeLow,
        L"high load keeps high-priority cache");

    // 手动计划
    CleanPlan pm = PlanManual(c);
    chk(pm.trimWS && pm.purgeFull && pm.sysCache && !pm.requireIdle, L"manual plan");
    chk(pm.minWSMB == 8, L"manual min ws");
    chk(pm.combine == c.combinePages, L"manual combine follows config");

    // 离开时维护：空闲后台 + 低优先级缓存 + 页合并；不清全量缓存/文件缓存
    CleanPlan pidle = PlanIdle(c);
    chk(pidle.trimWS && pidle.requireIdle && pidle.combine &&
        pidle.purgeLow && !pidle.purgeFull && !pidle.sysCache,
        L"idle maintenance plan");

    // 颠簸保护：常规压力下高硬缺页阻止缓存清理；真实枯竭仍允许
    g_hfPerSec.store(9000.0);
    m.loadPct = 80;
    m.availPhys = 4ULL * GB; m.freeBytes = 100ULL * MB; m.standbyBytes = 2ULL * GB;
    CleanPlan p5 = PlanAuto(c, m, false);
    chk(!p5.purgeLow && !p5.purgeFull, L"hf spike blocks non-emergency purge");
    m.availPhys = 200ULL * MB;
    CleanPlan p6 = PlanAuto(c, m, false);
    chk(p6.purgeFull, L"emergency purge despite hf spike");
    g_hfPerSec.store(0.0);

    // 结果统计只取正向增量
    chk(Grow(100, 40) == 60 && Grow(40, 100) == 0, L"Grow only positive delta");

    // 保护名单
    chk(IsProtectedName(L"dwm.exe"), L"builtin dwm protected");
    chk(IsProtectedName(L"qemu-system-i386"), L"VM qemu protected");
    chk(!IsProtectedName(L"claude"), L"dev app not hard-protected by default");
    chk(!IsProtectedName(L"notepad.exe"), L"notepad not protected");
    chk(TrimPid(0) == false && TrimPid(GetCurrentProcessId()) == false, L"TrimPid refuses self/idle");

    // 格式化
    chk(FormatBytes(1536ULL * MB) == L"1.5 GB", L"FormatBytes GB");
    chk(FormatInt(1234567) == L"1,234,567", L"FormatInt");

    Log(L"[selftest] %s", ok ? L"PASS" : L"FAIL");
    return ok;
}

} // namespace ms
