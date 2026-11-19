// Doc section 6.1.2 — NSPrincipalClass for SteamLibraryScreensaver.saver.
//
// Cinder's macOS screensaver path is driven by two extern "C" hooks it
// looks up in the bundle: ScreenSaverSettingsMethod (runs before the app
// exists — sets up Settings + renderer) and ScreenSaverFactoryMethod
// (creates the App and binds the AppImplMacScreenSaver). Cinder supplies
// the ScreenSaverView mechanics via WindowImplCocoaScreenSaver; we subclass
// it as SLCScreenSaverView so the principal-class name is unique to this
// bundle and we can record the preview flag for the platform host.
//
// initWithFrame:isPreview: is the first message the system sends the
// principal class; it must return within 100 ms — heavy work (Steam scan)
// lives on the app's discovery thread.

#include "slc/app/iplatform_host.h"
#include "slc/app/screensaver_app.h"
#include "slc/platform/paths.h"

#include <cinder/app/AppScreenSaver.h>
#include <cinder/app/RendererGl.h>
#include <cinder/app/cocoa/AppImplMacScreenSaver.h>

#import <ScreenSaver/ScreenSaver.h>

#include <memory>
#include <vector>

namespace {

// Set by SLCScreenSaverView -initWithFrame:isPreview: — the earliest point
// the preview flag is known, before the factory creates the app/host.
BOOL gIsPreview = NO;

class MacHost final : public slc::app::IPlatformHost {
  public:
    [[nodiscard]] bool isPreviewMode() const noexcept override
    {
        return gIsPreview == YES;
    }

    // The system owns the screensaver lifecycle; nothing to signal.
    void requestExit(int /*code*/) override {}

    [[nodiscard]] std::vector<slc::platform::DisplayInfo> displays()
        const override
    {
        // The system instantiates one ScreenSaverView PER display, each
        // with a window covering exactly that screen — unlike Windows/
        // Linux where one window spans the whole virtual screen. An empty
        // list keeps buildCells on the whole-window layout so every
        // monitor composes its own independent collage; returning all
        // displays here would instead compress the desktop-wide layout
        // into each single-screen view.
        return {};
    }
};

} // namespace

@interface SLCScreenSaverView : WindowImplCocoaScreenSaver
@end

@implementation SLCScreenSaverView

- (id)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview
{
    gIsPreview = isPreview;
    return [super initWithFrame:frame isPreview:isPreview];
}

@end

extern "C" void ScreenSaverSettingsMethod(
    cinder::app::AppScreenSaver::Settings* settings)
{
    cinder::app::AppScreenSaver::callSettings<cinder::app::RendererGl>(
        settings, "SteamLibraryScreensaver");
}

extern "C" cinder::app::AppScreenSaver* ScreenSaverFactoryMethod(
    void* impl, cinder::app::AppScreenSaver::Settings* settings)
{
    slc::app::ScreensaverApp::setHostFactory(
        [] { return std::make_unique<MacHost>(); });
    return cinder::app::AppScreenSaver::main<slc::app::ScreensaverApp>(
        impl, "SteamLibraryScreensaver", settings);
}
