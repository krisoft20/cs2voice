#include "pch.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

#include "bridge/bridge.h"
#include "bridge/jobs.h"
#include "dbg.h"
#include "platform/window.h"
#include "text.h"
#include "web/web.h"

static std::wstring arg(const wchar_t* name) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring out;
    for (int i = 1; i + 1 < argc; i++)
        if (wcscmp(argv[i], name) == 0)
            out = argv[i + 1];
    LocalFree(argv);
    return out;
}

static std::wstring data_dir() {
    std::wstring out = arg(L"--data");
    if (!out.empty()) {
        CreateDirectoryW(out.c_str(), nullptr);
        return out;
    }
    PWSTR base = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
        out = std::wstring(base) + L"\\cs2voice";
        CoTaskMemFree(base);
    }
    CreateDirectoryW(out.c_str(), nullptr);
    return out;
}

static int read_demo(const std::wstring& path, const std::wstring& out) {
    demo_file_t f;
    f.path = text::utf8(path);
    roster_t r;
    bool ok = reader::read(f.path, r);
    FILE* o = _wfopen(out.c_str(), L"wb");
    if (!o)
        return 2;
    fputs(jobs::roster_json(f, r, ok).c_str(), o);
    fclose(o);
    return ok ? 0 : 1;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 1;
    std::wstring data = data_dir();
    dbg::init(text::utf8(data) + "\\cs2voice.log");
    dbg::print("[+] cs2voice starting");

    std::wstring read = arg(L"--read");
    if (!read.empty()) {
        int rc = read_demo(read, arg(L"--out"));
        dbg::cleanup();
        CoUninitialize();
        return rc;
    }

    std::wstring after = arg(L"--after-update");
    if (!after.empty()) {
        HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)_wtoi(after.c_str()));
        if (old) {
            WaitForSingleObject(old, 20000);
            CloseHandle(old);
        }
    }

    std::wstring shot = arg(L"--screenshot");
    HANDLE single = shot.empty() ? CreateMutexW(nullptr, TRUE, L"cs2voice.single") : nullptr;
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"cs2voice", nullptr);
        if (other) {
            DWORD pid = 0;
            GetWindowThreadProcessId(other, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(other, window::WM_SHOW, 0, 0);
        }
        CloseHandle(single);
        dbg::cleanup();
        CoUninitialize();
        return 0;
    }
    if (!shot.empty())
        web::screenshot(shot, arg(L"--page"), (UINT)_wtoi(arg(L"--wait").c_str()));
    int code = 1;
    if (web::runtime() && window::init(inst, 1440, 900, !shot.empty()) && bridge::init(data) && web::init(window::handle(), arg(L"--dev"), data + (shot.empty() ? L"\\webview" : L"\\webview-shot"), data)) {
        while (window::pump())
            ;
        code = 0;
    }

    web::cleanup();
    bridge::cleanup();
    window::cleanup();
    dbg::print("[+] cs2voice closed");
    dbg::cleanup();
    if (single)
        CloseHandle(single);
    CoUninitialize();
    return code;
}
