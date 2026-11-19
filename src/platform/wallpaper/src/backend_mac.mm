// §6.1.4 macOS wallpaper backend: NSWorkspace desktop-image API sets the
// wallpaper on every screen. Compiled as Objective-C++ (.mm) — this is
// the only AppKit TU in the wallpaper module.
#include "slc/wallpaper/backend.h"

#import <AppKit/AppKit.h>

namespace slc::wallpaper {
namespace {

class MacWallpaperBackend final : public IWallpaperBackend {
  public:
    Result<void> setWallpaper(const std::filesystem::path& image) override
    {
        @autoreleasepool {
            NSString* nsPath = [NSString stringWithUTF8String:
                    image.string().c_str()];
            NSURL* url = [NSURL fileURLWithPath:nsPath];
            NSArray<NSScreen*>* screens = [NSScreen screens];
            if ([screens count] == 0) {
                return std::unexpected(
                    Error::make(ErrorCode::NotFound, "no screens"));
            }
            for (NSScreen* screen in screens) {
                NSError* err = nil;
                const BOOL ok = [[NSWorkspace sharedWorkspace]
                    setDesktopImageURL:url
                             forScreen:screen
                               options:@{}
                                 error:&err];
                if (!ok) {
                    return std::unexpected(Error::make(
                        ErrorCode::IoError,
                        "setDesktopImageURL: " +
                            std::string(err.localizedDescription
                                            .UTF8String ?: "unknown")));
                }
            }
            return {};
        }
    }

    [[nodiscard]] Result<std::filesystem::path>
    currentWallpaper() const override
    {
        @autoreleasepool {
            NSScreen* main = [NSScreen mainScreen];
            NSURL* url = main ? [[NSWorkspace sharedWorkspace]
                                 desktopImageURLForScreen:main]
                              : nil;
            if (!url || ![url isFileURL]) {
                return std::unexpected(
                    Error::make(ErrorCode::NotFound,
                                "no current wallpaper URL"));
            }
            return std::filesystem::path(url.fileSystemRepresentation);
        }
    }
};

} // namespace

std::unique_ptr<IWallpaperBackend> makeWallpaperBackend()
{
    return std::make_unique<MacWallpaperBackend>();
}

} // namespace slc::wallpaper
