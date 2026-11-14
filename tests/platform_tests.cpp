#include "../src/platform.hpp"
#include <iostream>
using namespace pulse;
int checks = 0;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(x)) {                                                                                          \
            std::cerr << "FAIL line " << __LINE__ << ": " #x "\n";                                           \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (0)
int main() {
    CHECK(utf8(wide("中文路径 / Codex")) == "中文路径 / Codex");
    CHECK(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(b64decode("SGVsbG8_") == "Hello?");
    CHECK(b64decode("$").empty());
    CHECK(str(jwt("x.eyJzdWIiOiJ1c2VyIn0.x"), "sub") == "user");
    auto root = fs::temp_directory_path() / (L"pulse-platform-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(root);
    auto auth = readAuth(root);
    CHECK(auth.kind == "missing");
    {
        std::ofstream f(root / L"auth.json");
        f << "{bad";
    }
    CHECK(readAuth(root).kind == "invalid");
    {
        std::ofstream f(root / L"auth.json");
        f << R"({"OPENAI_API_KEY":"not-a-real-api-key"})";
    }
    CHECK(readAuth(root).kind == "api-key");
    {
        std::ofstream f(root / L"auth.json");
        f << R"({"tokens":{"access_token":"x.eyJzdWIiOiJ1c2VyIn0.x","account_id":"test-account","id_token":"x.eyJlbWFpbCI6InRlc3RAZXhhbXBsZS5jb20ifQ.x"}})";
    }
    auth = readAuth(root);
    CHECK(auth.kind == "chatgpt");
    CHECK(auth.account == "test-account");
    CHECK(auth.label == "te***@example.com");
    CHECK(!auth.identity.empty());
    auth.token = "invalid\r\nInjected: yes";
    CHECK(!fetchUsage(auth).ok); // rejected before network I/O
    // A direct keyring entry must be decoded as UTF-16, matched to this exact home.
    fs::remove(root / L"auth.json");
    std::error_code ec;
    auto cp = fs::canonical(root, ec);
    auto target = L"cli|" + wide(sha256(utf8(cp.wstring())).substr(0, 16)) + L".Codex Auth";
    std::wstring secret =
        L"{\"tokens\":{\"access_token\":\"fake-keyring-token\",\"account_id\":\"test-keyring\"}}";
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(secret.data());
    credential.CredentialBlobSize = static_cast<DWORD>(secret.size() * 2);
    credential.Persist = CRED_PERSIST_SESSION;
    CHECK(CredWriteW(&credential, 0));
    auth = readAuth(root);
    CHECK(auth.account == "test-keyring");
    CHECK(auth.location == "Windows 凭据管理器");
    CHECK(CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0));
    // Redirect this process's HKCU before testing startup. Never modify the real Run key.
    struct StartupSandbox {
        HKEY key = nullptr;
        std::wstring path, previousLocalAppData;
        fs::path files;
        bool created = false, redirected = false;
        ~StartupSandbox() {
            if (redirected)
                RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
            if (key)
                RegCloseKey(key);
            if (created)
                RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
            SetEnvironmentVariableW(L"LOCALAPPDATA",
                                    previousLocalAppData.empty() ? nullptr : previousLocalAppData.c_str());
            std::error_code ec;
            fs::remove_all(files, ec);
        }
    } sandbox;
    sandbox.path = L"Software\\CodexPulseTests\\startup-" + std::to_wstring(GetCurrentProcessId());
    sandbox.files = root;
    sandbox.previousLocalAppData = env(L"LOCALAPPDATA");
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, sandbox.path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr,
                          &sandbox.key, nullptr) == ERROR_SUCCESS);
    sandbox.created = true;
    CHECK(RegOverridePredefKey(HKEY_CURRENT_USER, sandbox.key) == ERROR_SUCCESS);
    sandbox.redirected = true;
    CHECK(SetEnvironmentVariableW(L"LOCALAPPDATA", (root / L"preferences").c_str()));
    auto settings = loadSettings();
    CHECK(settings.autostart && !settings.autostartInitialized);
    CHECK(!autostartRegistered());
    fs::create_directories(configDir());
    {
        std::ofstream f(configDir() / L"settings.json");
        f << R"({"theme":2,"interval":300})";
    }
    settings = loadSettings();
    CHECK(settings.autostart && !settings.autostartInitialized && settings.theme == 2);
    CHECK(applyDefaultAutostart(settings) == ERROR_SUCCESS);
    CHECK(settings.autostart && settings.autostartInitialized && autostartRegistered());
    wchar_t startup[32768]{}, self[32768]{};
    DWORD length = sizeof startup;
    CHECK(RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"CodexPulse",
                       RRF_RT_REG_SZ, nullptr, startup, &length) == ERROR_SUCCESS);
    GetModuleFileNameW(nullptr, self, 32768);
    CHECK(std::wstring(startup) == L"\"" + std::wstring(self) + L"\" --tray");
    CHECK(saveSettings(settings));
    settings = loadSettings();
    CHECK(settings.autostart && settings.autostartInitialized && settings.theme == 2 &&
          settings.interval == 300);
    CHECK(setAutostartEnabled(false) == ERROR_SUCCESS);
    CHECK(!autostartRegistered());
    settings.autostart = false;
    CHECK(saveSettings(settings));
    settings = loadSettings();
    CHECK(!settings.autostart && settings.autostartInitialized);
    CHECK(applyDefaultAutostart(settings) == ERROR_SUCCESS && !autostartRegistered());
    CHECK(setAutostartEnabled(false) == ERROR_SUCCESS); // repeated disable is harmless
    CHECK(setAutostartEnabled(true) == ERROR_SUCCESS && autostartRegistered());
    settings.autostart = true;
    CHECK(saveSettings(settings));
    CHECK(setAutostartEnabled(false) == ERROR_SUCCESS); // external removal must not be undone on launch
    settings = loadSettings();
    CHECK(applyDefaultAutostart(settings) == ERROR_SUCCESS && !autostartRegistered());
    std::cout << "PASS: " << checks << " Windows platform checks\n";
    return 0;
}
