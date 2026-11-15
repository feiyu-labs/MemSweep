// MemSweep · 内存速清 — 入口
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
    const bool wantShow = HasArg(L"/show");

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
        DWORD pid = (DWORD)_wtoi(trimArg.c_str());
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
        bool already = AlreadyRunning();
        Log(L"/clean: alreadyRunning=%d", (int)already);
        if (already) {
            UI_ForwardClean();
            return 0;
        }
        HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\MemSweep.Instance");
        (void)mtx;
        EnsureConsole();
        if (!IsElevated()) {
#ifdef MS_DEV
            Log(L"[dev] /clean unelevated (purge will fail, trim works)");
#else
            ConPrint(L"MemSweep needs administrator privileges.\n");
            return 2;
#endif
        }
        Snapshot s;
        TakeSnapshot(s);
        Config cfg = GetConfigSnapshot();
        size_t cand = CountTrimCandidates(s, false, 8, cfg);
        CleanPlan plan = PlanManual(cfg);
        CleanResult r;
        RunClean(plan, cfg, r);
        LogCleanResult(L"单次", (size_t)r.candidates, r);
        ConPrint(L"MemSweep clean done: candidates %d, trimmed %d/%d, standby(low/full)=%d/%d, filecache=%d, combined=%d(%llu pages)\n",
                 (int)cand, r.trimmed, r.attempted,
                 (int)r.standbyLow, (int)r.standbyFull, (int)r.cache,
                 (int)r.combined, r.pagesCombined);
        ConPrint(L"free %s -> %s (%llu MB gained), available %s -> %s (%llu MB gained), %llu ms\n",
                 FormatBytes(r.freeBefore).c_str(), FormatBytes(r.freeAfter).c_str(),
                 (unsigned long long)(r.freeGain / MB),
                 FormatBytes(r.availBefore).c_str(), FormatBytes(r.availAfter).c_str(),
                 (unsigned long long)(r.availGain / MB),
                 (unsigned long long)r.durationMs);
        return 0;
    }

    // UI mode (single instance)
    bool showOnStart = !HasArg(L"/tray");
    Log(L"UI launch: cmdline=%s alreadyRunning=%d", GetCommandLineW(), (int)AlreadyRunning());
    bool already = AlreadyRunning();
    if (already) {
        if (wantExit) UI_ForwardExit();
        else UI_ForwardShow();
        return 0;
    }
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\MemSweep.Instance");
    (void)mtx;
    (void)wantShow;

    if (!IsElevated()) {
#ifdef MS_DEV
        Log(L"[dev] running unelevated (UI only, clean actions will fail)");
#else
        MessageBoxW(nullptr,
                    Tx(L"内存速清需要管理员权限才能清理内核内存。\n请右键「以管理员身份运行」。",
                       L"MemSweep requires administrator privileges.\nPlease run as administrator."),
                    L"MemSweep", MB_ICONWARNING);
        return 1;
#endif
    }
    return RunUI(hInst, showOnStart);
}
