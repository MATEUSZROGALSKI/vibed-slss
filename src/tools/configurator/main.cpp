// Standalone Qt settings app for the Steam Library Screensaver
// (doc §5.8 configurator — invoked by /c on Windows, launched directly on
// Linux/macOS).
#include "config_window.h"

#include <QApplication>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("slc-configurator"));
    slc::configurator::ConfigWindow window;
    window.show();
    return QApplication::exec();
}
