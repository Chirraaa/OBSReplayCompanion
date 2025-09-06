#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QStyleFactory>
#include <QTimer>
#include <QIcon>
#include <memory>
#include "MainWindow.h"
#include "GameCapture.h"
#include "Logger.h"


// This tells the linker to create a GUI application instead of a console one.
#if defined(Q_OS_WIN) && defined(_MSC_VER)
#pragma comment(linker, "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
#endif

int main(int argc, char *argv[])
{
    // This redirects all qDebug, qWarning, etc. output to our Logger class.
    qInstallMessageHandler(messageHandler);

    QApplication app(argc, argv);

    app.setApplicationName("OBS Replay Companion");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("Chirraaa");
    app.setWindowIcon(QIcon(":/logo.ico"));
    app.setQuitOnLastWindowClosed(false);

    // Apply a consistent modern style
    app.setStyle(QStyleFactory::create("Fusion"));

    std::unique_ptr<GameCapture> capture;
    try {
        capture = std::make_unique<GameCapture>();
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, "Initialization Error",
            QString("Failed to create GameCapture component: %1").arg(e.what()));
        return 1;
    }

    std::unique_ptr<MainWindow> window;
    try {
        window = std::make_unique<MainWindow>(capture.get());
        window->show();
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, "Initialization Error",
            QString("Failed to create the main window: %1").arg(e.what()));
        return 1;
    }

    // Initialize OBS after the UI is stable to prevent blocking
    QTimer::singleShot(1500, [&]() {
        if (!capture->Initialize()) {
            QMessageBox::critical(nullptr, "OBS Initialization Failed",
                "Failed to initialize the OBS core.\n\n"
                "This may be due to a missing OBS Studio installation, "
                "outdated graphics drivers, or another application using capture resources.\n\n"
                "Please ensure OBS Studio is installed, update your drivers, "
                "and restart the application.");
            app.quit();
            return;
        }
        window->postInitRefresh();
    });

    int result = app.exec();

    // The capture object is already managed by unique_ptr,
    // but explicit shutdown can be safer.
    if (capture) {
        capture->Shutdown();
    }

    return result;
}