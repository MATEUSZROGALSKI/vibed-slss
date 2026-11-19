// Security-framework backend (§6.2): generic-password items under
// service "io.github.slc", account = the key name.

#include "slc/platform/credential.h"

#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>

namespace slc::platform {
namespace {

CFMutableDictionaryRef baseQuery(const std::string& key)
{
    CFMutableDictionaryRef q = CFDictionaryCreateMutable(
        nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
    auto* svc = CFStringCreateWithCString(nullptr, "io.github.slc",
                                          kCFStringEncodingUTF8);
    auto* acct = CFStringCreateWithCString(nullptr, key.c_str(),
                                           kCFStringEncodingUTF8);
    CFDictionarySetValue(q, kSecAttrService, svc);
    CFDictionarySetValue(q, kSecAttrAccount, acct);
    CFRelease(svc);
    CFRelease(acct);
    return q;
}

} // namespace

bool storeCredential(const std::string& key, const std::string& value)
{
    deleteCredential(key); // upsert
    CFMutableDictionaryRef q = baseQuery(key);
    auto* data = CFDataCreate(
        nullptr, reinterpret_cast<const UInt8*>(value.data()),
        static_cast<CFIndex>(value.size()));
    CFDictionarySetValue(q, kSecValueData, data);
    const OSStatus rc = SecItemAdd(q, nullptr);
    CFRelease(data);
    CFRelease(q);
    return rc == errSecSuccess;
}

std::optional<std::string> loadCredential(const std::string& key)
{
    CFMutableDictionaryRef q = baseQuery(key);
    CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    const OSStatus rc = SecItemCopyMatching(q, &result);
    CFRelease(q);
    if (rc != errSecSuccess || !result) {
        return std::nullopt;
    }
    auto* data = static_cast<CFDataRef>(result);
    std::string value(
        reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
        static_cast<std::size_t>(CFDataGetLength(data)));
    CFRelease(result);
    return value;
}

bool deleteCredential(const std::string& key)
{
    CFMutableDictionaryRef q = baseQuery(key);
    const OSStatus rc = SecItemDelete(q);
    CFRelease(q);
    return rc == errSecSuccess || rc == errSecItemNotFound;
}

} // namespace slc::platform
