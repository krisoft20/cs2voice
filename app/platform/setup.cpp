#include "pch.h"

#include <shlobj.h>
#include <shobjidl.h>

#include "dbg.h"
#include "platform/setup.h"
#include "text.h"

namespace setup {
    const wchar_t* UNINSTALL_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\cs2voice";
    const wchar_t* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    std::wstring known(REFKNOWNFOLDERID id) {
        PWSTR p = nullptr;
        std::wstring out;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)))
            out = p;
        CoTaskMemFree(p);
        return out;
    }

    std::wstring dir() {
        return known(FOLDERID_UserProgramFiles) + L"\\cs2voice";
    }

    std::wstring target() {
        return dir() + L"\\cs2voice.exe";
    }

    std::wstring start_link() {
        return known(FOLDERID_Programs) + L"\\cs2voice.lnk";
    }

    std::wstring desktop_link() {
        return known(FOLDERID_Desktop) + L"\\cs2voice.lnk";
    }

    std::wstring exe_path() {
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return std::wstring(buf, n);
    }

    bool exists(const std::wstring& path) {
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    bool link(const std::wstring& at, const std::wstring& exe) {
        IShellLinkW* sl = nullptr;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl))))
            return false;
        sl->SetPath(exe.c_str());
        sl->SetWorkingDirectory(dir().c_str());
        sl->SetIconLocation(exe.c_str(), 0);
        sl->SetDescription(L"cs2voice");
        IPersistFile* pf = nullptr;
        bool ok = SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf))) && SUCCEEDED(pf->Save(at.c_str(), TRUE));
        if (pf)
            pf->Release();
        sl->Release();
        return ok;
    }

    void put(HKEY k, const wchar_t* name, const std::wstring& v) {
        RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    }

    void put(HKEY k, const wchar_t* name, DWORD v) {
        RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    }

    bool registered() {
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, UNINSTALL_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS)
            return false;
        RegCloseKey(k);
        return true;
    }

    void register_app() {
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, UNINSTALL_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) {
            dbg::print("[setup] cant write the uninstall key");
            return;
        }
        std::wstring exe = target();
        WIN32_FILE_ATTRIBUTE_DATA fa;
        DWORD kb = 0;
        if (GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &fa))
            kb = (DWORD)((((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow) / 1024);
        put(k, L"DisplayName", L"cs2voice");
        put(k, L"DisplayVersion", text::wide(CS2VOICE_VERSION));
        put(k, L"Publisher", L"krisoft20");
        put(k, L"DisplayIcon", exe + L",0");
        put(k, L"InstallLocation", dir());
        put(k, L"UninstallString", L"\"" + exe + L"\" --uninstall");
        put(k, L"URLInfoAbout", L"https://github.com/krisoft20/cs2voice");
        put(k, L"EstimatedSize", kb);
        put(k, L"NoModify", 1u);
        put(k, L"NoRepair", 1u);
        RegCloseKey(k);
    }

    setup_t state() {
        setup_t s;
        s.dir = dir();
        s.installed = registered() && exists(target());
        s.here = _wcsicmp(exe_path().c_str(), target().c_str()) == 0;
        s.desktop = exists(desktop_link());
        s.autostart = RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, L"cs2voice", RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        return s;
    }

    bool install(bool desktop, std::string& err) {
        std::wstring d = dir(), exe = target(), self = exe_path();
        if (d.size() < 12) {
            err = "no programs folder";
            return false;
        }
        SHCreateDirectoryExW(nullptr, d.c_str(), nullptr);
        if (_wcsicmp(self.c_str(), exe.c_str()) != 0 && !CopyFileW(self.c_str(), exe.c_str(), FALSE)) {
            dbg::print("[setup] copy failed %lu", GetLastError());
            err = "cant copy the exe";
            return false;
        }
        if (!link(start_link(), exe)) {
            err = "cant create the start menu shortcut";
            return false;
        }
        if (desktop)
            link(desktop_link(), exe);
        register_app();
        dbg::print("[setup] installed to %s", text::utf8(d).c_str());
        return true;
    }

    bool start_installed() {
        std::wstring exe = target(), d = dir();
        std::wstring cmd = L"\"" + exe + L"\" --after-update " + std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, d.c_str(), &si, &pi))
            return false;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    void set_desktop(bool on) {
        if (on)
            link(desktop_link(), target());
        else
            DeleteFileW(desktop_link().c_str());
    }

    void set_autostart(bool on) {
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
            return;
        if (on)
            put(k, L"cs2voice", L"\"" + target() + L"\" --tray");
        else
            RegDeleteValueW(k, L"cs2voice");
        RegCloseKey(k);
    }

    void uninstall(bool data, const std::wstring& data_dir) {
        set_autostart(false);
        DeleteFileW(start_link().c_str());
        DeleteFileW(desktop_link().c_str());
        RegDeleteKeyW(HKEY_CURRENT_USER, UNINSTALL_KEY);
        std::wstring cmd = L"cmd.exe /c ping -n 6 127.0.0.1 >nul & rmdir /s /q \"" + dir() + L"\"";
        if (data && data_dir.size() > 12 && data_dir.ends_with(L"\\cs2voice"))
            cmd += L" & rmdir /s /q \"" + data_dir + L"\"";
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, tmp, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        dbg::print("[setup] uninstalled%s", data ? " with data" : "");
    }

    void refresh() {
        setup_t s = state();
        if (s.installed && s.here)
            register_app();
    }
}
