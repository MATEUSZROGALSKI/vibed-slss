// Credential Manager backend (§6.2): generic credentials named
// "SteamLibraryScreensaver:<key>" in the current user's vault — survives
// reboots, encrypted per-user by DPAPI, never touches disk in plaintext.

#include "slc/platform/credential.h"

#include <windows.h>
#include <wincred.h>

#include <string>

namespace slc::platform {
namespace {

std::wstring targetName(const std::string& key)
{
    std::wstring target = L"SteamLibraryScreensaver:";
    for (const char c : key) {
        target += static_cast<wchar_t>(static_cast<unsigned char>(c));
    }
    return target;
}

} // namespace

bool storeCredential(const std::string& key, const std::string& value)
{
    const std::wstring target = targetName(key);
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target.c_str());
    cred.CredentialBlobSize =
        static_cast<DWORD>(value.size() * sizeof(wchar_t));
    std::wstring wide;
    wide.reserve(value.size());
    for (const char c : value) {
        wide += static_cast<wchar_t>(static_cast<unsigned char>(c));
    }
    cred.CredentialBlob =
        reinterpret_cast<LPBYTE>(wide.data());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = const_cast<LPWSTR>(L"slc");
    return CredWriteW(&cred, 0) == TRUE;
}

std::optional<std::string> loadCredential(const std::string& key)
{
    const std::wstring target = targetName(key);
    PCREDENTIALW cred = nullptr;
    if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) != TRUE) {
        return std::nullopt;
    }
    std::string value;
    const auto* wide = reinterpret_cast<const wchar_t*>(cred->CredentialBlob);
    const std::size_t count =
        cred->CredentialBlobSize / sizeof(wchar_t);
    value.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        value += static_cast<char>(wide[i] & 0xFF);
    }
    CredFree(cred);
    return value;
}

bool deleteCredential(const std::string& key)
{
    const std::wstring target = targetName(key);
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) == TRUE ||
           GetLastError() == ERROR_NOT_FOUND;
}

} // namespace slc::platform
