// §6.1.3 Linux host — Qt session daemon driving the Cinder render session.
//
// Modes:
//   (none) / daemon   -> DBus IdleWatcher; spawns "self --run" on activation
//   --run             -> one fullscreen GL session (internal; Cinder-driven)
//   --xss             -> XScreenSaver hack mode ($XSCREENSAVER_WINDOW)
//   --list-artwork    -> dev diagnostic: run discovery, print results
//   --idle-sec N      -> daemon: also activate when session idle >= N sec
//
// The daemon itself never shows a window: the GL session runs in a child
// process so the Qt loop and Cinder's GLFW loop each own their main thread.

#include "idle_watcher.h"
#include "session.h"

#include "slc/common/env.h"
#include "slc/common/logging.h"
#include "slc/config/app_config.h"
#include "slc/content/content_service.h"
#include "slc/content/steam_local_provider.h"
#include "slc/platform/paths.h"

#include <QCoreApplication>
#include <QProcess>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

enum class Mode { Daemon, Run, Xss, Wallpaper, ListArtwork, Help };

struct Args {
    Mode mode = Mode::Daemon;
    quint32 idleSec = 0;
};

Args parseArgs(int argc, char** argv)
{
    Args out;
    for (int i = 1; i < argc; ++i) {
        const std::string_view t = argv[i];
        if (t == "--run" || t == "--once") {
            out.mode = Mode::Run;
        } else if (t == "--xss") {
            out.mode = Mode::Xss;
        } else if (t == "--wallpaper") {
            out.mode = Mode::Wallpaper;
        } else if (t == "daemon") {
            out.mode = Mode::Daemon;
        } else if (t == "--list-artwork") {
            out.mode = Mode::ListArtwork;
        } else if (t == "--idle-sec" && i + 1 < argc) {
            out.idleSec = static_cast<quint32>(std::strtoul(argv[++i], nullptr, 10));
        } else if (t == "--help" || t == "-h") {
            out.mode = Mode::Help;
        }
    }
    return out;
}

} // namespace

int diagnoseArtwork()
{
    slc::content::ContentService service;
    service.addProvider(std::make_unique<slc::content::SteamLocalProvider>());
    slc::content::DiscoveryContext ctx;

    auto cfg = slc::config::loadConfig(slc::config::defaultConfigPath());
    ctx.steamRootOverride = cfg.content.steamRoot;
    ctx.excludeAppIds = cfg.content.excludeAppIds;
    for (const auto& kind : cfg.content.includeKinds) {
        if (auto k = slc::content::artworkKindFromString(kind)) {
            ctx.includeKinds.insert(*k);
        }
    }
    ctx.minImageWidth = cfg.content.minImageWidth;

    auto result = service.collect(ctx);
    if (!result) {
        std::fprintf(stderr, "discovery failed: %s\n", result.error().message.c_str());
        return 1;
    }
    for (const auto& a : *result) {
        std::printf("%-8s %-7s %-40s %s\n", a.appId.c_str(),
                    std::string(slc::content::toString(a.kind)).c_str(),
                    a.gameName.c_str(), a.path.filename().string().c_str());
    }
    std::printf("%zu artworks\n", result->size());
    return 0;
}

int main(int argc, char** argv)
{
    slc::logging::initFileLogging(slc::platform::logDir());
    const Args args = parseArgs(argc, argv);
    SLC_LOG_INFO("slc_host_linux start, mode={}", static_cast<int>(args.mode));

    switch (args.mode) {
    case Mode::Help:
        std::fputs("steam-library-screensaver (linux host)\n"
                   "  daemon          session daemon (default)\n"
                   "  --run           one fullscreen session\n"
                   "  --wallpaper     live wallpaper session\n"
                   "  --xss           xscreensaver hack mode\n"
                   "  --idle-sec N    daemon: idle-time activation threshold\n"
                   "  --list-artwork  print discovered artwork\n",
                   stdout);
        return 0;

    case Mode::ListArtwork:
        return diagnoseArtwork();

    case Mode::Run:
        return runScreensaverSession();

    case Mode::Wallpaper:
        return runWallpaperSession();

    case Mode::Xss: {
        const char* win = std::getenv("XSCREENSAVER_WINDOW");
        if (win == nullptr || *win == '\0') {
            std::fputs("error: XSCREENSAVER_WINDOW not set\n", stderr);
            return 2;
        }
        const unsigned long xid = std::strtoul(win, nullptr, 0);
        if (xid == 0) {
            SLC_LOG_ERROR("xss mode: bad XSCREENSAVER_WINDOW '{}'", win);
            std::fputs("error: XSCREENSAVER_WINDOW is not a valid XID\n",
                       stderr);
            return 2;
        }
        return runXssSession(xid);
    }

    case Mode::Daemon:
        break;
    }

    // --- daemon mode ---
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("slc-screensaverd"));

    slc::host::IdleWatcher watcher;
    watcher.setPollThresholdSeconds(args.idleSec);

    QProcess session;
    session.setProgram(QCoreApplication::applicationFilePath());
    session.setArguments({QStringLiteral("--run")});

    QObject::connect(&watcher, &slc::host::IdleWatcher::activated, &app, [&] {
        if (session.state() == QProcess::Running) {
            return; // already showing
        }
        SLC_LOG_INFO("daemon: idle activation -> spawning --run session");
        session.start();
    });
    QObject::connect(&session, &QProcess::errorOccurred, &app,
                     [](QProcess::ProcessError e) {
                         SLC_LOG_ERROR("daemon: session spawn failed ({})",
                                       static_cast<int>(e));
                     });

    watcher.start();

    // Test hook: bound the daemon's life for CI smoke runs.
    if (const char* ms = std::getenv("SLC_TEST_IDLE_TIMEOUT_MS")) {
        QTimer::singleShot(static_cast<int>(std::strtoul(ms, nullptr, 10)),
                           &app, &QCoreApplication::quit);
    }

    const int rc = app.exec();
    if (session.state() == QProcess::Running) {
        session.terminate();
        session.waitForFinished(3000);
    }
    return rc;
}
