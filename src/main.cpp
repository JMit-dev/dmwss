#include <QApplication>
#include <QMainWindow>
#include <QTimer>
#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QKeyEvent>
#include <QStatusBar>
#include <spdlog/spdlog.h>
#include "core/types.hpp"
#include "machine/gameboy.hpp"
#include "ui/gl_widget.hpp"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr)
        : QMainWindow(parent)
        , m_gameboy(nullptr)
        , m_gl_widget(nullptr)
        , m_frame_timer(nullptr)
        , m_joypad_state(0xFF) {

        setWindowTitle("DMWSS - Game Boy Emulator v0.1.0");
        resize(800, 720);

        // Create OpenGL widget
        m_gl_widget = new GLWidget(this);
        m_gl_widget->setMinimumSize(160 * 3, 144 * 3);
        setCentralWidget(m_gl_widget);

        // Create menu bar
        CreateMenus();

        // Create GameBoy instance
        m_gameboy = std::make_unique<GameBoy>();

        // Create frame timer (60 FPS)
        m_frame_timer = new QTimer(this);
        connect(m_frame_timer, &QTimer::timeout, this, &MainWindow::OnFrameUpdate);

        spdlog::info("DMWSS - Game Boy Emulator v0.1.0");
        spdlog::info("Application started successfully");
    }

    ~MainWindow() override = default;

protected:
    void keyPressEvent(QKeyEvent* event) override {
        UpdateJoypad(event->key(), false);
        QMainWindow::keyPressEvent(event);
    }

    void keyReleaseEvent(QKeyEvent* event) override {
        UpdateJoypad(event->key(), true);
        QMainWindow::keyReleaseEvent(event);
    }

private slots:
    void OnFileOpen() {
        QString filename = QFileDialog::getOpenFileName(
            this,
            "Open Game Boy ROM",
            "",
            "Game Boy ROMs (*.gb *.gbc);;All Files (*)"
        );

        if (filename.isEmpty()) return;

        if (m_gameboy->LoadROM(filename.toStdString())) {
            spdlog::info("ROM loaded successfully");
            m_frame_timer->start(16);  // ~60 FPS
            statusBar()->showMessage("ROM loaded: " + filename);
        } else {
            QMessageBox::critical(this, "Error", "Failed to load ROM");
        }
    }

    void OnPause() {
        if (m_frame_timer->isActive()) {
            m_frame_timer->stop();
            statusBar()->showMessage("Paused");
        } else {
            m_frame_timer->start(16);
            statusBar()->showMessage("Running");
        }
    }

    void OnReset() {
        if (m_gameboy->IsRunning()) {
            m_gameboy->Reset();
            spdlog::info("System reset");
            statusBar()->showMessage("System reset");
        }
    }

    void OnFrameUpdate() {
        if (!m_gameboy || !m_gameboy->IsRunning()) return;

        // Update joypad state
        m_gameboy->SetJoypadState(m_joypad_state);

        // Run one frame
        m_gameboy->RunFrame();

        // Update display if frame is ready
        if (m_gameboy->IsFrameReady()) {
            m_gl_widget->UpdateFramebuffer(m_gameboy->GetFramebuffer(), 160, 144);
            m_gameboy->ClearFrameReady();
        }
    }

private:
    void CreateMenus() {
        QMenu* fileMenu = menuBar()->addMenu("&File");

        QAction* openAction = fileMenu->addAction("&Open ROM...");
        openAction->setShortcut(QKeySequence::Open);
        connect(openAction, &QAction::triggered, this, &MainWindow::OnFileOpen);

        fileMenu->addSeparator();

        QAction* exitAction = fileMenu->addAction("E&xit");
        exitAction->setShortcut(QKeySequence::Quit);
        connect(exitAction, &QAction::triggered, this, &QWidget::close);

        QMenu* emulationMenu = menuBar()->addMenu("&Emulation");

        QAction* pauseAction = emulationMenu->addAction("&Pause");
        pauseAction->setShortcut(Qt::Key_P);
        connect(pauseAction, &QAction::triggered, this, &MainWindow::OnPause);

        QAction* resetAction = emulationMenu->addAction("&Reset");
        resetAction->setShortcut(Qt::Key_R);
        connect(resetAction, &QAction::triggered, this, &MainWindow::OnReset);
    }

    void UpdateJoypad(int key, bool released) {
        // Game Boy button mapping
        // Bit 0: Right/A
        // Bit 1: Left/B
        // Bit 2: Up/Select
        // Bit 3: Down/Start
        // (0 = pressed, 1 = released)

        u8 button_mask = 0;

        switch (key) {
            case Qt::Key_Right:  button_mask = 0x01; break;  // Right
            case Qt::Key_Left:   button_mask = 0x02; break;  // Left
            case Qt::Key_Up:     button_mask = 0x04; break;  // Up
            case Qt::Key_Down:   button_mask = 0x08; break;  // Down
            case Qt::Key_Z:      button_mask = 0x10; break;  // A
            case Qt::Key_X:      button_mask = 0x20; break;  // B
            case Qt::Key_Space:  button_mask = 0x40; break;  // Select
            case Qt::Key_Return: button_mask = 0x80; break;  // Start
            default: return;
        }

        if (released) {
            m_joypad_state |= button_mask;   // Set bit (released)
        } else {
            m_joypad_state &= ~button_mask;  // Clear bit (pressed)
        }
    }

    std::unique_ptr<GameBoy> m_gameboy;
    GLWidget* m_gl_widget;
    QTimer* m_frame_timer;
    u8 m_joypad_state;
};

int main(int argc, char* argv[]) {
    // Initialize logging
    spdlog::set_level(spdlog::level::info);
    spdlog::info("DMWSS - Game Boy Emulator starting...");

    // Create Qt application
    QApplication app(argc, argv);

    // Create main window
    MainWindow window;
    window.show();

    spdlog::info("Application initialized successfully");

    return app.exec();
}

#include "main.moc"
