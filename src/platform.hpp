#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <winhttp.h>
#include <wincred.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <atomic>
#include <mutex>
#include <thread>
#include "core.hpp"

namespace pulse {
inline std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring r(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n);
    return r;
}
inline std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int n =
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n, nullptr, nullptr);
    return r;
}
inline std::wstring env(const wchar_t *key) {
    DWORD n = GetEnvironmentVariableW(key, nullptr, 0);
    if (!n || n > 32768)
        return {};
    std::wstring s(n, L'\0');
    DWORD got = GetEnvironmentVariableW(key, s.data(), n);
    s.resize(got);
    return s;
}
inline fs::path configDir() {
    auto p = env(L"LOCALAPPDATA");
    if (p.empty())
        p = env(L"USERPROFILE");
    return fs::path(p) / L"CodexPulse";
}
inline fs::path defaultHome() {
    auto p = env(L"CODEX_HOME");
    return p.empty() ? fs::path(env(L"USERPROFILE")) / L".codex" : fs::path(p);
}
struct Settings {
    std::wstring home;
    int interval = 120, theme = 0; // 0: system, 1: light, 2: dark
    bool online = true, tray = true, notify = true;
    bool autostart = true, autostartInitialized = false;
};
inline Settings loadSettings() {
    Settings s;
    auto j = parse(readSmall(configDir() / L"settings.json", 65536));
    s.home = wide(str(j, "home"));
    int i = static_cast<int>(integer(j, "interval", 120));
    s.interval = (i == 60 || i == 120 || i == 300 || i == 900) ? i : 120;
    s.theme = static_cast<int>(integer(j, "theme", 0));
    if (s.theme > 2)
        s.theme = 0;
    s.online = boolean(j, "online", true);
    s.tray = boolean(j, "tray", true);
    s.notify = boolean(j, "notify", true);
    auto startup = j.find("autostart");
    if (startup != j.end() && startup->is_boolean()) {
        s.autostart = startup->get<bool>();
        s.autostartInitialized = true;
    }
    return s;
}
inline bool saveSettings(const Settings &s) {
    auto dir = configDir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec)
        return false;
    auto tmp = dir / L"settings.tmp";
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f)
        return false;
    json j = {{"home", utf8(s.home)}, {"interval", s.interval}, {"theme", s.theme},
              {"online", s.online},   {"tray", s.tray},         {"notify", s.notify}};
    if (s.autostartInitialized)
        j["autostart"] = s.autostart;
    f << j.dump(2);
    f.close();
    if (!f)
        return false;
    return MoveFileExW(tmp.c_str(), (dir / L"settings.json").c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}
