// §6.1.4 Windows backend: IDesktopWallpaper COM with
// SystemParametersInfo(SPI_SETDESKWALLPAPER) fallback.
#include "slc/wallpaper/backend.h"

#include "slc/common/logging.h"

#include <windows.h>

#include <shobjidl.h> // IDesktopWallpaper, CLSID_DesktopWallpaper

namespace slc::wallpaper {
namespace {

class ComGuard {
  public:
    ComGuard()
    {
        m_hr = CoInitializeEx(nullptr,
                              COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    }
    ~ComGuard()
    {
        if (SUCCEEDED(m_hr)) {
            CoUninitialize();
        }
    }
    [[nodiscard]] bool ok() const { return SUCCEEDED(m_hr); }
    [[nodiscard]] HRESULT hr() const { return m_hr; }

    ComGuard(const ComGuard&) = delete;
    ComGuard& operator=(const ComGuard&) = delete;

  private:
    HRESULT m_hr = E_FAIL;
};

class WinWallpaperBackend final : public IWallpaperBackend {
  public:
    Result<void> setWallpaper(const std::filesystem::path& image) override
    {
        ComGuard com;
        if (!com.ok()) {
            return std::unexpected(Error::make(
                ErrorCode::Unsupported, "CoInitializeEx failed"));
        }

        IDesktopWallpaper* dw = nullptr;
        const HRESULT hr = CoCreateInstance(
            CLSID_DesktopWallpaper, nullptr, CLSCTX_LOCAL_SERVER,
            IID_PPV_ARGS(&dw));
        if (SUCCEEDED(hr) && dw != nullptr) {
            // nullptr monitorID => all monitors.
            const HRESULT set = dw->SetWallpaper(nullptr, image.c_str());
            dw->Release();
            if (SUCCEEDED(set)) {
                return {};
            }
            SLC_LOG_WARN("IDesktopWallpaper::SetWallpaper failed "
                         "(0x{:08x}); falling back to SPI",
                         static_cast<unsigned long>(set));
        } else {
            SLC_LOG_WARN("IDesktopWallpaper unavailable (0x{:08x}); "
                         "falling back to SPI",
                         static_cast<unsigned long>(hr));
        }

        // SPI_SETDESKWALLPAPER wants a writable buffer in some SDKs.
        std::wstring wide = image.wstring();
        if (SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, wide.data(),
                                  SPIF_UPDATEINIFILE | SPIF_SENDCHANGE)) {
            return {};
        }
        return std::unexpected(
            Error::make(ErrorCode::IoError,
                        "SystemParametersInfo(SPI_SETDESKWALLPAPER) failed"));
    }

    [[nodiscard]] Result<std::filesystem::path> currentWallpaper()
        const override
    {
        ComGuard com;
        if (!com.ok()) {
            return std::unexpected(Error::make(
                ErrorCode::Unsupported, "CoInitializeEx failed"));
        }
        IDesktopWallpaper* dw = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr,
                                       CLSCTX_LOCAL_SERVER,
                                       IID_PPV_ARGS(&dw))) &&
            dw != nullptr) {
            LPWSTR path = nullptr;
            const HRESULT got = dw->GetWallpaper(nullptr, &path);
            dw->Release();
            if (SUCCEEDED(got) && path != nullptr) {
                std::filesystem::path out(path);
                CoTaskMemFree(path);
                return out;
            }
        }

        wchar_t buf[MAX_PATH]{};
        if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, buf, 0) &&
            buf[0] != L'\0') {
            return std::filesystem::path(buf);
        }
        return std::unexpected(Error::make(
            ErrorCode::Unsupported, "cannot query current wallpaper"));
    }
};

} // namespace

std::unique_ptr<IWallpaperBackend> makeWallpaperBackend()
{
    return std::make_unique<WinWallpaperBackend>();
}

} // namespace slc::wallpaper
