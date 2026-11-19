#pragma once

// OS keychain access (doc §6.2): secrets like the SteamGridDB API key
// belong in the platform credential store, not config.json or env vars.
// Windows → Credential Manager (generic credentials), Linux → libsecret
// via the secret-tool CLI (no link-time dep on libsecret), macOS →
// Security framework generic passwords. All keys live under the fixed
// service name "SteamLibraryScreensaver"; the `key` arg selects which
// credential (e.g. "sgdb.apiKey").

#include <optional>
#include <string>

namespace slc::platform {

/// Persists `value` under `key` in the OS credential store.
[[nodiscard]] bool storeCredential(const std::string& key,
                                   const std::string& value);

/// Reads the credential, or nullopt when absent/unavailable.
[[nodiscard]] std::optional<std::string>
loadCredential(const std::string& key);

/// Removes the credential. Returns true if it existed or the store is
/// unavailable; false only on a real delete failure.
[[nodiscard]] bool deleteCredential(const std::string& key);

} // namespace slc::platform