inline bool autostartRegistered() {
    wchar_t command[32768]{};
    DWORD size = sizeof command;
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        L"CodexPulse", RRF_RT_REG_SZ, nullptr, command, &size) == ERROR_SUCCESS &&
           command[0] != L'\0';
}
inline LSTATUS setAutostartEnabled(bool enabled) {
    constexpr auto path = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    HKEY key = nullptr;
    LSTATUS result = enabled ? RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                                               &key, nullptr)
                             : RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_SET_VALUE, &key);
    if (!enabled && result == ERROR_FILE_NOT_FOUND)
        return ERROR_SUCCESS;
    if (result != ERROR_SUCCESS)
        return result;
    if (enabled) {
        wchar_t executable[32768]{};
        DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length >= 32768)
            result = length ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
        else {
            std::wstring command = L"\"" + std::wstring(executable, length) + L"\" --tray";
            result =
                RegSetValueExW(key, L"CodexPulse", 0, REG_SZ, reinterpret_cast<const BYTE *>(command.c_str()),
                               static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }
    } else {
        result = RegDeleteValueW(key, L"CodexPulse");
        if (result == ERROR_FILE_NOT_FOUND)
            result = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return result;
}
inline LSTATUS applyDefaultAutostart(Settings &s) {
    if (s.autostartInitialized)
        return ERROR_SUCCESS;
    const auto result = setAutostartEnabled(s.autostart);
    s.autostart = autostartRegistered();
    s.autostartInitialized = true;
    return result;
}
inline std::string sha256(const std::string &s) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD n = 0, len = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return {};
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&len), sizeof len, &n, 0);
    std::vector<UCHAR> object(len);
    std::array<UCHAR, 32> digest{};
    std::string out;
    if (BCryptCreateHash(alg, &hash, object.data(), len, nullptr, 0, 0) >= 0) {
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char *>(s.data())),
                           static_cast<ULONG>(s.size()), 0) >= 0 &&
            BCryptFinishHash(hash, digest.data(), 32, 0) >= 0) {
            const char *hex = "0123456789abcdef";
            for (auto c : digest) {
                out += hex[c >> 4];
                out += hex[c & 15];
            }
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}
inline std::string b64decode(std::string s) {
    std::string r;
    unsigned v = 0;
    int bits = -8;
    for (unsigned char c : s) {
        int x = -1;
        if (c >= 'A' && c <= 'Z')
            x = c - 'A';
        else if (c >= 'a' && c <= 'z')
            x = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            x = c - '0' + 52;
        else if (c == '-' || c == '+')
            x = 62;
        else if (c == '_' || c == '/')
            x = 63;
        else if (c == '=')
            break;
        else
            return {};
        v = (v << 6) | static_cast<unsigned>(x);
        bits += 6;
        if (bits >= 0) {
            r += static_cast<char>((v >> bits) & 255);
            bits -= 8;
        }
    }
    return r;
}
inline json jwt(const std::string &token) {
    auto a = token.find('.'), b = token.find('.', a == std::string::npos ? 0 : a + 1);
    if (a == std::string::npos || b == std::string::npos || b - a > 65536)
        return {};
    return parse(b64decode(token.substr(a + 1, b - a - 1)));
}
struct Auth {
    std::string token, account, identity, label, kind = "missing", location;
    time_t expires = 0;
};
inline Auth readAuth(const fs::path &home) {
    Auth a;
    auto j = parse(readSmall(home / L"auth.json"));
    a.location = "auth.json";
    auto config = readSmall(home / L"config.toml", 256 * 1024);
    bool keyring = false;
    std::istringstream configLines(config);
    std::string line;
    while (std::getline(configLines, line)) {
        auto comment = line.find('#');
        if (comment != std::string::npos)
            line.resize(comment);
        auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos)
            continue;
        if (line[first] == '[')
            break; // Only root-level CLI auth mode applies.
        auto equal = line.find('=', first);
        if (equal == std::string::npos)
            continue;
        auto name = line.substr(first, equal - first);
        name.erase(name.find_last_not_of(" \t") + 1);
        if (name == "cli_auth_credentials_store")
            keyring = line.find("keyring", equal) != std::string::npos ||
                      line.find("auto", equal) != std::string::npos;
    }
    if (!j.is_object() || keyring) {
        // keyring-rs direct store: account.service; canonical path hash matches Codex.
        std::error_code ec;
        auto canonical = fs::canonical(home, ec);
        if (ec)
            canonical = home;
        std::vector<std::wstring> paths{canonical.wstring(), home.wstring()};
        auto cp = canonical.wstring();
        if (cp.rfind(L"\\\\?\\", 0) != 0)
            paths.push_back(L"\\\\?\\" + cp);
        for (auto &p : paths) {
            auto digest = sha256(utf8(p));
            if (digest.empty())
                continue;
            auto target = L"cli|" + wide(digest.substr(0, 16)) + L".Codex Auth";
            PCREDENTIALW cred = nullptr;
            if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred)) {
                if (cred->CredentialBlobSize <= 1024 * 1024) {
                    std::string bytes(reinterpret_cast<char *>(cred->CredentialBlob),
                                      cred->CredentialBlobSize);
                    auto candidate = parse(bytes);
                    if (!candidate.is_object() && cred->CredentialBlobSize % 2 == 0)
                        candidate = parse(utf8(std::wstring(reinterpret_cast<wchar_t *>(cred->CredentialBlob),
                                                            cred->CredentialBlobSize / 2)));
                    if (candidate.is_object()) {
                        j = std::move(candidate);
                        a.location = "Windows 凭据管理器";
                    }
                    SecureZeroMemory(bytes.data(), bytes.size());
                }
                CredFree(cred);
                if (a.location != "auth.json")
                    break;
            }
        }
    }
    if (!j.is_object()) {
        a.kind = fs::exists(home / L"auth.json") ? "invalid" : "missing";
        return a;
    }
    const auto &tokens = field(j, "tokens");
    a.token = str(tokens, "access_token");
    a.account = str(tokens, "account_id");
    if (a.token.empty()) {
        a.kind = !str(j, "OPENAI_API_KEY").empty() ? "api-key" : "unsupported";
        return a;
    }
    a.kind = "chatgpt";
    auto id = jwt(str(tokens, "id_token"));
    auto access = jwt(a.token);
    if (a.account.empty())
        a.account = str(field(access, "https://api.openai.com/auth"), "chatgpt_account_id");
    a.expires = static_cast<time_t>(integer(access, "exp"));
    auto email = str(id, "email", str(field(access, "https://api.openai.com/profile"), "email"));
    auto at = email.find('@');
    a.label = at != std::string::npos ? email.substr(0, std::min<size_t>(2, at)) + "***" + email.substr(at)
                                      : "已登录 ChatGPT";
    auto subject = str(access, "sub", str(id, "sub"));
    a.identity = sha256(a.account + "|" + subject + (a.account.empty() && subject.empty() ? a.token : ""));
    return a;
}
struct HttpResult {
    Quota quota;
    std::string error;
    DWORD status = 0;
    bool ok = false;
    int retryAfter = 0;
};
struct Internet {
    HINTERNET h = nullptr;
    explicit Internet(HINTERNET v = nullptr) : h(v) {}
    ~Internet() {
        if (h)
            WinHttpCloseHandle(h);
    }
    Internet(const Internet &) = delete;
    Internet &operator=(const Internet &) = delete;
};
inline HttpResult fetchUsage(const Auth &a) {
    HttpResult r;
    auto safe = [](const std::string &s) {
        return s.size() < 32768 &&
               std::all_of(s.begin(), s.end(), [](unsigned char c) { return c >= 32 && c < 127; });
    };
    if (!safe(a.token) || !safe(a.account)) {
        r.error = "登录凭据格式异常，请重新登录 Codex。";
        return r;
    }
    Internet session(WinHttpOpen(L"CodexPulse/1.0.7", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) {
        r.error = "无法初始化 Windows 网络服务。";
        return r;
    }
    WinHttpSetTimeouts(session.h, 5000, 5000, 5000, 8000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
    Internet conn(WinHttpConnect(session.h, L"chatgpt.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    Internet req(conn.h ? WinHttpOpenRequest(conn.h, L"GET", L"/backend-api/wham/usage", nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE)
                        : nullptr);
    if (!req.h) {
        r.error = "无法创建额度请求。";
        return r;
    }
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(req.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect);
    std::wstring headers = L"Authorization: Bearer " + wide(a.token) + L"\r\nAccept: application/json\r\n";
    if (!a.account.empty())
        headers += L"ChatGPT-Account-Id: " + wide(a.account) + L"\r\n";
    BOOL sent = WinHttpSendRequest(req.h, headers.c_str(), static_cast<DWORD>(headers.size()),
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    SecureZeroMemory(headers.data(), headers.size() * sizeof(wchar_t));
    if (!sent || !WinHttpReceiveResponse(req.h, nullptr)) {
        r.error =
            "网络连接失败（Windows 错误 " + std::to_string(GetLastError()) + "）。请检查网络或系统代理。";
        return r;
    }
    DWORD n = sizeof r.status;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &r.status, &n, WINHTTP_NO_HEADER_INDEX);
    if (r.status != 200) {
        if (r.status == 401)
            r.error = "登录已过期，请打开 Codex 重新登录，再刷新。";
        else if (r.status == 403)
            r.error = "服务拒绝访问（403），请检查账户权限或网络。";
        else if (r.status == 429) {
            r.error = "请求过于频繁，已延后刷新。";
            r.retryAfter = 300;
            wchar_t b[64]{};
            DWORD size = sizeof b;
            if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CUSTOM, L"Retry-After", b, &size,
                                    WINHTTP_NO_HEADER_INDEX))
                r.retryAfter = std::clamp(_wtoi(b), 60, 3600);
        } else
            r.error = "额度服务暂不可用（HTTP " + std::to_string(r.status) + "）。";
        return r;
    }
    std::string body;
    std::array<char, 8192> buf;
    ULONGLONG deadline = GetTickCount64() + 12000;
    for (;;) {
        DWORD read = 0;
        if (GetTickCount64() > deadline) {
            r.error = "额度响应超时。";
            return r;
        }
        if (!WinHttpReadData(req.h, buf.data(), static_cast<DWORD>(buf.size()), &read)) {
            r.error = "读取额度响应失败。";
            return r;
        }
        if (!read)
            break;
        body.append(buf.data(), read);
        if (body.size() > 1024 * 1024) {
            r.error = "额度响应超出大小限制。";
            return r;
        }
    }
    auto j = parse(body);
    if (!j.is_object()) {
        r.error = "服务返回了无法识别的数据，请稍后重试。";
        return r;
    }
    auto returnedAccount = str(j, "account_id", str(j, "accountId"));
    if (!returnedAccount.empty() && !a.account.empty() && returnedAccount != a.account) {
        r.error = "返回的账户与当前登录不一致，已忽略该结果。";
        return r;
    }
    r.quota = parseQuota(j);
    r.ok = r.quota.usable();
    if (!r.ok)
        r.error = "此账户未返回可用额度窗口；本地统计仍可查看。";
    return r;
}
inline std::wstring timestamp(time_t t, bool date = true) {
    if (!t)
        return L"未提供";
    auto tm = localTime(t);
    wchar_t b[80];
    std::wcsftime(b, 80, date ? L"%m-%d %H:%M" : L"%H:%M:%S", &tm);
    return b;
}
inline std::wstring count(int64_t v) {
    auto s = std::to_wstring(v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
        s.insert(static_cast<size_t>(i), L",");
    return s;
}
inline std::wstring shortCount(int64_t v) {
    if (v < 1000)
        return count(v);
    const int64_t units[] = {1000000000000LL, 1000000000LL, 1000000LL, 1000LL};
    const wchar_t *suffixes[] = {L"T", L"B", L"M", L"K"};
    size_t unit = 3;
    for (size_t i = 0; i < 4; ++i)
        if (v >= units[i]) {
            unit = i;
            break;
        }
    double scaled = static_cast<double>(v) / static_cast<double>(units[unit]);
    if (scaled >= 999.95 && unit > 0) {
        --unit;
        scaled = static_cast<double>(v) / static_cast<double>(units[unit]);
    }
    wchar_t b[64];
    swprintf(b, 64, scaled >= 100 ? L"%.1f %ls" : L"%.2f %ls", scaled, suffixes[unit]);
    return b;
}
inline std::wstring percent(double p, int precision = 0) {
    if (p < 0)
        return L"—";
    wchar_t b[40];
    swprintf(b, 40, L"%.*f%%", std::clamp(precision, 0, 2), p);
    return b;
}
inline std::wstring duration(int64_t s) {
    if (s <= 0)
        return L"等待刷新确认";
    int64_t m = (s + 59) / 60;
    return m >= 1440 ? std::to_wstring(m / 1440) + L" 天 " + std::to_wstring(m % 1440 / 60) + L" 小时"
           : m >= 60 ? std::to_wstring(m / 60) + L" 小时 " + std::to_wstring(m % 60) + L" 分"
                     : std::to_wstring(m) + L" 分钟";
}
inline std::wstring windowName(const Window &w) {
    if (w.name == "主要额度" || w.name == "周期额度") {
        if (w.seconds == 18000)
            return L"5 小时额度";
        if (w.seconds == 604800)
            return L"每周额度";
        if (w.seconds > 0)
            return std::to_wstring(w.seconds / 60) + L" 分钟额度";
    }
    return wide(w.name);
}
} // namespace pulse
