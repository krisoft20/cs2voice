#include "pch.h"

#include <dbghelp.h>

#include <csignal>
#include <exception>

#include "dbg.h"
#include "platform/crash.h"
#include "text.h"

namespace crash {
    const int KEEP_REPORTS = 5;

    wchar_t g_dir[MAX_PATH];
    HANDLE g_go = nullptr;
    HANDLE g_done = nullptr;
    EXCEPTION_POINTERS* g_ep = nullptr;
    DWORD g_tid = 0;
    LONG g_hit = 0;
    char g_what[256];
    char g_task[512];
    thread_local char t_task[512];

    void put(HANDLE h, const char* fmt, ...) {
        char line[1024];
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(line, sizeof(line), fmt, args);
        va_end(args);
        if (n <= 0 || h == INVALID_HANDLE_VALUE)
            return;
        DWORD w = 0;
        WriteFile(h, line, (DWORD)std::min(n, (int)sizeof(line) - 1), &w, nullptr);
    }

    void frame(HANDLE h, HANDLE proc, uint64_t addr) {
        HMODULE mod = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &mod)) {
            put(h, "  0x%llx\n", addr);
            return;
        }
        char path[MAX_PATH] = "?";
        GetModuleFileNameA(mod, path, MAX_PATH);
        const char* name = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;
        uint64_t rva = addr - (uint64_t)mod;
        alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256] = {};
        SYMBOL_INFO* sym = (SYMBOL_INFO*)buf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 off = 0;
        if (!SymFromAddr(proc, addr, &off, sym)) {
            put(h, "  %s+0x%llx\n", name, rva);
            return;
        }
        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD col = 0;
        if (SymGetLineFromAddr64(proc, addr, &col, &line))
            put(h, "  %s+0x%llx %s+0x%llx  %s:%lu\n", name, rva, sym->Name, off, line.FileName, line.LineNumber);
        else
            put(h, "  %s+0x%llx %s+0x%llx\n", name, rva, sym->Name, off);
    }

    void write() {
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t dmp[MAX_PATH + 64], txt[MAX_PATH + 64];
        swprintf(dmp, MAX_PATH + 64, L"%s\\crash_%04d%02d%02d_%02d%02d%02d_%lu.dmp", g_dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                 GetCurrentProcessId());
        wcscpy_s(txt, dmp);
        wcscpy_s(txt + wcslen(txt) - 4, 5, L".txt");
        HANDLE proc = GetCurrentProcess();
        HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, g_tid);

        HANDLE f = CreateFileW(dmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei = {g_tid, g_ep, FALSE};
            MiniDumpWriteDump(proc, GetCurrentProcessId(), f,
                              (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithUnloadedModules), &mei, nullptr,
                              nullptr);
            CloseHandle(f);
        }

        HANDLE h = CreateFileW(txt, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        EXCEPTION_RECORD* er = g_ep->ExceptionRecord;
        put(h, "cs2voice %s\n", CS2VOICE_VERSION);
        put(h, "%s, code 0x%08lx, thread %lu\n", g_what, er->ExceptionCode, g_tid);
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
            put(h, "%s at 0x%llx\n", er->ExceptionInformation[0] == 1 ? "write" : er->ExceptionInformation[0] == 8 ? "execute" : "read",
                er->ExceptionInformation[1]);
        put(h, "task: %s\n", g_task[0] ? g_task : "-");
        put(h, "stack:\n");
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        SymInitialize(proc, nullptr, TRUE);
        CONTEXT c = *g_ep->ContextRecord;
        STACKFRAME64 s = {};
        s.AddrPC.Offset = c.Rip;
        s.AddrPC.Mode = AddrModeFlat;
        s.AddrFrame.Offset = c.Rbp;
        s.AddrFrame.Mode = AddrModeFlat;
        s.AddrStack.Offset = c.Rsp;
        s.AddrStack.Mode = AddrModeFlat;
        for (int i = 0; i < 64; i++) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &s, &c, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || !s.AddrPC.Offset)
                break;
            frame(h, proc, s.AddrPC.Offset);
        }
        SymCleanup(proc);
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
        if (thread)
            CloseHandle(thread);
    }

    DWORD WINAPI writer(void*) {
        WaitForSingleObject(g_go, INFINITE);
        write();
        SetEvent(g_done);
        return 0;
    }

    void report(EXCEPTION_POINTERS* ep, const char* what) {
        if (InterlockedExchange(&g_hit, 1))
            Sleep(INFINITE);
        g_ep = ep;
        g_tid = GetCurrentThreadId();
        snprintf(g_what, sizeof(g_what), "%s", what);
        memcpy(g_task, t_task, sizeof(g_task));
        SetEvent(g_go);
        WaitForSingleObject(g_done, 60000);
    }

    LONG WINAPI on_exception(EXCEPTION_POINTERS* ep) {
        report(ep, ep->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW ? "stack overflow" : "unhandled exception");
        return EXCEPTION_EXECUTE_HANDLER;
    }

    void fake(const char* what) {
        CONTEXT c = {};
        RtlCaptureContext(&c);
        EXCEPTION_RECORD er = {};
        er.ExceptionCode = 0xE0000001;
        er.ExceptionAddress = (void*)c.Rip;
        EXCEPTION_POINTERS ep = {&er, &c};
        report(&ep, what);
        TerminateProcess(GetCurrentProcess(), 3);
    }

    void fake_current(const char* plain) {
        char what[256];
        snprintf(what, sizeof(what), "%s", plain);
        if (std::exception_ptr e = std::current_exception()) {
            try {
                std::rethrow_exception(e);
            } catch (const std::exception& x) {
                snprintf(what, sizeof(what), "uncaught exception: %s", x.what());
            } catch (...) {
                snprintf(what, sizeof(what), "uncaught exception");
            }
        }
        fake(what);
    }

    void on_terminate() {
        fake_current("terminate");
    }

    void on_abort(int) {
        fake_current("abort");
    }

    void on_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
        fake("invalid crt parameter");
    }

    void on_purecall() {
        fake("pure virtual call");
    }

    void prune() {
        std::vector<std::wstring> found;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((std::wstring(g_dir) + L"\\crash_*.txt").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return;
        do
            found.push_back(fd.cFileName);
        while (FindNextFileW(h, &fd));
        FindClose(h);
        std::sort(found.begin(), found.end());
        dbg::print("[crash] %zu earlier reports, newest %s", found.size(), text::utf8(found.back()).c_str());
        for (size_t i = 0; i + KEEP_REPORTS < found.size(); i++) {
            std::wstring stem = std::wstring(g_dir) + L"\\" + found[i].substr(0, found[i].size() - 4);
            DeleteFileW((stem + L".txt").c_str());
            DeleteFileW((stem + L".dmp").c_str());
        }
    }

    bool init(const std::wstring& dir) {
        wcscpy_s(g_dir, dir.c_str());
        CreateDirectoryW(g_dir, nullptr);
        prune();
        g_go = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE t = g_go && g_done ? CreateThread(nullptr, 0, writer, nullptr, 0, nullptr) : nullptr;
        if (!t) {
            dbg::print("[crash] no writer thread");
            return false;
        }
        CloseHandle(t);
        SetUnhandledExceptionFilter(on_exception);
        std::set_terminate(on_terminate);
        signal(SIGABRT, on_abort);
        _set_invalid_parameter_handler(on_invalid_parameter);
        _set_purecall_handler(on_purecall);
        return true;
    }

    void set_task(const std::string& task) {
        snprintf(t_task, sizeof(t_task), "%s", task.c_str());
    }
}
