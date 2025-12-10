#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <spdlog/spdlog.h>
#include "core/types.hpp"
#include "core/scheduler/scheduler.hpp"

int main(int argc, char* argv[]) {
    // Initialize logging
    spdlog::set_level(spdlog::level::info);
    spdlog::info("DMWSS - Game Boy Emulator starting...");

    // Test scheduler
    Scheduler scheduler;
    bool event_fired = false;

    scheduler.Schedule(Scheduler::EventType::VBLANK, 100, [&]() {
        event_fired = true;
        spdlog::info("VBlank event fired!");
    });

    scheduler.Advance(100);
    scheduler.ProcessEvents();

    if (event_fired) {
        spdlog::info("Scheduler test passed!");
    } else {
        spdlog::error("Scheduler test failed!");
    }

    // Create Qt application
    QApplication app(argc, argv);

    // Create main window
    QMainWindow window;
    window.setWindowTitle("DMWSS - Game Boy Emulator v0.1.0");
    window.resize(800, 600);

    // Add a simple label
    QLabel* label = new QLabel("DMWSS - Game Boy Emulator\n\nBuild successful!", &window);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("font-size: 18px; padding: 20px;");
    window.setCentralWidget(label);

    window.show();

    spdlog::info("Application started successfully");

    return app.exec();
}
