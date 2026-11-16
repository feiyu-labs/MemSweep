// MemSweep — 核心引擎接口
#pragma once
#include "common.h"

namespace ms {

// ---------------------------------------------------------------------------
// 指标与快照
// ---------------------------------------------------------------------------
struct Metrics {
    int loadPct = 0;                       // 使用中百分比
    unsigned long long totalPhys = 0;
    unsigned long long availPhys = 0;      // 可用（空闲 + 待机）
    unsigned long long freeBytes = 0;      // 真正空闲（zero + free list）
    unsigned long long standbyBytes = 0;   // 待机缓存合计
    unsigned long long standbyByPrio[8] = {};
    unsigned long long modifiedBytes = 0;
    unsigned long long commitUsed = 0;
    unsigned long long commitLimit = 0;
    unsigned long long hardFaultsCumulative = 0;
    double hardFaultsPerSec = 0.0;
    // 压缩存储（SystemStoreInformation / MemCompressionInfoRequest）
    bool  compressionOk = false;
    unsigned long long compressedBytes = 0;    // 压缩后的物理占用
    unsigned long long compressedLogical = 0;  // 去重后的逻辑数据
    double compressionRatio = 0.0;
    // 内核池
    unsigned long long nonpagedBytes = 0;
    unsigned long long pagedBytes = 0;
    double commitPct = 0.0;
};

struct ProcInfo {
    DWORD pid = 0, ppid = 0, sessionId = 0;
    unsigned long long ws = 0, wsPrivate = 0, commit = 0, cpu100ns = 0, createTime = 0;
    std::wstring name;
};

struct Snapshot {
    Metrics m;
    std::vector<ProcInfo> procs;   // 按 WS 降序
};

// 全局共享状态
extern Metrics g_metrics;          // g_lock 保护
extern Snapshot g_snap;            // g_lock 保护（含进程列表）
extern HWND g_hwnd;                // UI 窗口（CLI 模式为 null）
extern volatile LONG g_cleaning;
extern volatile LONG g_cleanPhase; // 0 空闲 / 1 修剪 / 2 合并页
extern volatile LONG g_quit;
extern HANDLE g_monEvent;
extern std::atomic<ULONGLONG> g_lastAutoCleanMs;
extern std::atomic<ULONGLONG> g_lastFreeGain;   // 真正空闲页的净增量（用户可感知）
extern std::atomic<ULONGLONG> g_lastAvailGain;  // 可用内存净增量（含待机缓存）
extern std::atomic<ULONGLONG> g_lastCleanTickMs;
extern std::atomic<bool> g_lastCleanWasAuto;   // 上次清理是否来自自动模式
extern std::atomic<double> g_hfPerSec;
extern std::atomic<bool> g_gameMode;           // 全屏/演示模式，自动清理暂停
extern std::atomic<unsigned long long> g_reclaimableBytes; // 可回收估算（低优/全量缓存 + 空闲候选私有WS）

bool  TakeSnapshot(Snapshot& s);
bool  QueryMemoryLists(Metrics& m);
bool  QueryCompression(Metrics& m);
bool  QueryPools(Metrics& m);
void  InitCore();
void  EnablePrivileges();
void  DisablePrivileges();
bool  IsElevated();
bool  IsCurrentSession(DWORD pid);
void  RebuildWhitelist();
void  PauseAutoGuard(int minutes);
bool  AutoPaused();

// 配置线程安全：UI 线程写 g_cfg，后台线程只读不可变快照
Config GetConfigSnapshot();
void   PublishConfig(const Config& c);

// ---------------------------------------------------------------------------
// 策略（纯逻辑，可 /selftest）
// ---------------------------------------------------------------------------
struct CleanPlan {
    bool trimWS = false;
    int  minWSMB = 0;
    bool requireIdle = false;
    bool purgeLow = false;
    bool purgeFull = false;
    bool sysCache = false;
    bool combine = false;      // 仅手动/离开维护：请求内核合并重复物理页
    bool eco = false;
    bool isAuto = false;
};

// 真实压力：可用内存 < 512MB 或使用率 >= 95%。free 低通常是待机缓存，
// 不能单独作为“系统快没内存”的判据。
bool IsSeverePressure(const Metrics& m);

// 自动触发判定：使用中 >= 阈值 或 真实压力；冷却期内不触发
bool ShouldTriggerAuto(const Config& c, const Metrics& m, ULONGLONG nowMs, ULONGLONG lastAutoMs);

// 自动计划：空闲后台修剪 + 条件性低优先级缓存清理；
// 真实压力下才升级为“非空闲后台修剪 + 全量缓存”，绝不自动合并页。
CleanPlan PlanAuto(const Config& c, const Metrics& m, bool hasCandidates);
// 手动计划：全量待机 + 文件缓存 + 修剪（无空闲要求）+ 可选合并重复页
CleanPlan PlanManual(const Config& c);
// 离开时维护：只处理空闲后台 + 低优先级缓存 + 合并页；不清全量缓存/文件缓存
CleanPlan PlanIdle(const Config& c);

// 保护判定
bool IsProtectedProcess(const ProcInfo& p, const std::unordered_set<DWORD>& protectedPids);
size_t CountTrimCandidates(const Snapshot& s, bool requireIdle, int minWSMB, const Config& c);
// 可回收估算：空闲候选私有WS + 低优先级/全量缓存（随清理力度）
unsigned long long ComputeReclaimable(const Snapshot& s, const Config& c, size_t* candidatesOut = nullptr);

// ---------------------------------------------------------------------------
// 清理执行
// ---------------------------------------------------------------------------
struct CombineResult {
    bool ok = false;
    bool aborted = false;
    unsigned long long pagesCombined = 0;
    unsigned long long availBefore = 0, availAfter = 0;
    unsigned long long freeBefore = 0, freeAfter = 0;
    unsigned long long durationMs = 0;
};
bool CombinePages(CombineResult& out, DWORD timeoutMs = 60000, HANDLE externalCancel = nullptr);
bool TrimPid(DWORD pid);
bool IsProtectedName(const std::wstring& name);
bool IsBuiltinProtectedName(const std::wstring& name);

struct CleanResult {
    bool standbyLow = false, standbyFull = false, cache = false;
    bool combined = false;
    bool combineAborted = false;
    bool aborted = false;
    unsigned long long pagesCombined = 0;
    int  trimmed = 0, attempted = 0, eco = 0, candidates = 0;
    unsigned long long availBefore = 0, availAfter = 0, availGain = 0;
    unsigned long long freeBefore = 0, freeAfter = 0, freeGain = 0;
    unsigned long long standbyBefore = 0, standbyAfter = 0;
    unsigned long long durationMs = 0;
};

void RunClean(const CleanPlan& plan, const Config& c, CleanResult& out, HANDLE cancel = nullptr);
void LogCleanResult(const wchar_t* mode, size_t candidates, const CleanResult& r);
bool StartCleanAsync(bool isAuto);
void RequestManualClean(bool dryRun = false);

// ---------------------------------------------------------------------------
// 监控与自检
// ---------------------------------------------------------------------------
void MonitorTick();
DWORD WINAPI MonitorThreadProc(LPVOID);
bool SelfTest();
void DumpIdleDiagnostics();

// 焦点跟踪（UI 的 WinEvent 钩子调用）
void NoteFocus(DWORD pid);
void PruneFocusHistory(const Config& c);

// 撤销 EcoQoS / 内存优先级标记（关闭开关时调用）
void RevertBackgroundTaming();

} // namespace ms
