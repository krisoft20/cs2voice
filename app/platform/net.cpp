#include "pch.h"

#include <bcrypt.h>
#include <winhttp.h>

#include "dbg.h"
#include "platform/net.h"
#include "text.h"

namespace net {
    bool get(const std::wstring& url, std::string& body, const wchar_t* headers, progress_fn_t progress, void* user) {
        URL_COMPONENTS uc = {};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256] = {}, path[2048] = {};
        uc.lpszHostName = host;
        uc.dwHostNameLength = 256;
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = 2048;
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
            return false;
        HINTERNET session = WinHttpOpen(L"cs2voice", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        HINTERNET con = session ? WinHttpConnect(session, host, uc.nPort, 0) : nullptr;
        HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                            : nullptr;
        DWORD status = 0;
        if (req) {
            WinHttpSetTimeouts(req, 8000, 8000, 8000, 30000);
            if (WinHttpSendRequest(req, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, headers ? (DWORD)-1L : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                DWORD size = sizeof(status);
                WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                    WINHTTP_NO_HEADER_INDEX);
            }
        }
        uint64_t total = 0;
        if (status == 200) {
            wchar_t len[32] = {};
            DWORD size = sizeof(len);
            if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, len, &size, WINHTTP_NO_HEADER_INDEX))
                total = _wcstoui64(len, nullptr, 10);
        }
        bool ok = status == 200;
        while (ok) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail)) {
                ok = false;
                break;
            }
            if (!avail)
                break;
            size_t at = body.size();
            body.resize(at + avail);
            DWORD got = 0;
            if (!WinHttpReadData(req, body.data() + at, avail, &got)) {
                ok = false;
                break;
            }
            body.resize(at + got);
            if (progress)
                progress(body.size(), total, user);
        }
        if (!ok)
            dbg::print("[net] %s failed, status %lu", text::utf8(url).c_str(), status);
        if (req)
            WinHttpCloseHandle(req);
        if (con)
            WinHttpCloseHandle(con);
        if (session)
            WinHttpCloseHandle(session);
        return ok;
    }

    std::string sha256(const std::string& bytes) {
        BCRYPT_ALG_HANDLE alg = nullptr;
        unsigned char digest[32] = {};
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
            return "";
        NTSTATUS st = BCryptHash(alg, nullptr, 0, (PUCHAR)bytes.data(), (ULONG)bytes.size(), digest, sizeof(digest));
        BCryptCloseAlgorithmProvider(alg, 0);
        if (st != 0)
            return "";
        char hex[65] = {};
        for (int i = 0; i < 32; i++)
            snprintf(hex + i * 2, 3, "%02x", digest[i]);
        return hex;
    }
}
