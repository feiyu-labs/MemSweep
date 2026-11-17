// MemSweep — 入口
#include "core.h"
#include "ui.h"

using namespace ms;

static HANDLE g_con = INVALID_HANDLE_VALUE;

static void EnsureConsole() {
    if (g_con != INVALID_HANDLE_VALUE) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
    g_con = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ,
                        nullptr, OPEN_EXISTING, 0, nullptr);
}
static void ConPrint(const wchar_t* fmt, ...) {
    if (g_con == INVALID_HANDLE_VALUE) return;
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 2047, fmt, ap);
    va_end(ap);
    buf[2047] = 0;
    DWORD wr = 0;
    WriteConsoleW(g_con, buf, (DWORD)wcslen(buf), &wr, nullptr);
}

static bool AlreadyRunning() {
    HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\MemSweep.Instance");
    if (h) { CloseHandle(h); return true; }
    return false;
}

static bool FindArgValue(const wchar_t* a, std::wstring& out) {
    int n = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &n);
    bool found = false;
    if (argv) {
        for (int i = 1; i + 1 < n; i++) {
            if (_wcsicmp(argv[i], a) == 0) { out = argv[i + 1]; found = true; break; }
        }
        LocalFree(argv);
    }
    return found;
}

static bool HasArg(const wchar_t* a) {
    int n = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &n);
    bool found = false;
    if (argv) {
        for (int i = 1; i < n; i++)
            if (_wcsicmp(argv[i], a) == 0) found = true;
        LocalFree(argv);
    }
    return found;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    InitCore();

    const bool selftest = HasArg(L"/selftest");
    const bool uipng    = HasArg(L"/uipng");
    const bool checkpng = HasArg(L"/checkpng");
    const bool idlecheck = HasArg(L"/idlecheck");
    const bool pauseCmd  = HasArg(L"/pause");
    const bool resumeCmd = HasArg(L"/resume");
    const bool combineCmd = HasArg(L"/combine");
    std::wstring trimArg;
    const bool trimCmd = FindArgValue(L"/trimpid", trimArg);
    const bool dryrun   = HasArg(L"/dryrun");
    const bool oneShot  = HasArg(L"/clean");
    const bool wantExit = HasArg(L"/exit");

    if (selftest) {
        EnsureConsole();
        bool ok = SelfTest();
        ConPrint(ok ? L"MemSweep selftest: PASS\n" : L"MemSweep selftest: FAIL (see log)\n");
        return ok ? 0 : 1;
    }
    if (uipng) {
        EnsureConsole();
        CreateDirectoryW(ConfigDir().c_str(), nullptr);
        std::wstring out = ConfigDir() + L"\\ui.png";
        int rc = RenderUISnapshot(out);
        ConPrint(L"UI snapshot -> %s (rc=%d)\n", out.c_str(), rc);
        return rc;
    }
    if (checkpng) {
        EnsureConsole();
        CreateDirectoryW(ConfigDir().c_str(), nullptr);
        std::wstring out = ConfigDir() + L"\\syscheck.png";
        int rc = RenderSysCheckSnapshot(out);
        ConPrint(L"System check snapshot -> %s (rc=%d)\n", out.c_str(), rc);
        return rc;
    }
    if (combineCmd) {
        EnsureConsole();
        if (AlreadyRunning()) {
            UI_ForwardCombine();
            ConPrint(L"deep combine requested\n");
        } else {
            CombineResult r;
            CombinePages(r);
            ConPrint(L"combine ok=%d pages=%llu free %llu -> %llu MB, avail %llu -> %llu MB, time=%llu ms\n",
                     (int)r.ok, r.pagesCombined, r.freeBefore / MB, r.freeAfter / MB,
                     r.availBefore / MB, r.availAfter / MB, r.durationMs);
        }
        return 0;
    }
    if (trimCmd) {
        EnsureConsole();
        wchar_t* end = nullptr;
        unsigned long v = wcstoul(trimArg.c_str(), &end, 10);
        if (!end || *end != 0 || v <= 4 || v > 0xFFFFFFFFul) {
            ConPrint(L"Invalid PID: %s\n", trimArg.c_str());
            return 1;
        }
        DWORD pid = (DWORD)v;
        bool ok = TrimPid(pid);
        ConPrint(L"trim pid=%u ok=%d\n", pid, (int)ok);
        return ok ? 0 : 1;
    }
    if (pauseCmd || resumeCmd) {
        EnsureConsole();
        if (AlreadyRunning()) {
            UI_ForwardPause(pauseCmd ? 60 : 0);
            ConPrint(pauseCmd ? L"auto guard paused 60 min\n" : L"auto guard resumed\n");
        } else {
            ConPrint(L"MemSweep is not running\n");
        }
        return 0;
    }
    if (idlecheck) {
        EnsureConsole();
        DumpIdleDiagnostics();
        ConPrint(L"idle diagnosis written to: %s\n", LogPath().c_str());
        return 0;
    }
    if (dryrun) {
        EnsureConsole();
        ConPrint(L"MemSweep dry-run (no changes)\n");
        RequestManualClean(true);
        ConPrint(L"details written to: %s\n", LogPath().c_str());
        return 0;
    }
    if (oneShot) {
        // 先抢占实例互斥量；若已存在则转发给运行中 UI，否则一直持有到清理完成，
        // 避免并发 /clean 或启动过程中发生清理重入。
        HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\MemSweep.Instance");
        bool heldByOther = (!mtx || GetLastError() == ERROR_ALREADY_EXISTS);
        Log(L"/clean: heldByOther=%d", (int)heldByOther);
        if (heldByOther) {
            if (mtx) CloseHandle(mtx);
            UI_ForwardClean();
            return 0;
        }
        EnsureConsole();
        if (!IsElevated()) {
#ifdef MS_DEV
            Log(L"[dev] /clean unelevated (purge will fail, trim works)");
#else
            ConPrint(L"MemSweep needs administrator privileges.\n");
            ReleaseMutex(mtx);
            CloseHandle(mtx);
            return 2;
#endif
        }
        Config cfg = GetConfigSnapshot();
        CleanPlan plan = PlanManual(cfg);
        CleanResult r;
        RunClean(plan, cfg, r);
        LogCleanResult(L"单次", (size_t)r.candidates, r);
        ConPrint(L"MemSweep: freed %s, %s available (%llu ms)\n",
                 FormatBytes(r.freeGain > r.availGain ? r.freeGain : r.availGain).c_str(),
                 FormatBytes(r.availAfter).c_str(), (unsigned long long)r.durationMs);
        ConPrint(L"  trimmed %d/%d background apps, standby low/full %d/%d, file cache %d, combined %d (%llu pages)\n",
                 r.trimmed, r.attempted,
                 (int)r.standbyLow, (int)r.standbyFull, (int)r.cache,
                 (int)r.combined, r.pagesCombined);
        ReleaseMutex(mtx);
        CloseHandle(mtx);
        return 0;
    }

    // UI mode (single instance)
    bool showOnStart = !HasArg(L"/tray");
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\MemSweep.Instance");
    if (!mtx || GetLastError() == ERROR_ALREADY_EXISTS) {
        // 以创建结果为准，消除“先查后建”的窗口：两个实例同时启动只有一个能进来
        if (mtx) CloseHandle(mtx);
        Log(L"UI launch forwarded to running instance: cmdline=%s", GetCommandLineW());
        if (wantExit) UI_ForwardExit();
        else UI_ForwardShow();
        return 0;
    }
    Log(L"UI launch: cmdline=%s", GetCommandLineW());

    if (wantExit) {
        // 没有正在运行的实例时 /exit 只表示“确保已退出”，不能顺手把窗口开起来
        ReleaseMutex(mtx);
        CloseHandle(mtx);
        return 0;
    }

    if (!IsElevated()) {
#ifdef MS_DEV
        Log(L"[dev] running unelevated (UI only, clean actions will fail)");
#else
        MessageBoxW(nullptr,
                    Tx(L"MemSweep 需要管理员权限才能清理内存。\n请右键选择「以管理员身份运行」。",
                       L"MemSweep requires administrator privileges.\nPlease run it as administrator."),
                    L"MemSweep", MB_ICONWARNING);
        ReleaseMutex(mtx);
        CloseHandle(mtx);
        return 1;
#endif
    }
    int uiRc = RunUI(hInst, showOnStart);
    ReleaseMutex(mtx);
    CloseHandle(mtx);
    return uiRc;
}
