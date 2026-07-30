#include <QApplication>
#include <QMainWindow>
#include <QTimer>
#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QKeyEvent>
#include <QStatusBar>
#include <QSlider>
#include <QWidgetAction>
#include <QActionGroup>
#include <QImage>
#include <QSettings>
#include <QFile>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <array>
#include <spdlog/spdlog.h>
#include "core/types.hpp"
#include "machine/gameboy.hpp"
#include "ui/gl_widget.hpp"
#include "ui/audio_output.hpp"
#include "ui/settings_dialog.hpp"
#include "ui/cheats_dialog.hpp"
#include "ui/serial_link.hpp"
#include <QInputDialog>
#include <QLineEdit>
#ifdef Q_OS_ANDROID
#include "ui/touch_controls.hpp"
#endif

// Framebuffer byte order is R,G,B,A, so as a little-endian u32: 0xAABBGGRR
static constexpr u32 RGBA(u8 r, u8 g, u8 b) {
    return 0xFF000000u | (static_cast<u32>(b) << 16) | (static_cast<u32>(g) << 8) | r;
}

struct DisplayPalette {
    const char* name;
    std::array<u32, 4> colors;  // Lightest to darkest
};

static const DisplayPalette PALETTES[] = {
    {"Grayscale",     {RGBA(0xFF, 0xFF, 0xFF), RGBA(0xAA, 0xAA, 0xAA),
                       RGBA(0x55, 0x55, 0x55), RGBA(0x00, 0x00, 0x00)}},
    {"Classic Green", {RGBA(0xE0, 0xF8, 0xD0), RGBA(0x88, 0xC0, 0x70),
                       RGBA(0x34, 0x68, 0x56), RGBA(0x08, 0x18, 0x20)}},
    {"Pocket",        {RGBA(0xC4, 0xCF, 0xA1), RGBA(0x8B, 0x95, 0x6D),
                       RGBA(0x4D, 0x53, 0x3C), RGBA(0x1F, 0x1F, 0x1F)}},
    {"Amber",         {RGBA(0xFF, 0xCC, 0x66), RGBA(0xCC, 0x92, 0x33),
                       RGBA(0x88, 0x55, 0x11), RGBA(0x22, 0x11, 0x00)}},
};
static constexpr int PALETTE_COUNT = 4;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr)
        : QMainWindow(parent)
        , m_gameboy(nullptr)
        , m_gl_widget(nullptr)
        , m_frame_timer(nullptr)
        , m_joypad_state(0xFF)
        , m_current_slot(1) {

        setWindowTitle("DMWSS - Game Boy Emulator v1.0.0");
        resize(800, 720);

        // Create OpenGL widget
        m_gl_widget = new GLWidget(this);
        m_gl_widget->setMinimumSize(160 * 3, 144 * 3);
        setCentralWidget(m_gl_widget);

#ifdef Q_OS_ANDROID
        // Touch controls overlay the game display; there is no keyboard
        // to bind, so unlike desktop this is not user-configurable
        m_touch_controls = new TouchControls(this);
        m_touch_controls->raise();
#endif

        // Create GameBoy instance
        m_gameboy = std::make_unique<GameBoy>();

        // Optional boot ROMs next to the executable: with these present,
        // games start with the authentic logo scroll
        for (const char* name : {"dmg_boot.bin", "cgb_boot.bin"}) {
            QFile boot(QCoreApplication::applicationDirPath() + "/" + name);
            if (boot.open(QIODevice::ReadOnly)) {
                QByteArray data = boot.readAll();
                m_gameboy->SetBootROM(std::vector<u8>(data.begin(), data.end()));
            }
        }

        // Create audio output
        m_audio = std::make_unique<AudioOutput>(m_gameboy->GetAPU());
        m_audio->Start();

        // Link cable over TCP
        m_serial_link = new SerialLink(m_gameboy->GetSerial(), this);
        connect(m_serial_link, &SerialLink::StatusChanged, this,
                [this](const QString& status) { statusBar()->showMessage(status, 5000); });

        CreateMenus();
        LoadSettings();

        // Create frame timer (60 FPS at 1x speed)
        m_frame_timer = new QTimer(this);
        connect(m_frame_timer, &QTimer::timeout, this, &MainWindow::OnFrameUpdate);

        spdlog::info("DMWSS - Game Boy Emulator v1.0.0");
        spdlog::info("Application started successfully");
    }

    ~MainWindow() override {
        SaveCheats();
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        UpdateJoypad(event->key(), false);
        QMainWindow::keyPressEvent(event);
    }

    void keyReleaseEvent(QKeyEvent* event) override {
        UpdateJoypad(event->key(), true);
        QMainWindow::keyReleaseEvent(event);
    }

#ifdef Q_OS_ANDROID
    void resizeEvent(QResizeEvent* event) override {
        QMainWindow::resizeEvent(event);
        m_touch_controls->setGeometry(m_gl_widget->geometry());
        m_touch_controls->raise();
    }
#endif

private slots:
    void OnFileOpen() {
        QString filename = QFileDialog::getOpenFileName(
            this,
            "Open Game Boy ROM",
            "",
            "Game Boy ROMs (*.gb *.gbc);;All Files (*)"
        );

        if (filename.isEmpty()) return;

#ifdef Q_OS_ANDROID
        // The Android picker returns a content:// URI, which is readable
        // through Qt's file engine but has no writable sibling directory
        // for .sav/.state files (and often isn't a stable path at all).
        // Copy the ROM into app-private storage - always fully readable
        // and writable, no permission prompts - and load from there so
        // every existing sibling-file save path keeps working unchanged.
        QString local_path = CopyROMToLocalStorage(filename);
        if (local_path.isEmpty()) {
            QMessageBox::critical(this, "Error", "Failed to import ROM");
            return;
        }
        filename = local_path;
#endif

        // Persist the previous game's cheats before they are cleared
        SaveCheats();

        if (m_gameboy->LoadROM(filename.toStdString())) {
            spdlog::info("ROM loaded successfully");
            m_current_slot = 1;
            // LoadROM defaults DMG games to GBC colorization; a manually
            // chosen palette wins over it
            int palette = m_settings.value("video/palette", -1).toInt();
            if (palette >= 0 && palette < PALETTE_COUNT) {
                m_gameboy->GetPPU().SetDisplayPalette(PALETTES[palette].colors);
            }
            LoadCheats();
            m_frame_timer->start(CurrentInterval());
            SetPausedUI(false);
            statusBar()->showMessage("ROM loaded: " + filename);
        } else {
            QMessageBox::critical(this, "Error", "Failed to load ROM");
        }
    }

    void OnPause() {
        if (m_frame_timer->isActive()) {
            m_frame_timer->stop();
            SetPausedUI(true);
            statusBar()->showMessage("Paused");
        } else {
            m_frame_timer->start(CurrentInterval());
            SetPausedUI(false);
            statusBar()->showMessage("Running");
        }
    }

    void OnReset() {
        if (m_gameboy->IsRunning()) {
            m_gameboy->Reset();
            spdlog::info("System reset");
            statusBar()->showMessage("System reset", 2000);
        }
    }

    void OnSaveState(int slot) {
        if (m_gameboy->SaveStateToFile(slot)) {
            m_current_slot = slot;
            statusBar()->showMessage(QString("State saved to slot %1").arg(slot), 2000);
        } else {
            statusBar()->showMessage("Failed to save state", 2000);
        }
    }

    void OnLoadState(int slot) {
        if (m_gameboy->LoadStateFromFile(slot)) {
            m_current_slot = slot;
            m_gl_widget->UpdateFramebuffer(m_gameboy->GetFramebuffer(), 160, 144);
            statusBar()->showMessage(QString("State loaded from slot %1").arg(slot), 2000);
        } else {
            statusBar()->showMessage(QString("No state in slot %1").arg(slot), 2000);
        }
    }

    void OnToggleMute() {
        bool muted = m_mute_action->isChecked();
        m_audio->SetMuted(muted);
        m_settings.setValue("audio/muted", muted);
        statusBar()->showMessage(muted ? "Muted" : "Unmuted", 1500);
    }

    void OnVolumeChanged(int value) {
        m_audio->SetVolume(value / 100.0f);
        m_settings.setValue("audio/volume", value);
    }

    void OnSpeedChanged(int index) {
        m_speed_index = index;
        m_settings.setValue("emulation/speed", index);
        if (m_frame_timer->isActive()) {
            m_frame_timer->start(CurrentInterval());
        }
    }

    void OnPaletteChanged(int index) {
        // Index -1 is "Game Boy Color": the colorization a real GBC's
        // boot ROM would assign this game
        if (index < 0) {
            m_gameboy->ApplyBootColorization();
        } else {
            m_gameboy->GetPPU().SetDisplayPalette(PALETTES[index].colors);
        }
        m_settings.setValue("video/palette", index);
        // Repaint immediately when paused so the change is visible
        m_gl_widget->UpdateFramebuffer(m_gameboy->GetFramebuffer(), 160, 144);
    }

    void OnScalingChanged(int index) {
        m_gl_widget->SetScalingMode(static_cast<GLWidget::ScalingMode>(index));
        m_settings.setValue("video/scaling", index);
    }

    void OnScreenshot() {
        if (!m_gameboy->IsRunning()) {
            statusBar()->showMessage("No ROM loaded", 2000);
            return;
        }
        QString filename = QFileDialog::getSaveFileName(
            this, "Save Screenshot", "screenshot.png", "PNG Image (*.png)");
        if (filename.isEmpty()) return;

        QImage image(reinterpret_cast<const uchar*>(m_gameboy->GetFramebuffer()),
                     160, 144, QImage::Format_RGBA8888);
        if (image.save(filename)) {
            statusBar()->showMessage("Screenshot saved: " + filename, 2000);
        } else {
            statusBar()->showMessage("Failed to save screenshot", 2000);
        }
    }

    void OnToggleFullscreen() {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
    }

    void OnControls() {
        SettingsDialog dialog(m_key_bindings, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_key_bindings = dialog.GetBindings();
            for (int i = 0; i < SettingsDialog::BUTTON_COUNT; i++) {
                m_settings.setValue(QString("input/key%1").arg(i), m_key_bindings[i]);
            }
            statusBar()->showMessage("Controls updated", 2000);
        }
    }

    void OnCheats() {
        if (!m_gameboy->IsRunning()) {
            statusBar()->showMessage("Load a ROM first", 2000);
            return;
        }
        CheatsDialog dialog(*m_gameboy, this);
        dialog.exec();
        SaveCheats();
    }

    void OnLinkHost() {
        bool ok = false;
        int port = QInputDialog::getInt(this, "Host Link Cable",
                                        "Listen on port:", 7683, 1024, 65535, 1, &ok);
        if (ok) {
            m_serial_link->Host(static_cast<u16>(port));
        }
    }

    void OnLinkConnect() {
        bool ok = false;
        QString address = QInputDialog::getText(
            this, "Connect Link Cable", "Host address (host or host:port):",
            QLineEdit::Normal, "127.0.0.1:7683", &ok);
        if (!ok || address.isEmpty()) return;

        QString host = address;
        u16 port = 7683;
        int colon = address.lastIndexOf(':');
        if (colon > 0) {
            host = address.left(colon);
            port = static_cast<u16>(address.mid(colon + 1).toUInt());
        }
        m_serial_link->ConnectTo(host, port);
    }

    void OnFrameUpdate() {
        if (!m_gameboy || !m_gameboy->IsRunning()) return;

        // Update joypad state. Bit clear = pressed, so combining keyboard
        // and touch input is a bitwise AND (either source can press).
        u8 effective_state = m_joypad_state;
#ifdef Q_OS_ANDROID
        effective_state &= m_touch_controls->State();
#endif
        m_gameboy->SetJoypadState(effective_state);

        // Run one frame
        m_gameboy->RunFrame();

        // Update display if frame is ready
        if (m_gameboy->IsFrameReady()) {
            m_gl_widget->UpdateFramebuffer(m_gameboy->GetFramebuffer(), 160, 144);
            m_gameboy->ClearFrameReady();
        }
    }

private:
    int CurrentInterval() const {
        // 1x = 16ms (~60 FPS); each speed step halves the interval
        return 16 >> m_speed_index;
    }

    void SetPausedUI(bool paused) {
        m_pause_action->setText(paused ? "&Resume" : "&Pause");
    }

    QMenu* BuildStateMenu(const QString& title, bool is_save) {
        QMenu* menu = new QMenu(title, this);
        for (int i = 0; i < GameBoy::STATE_SLOT_COUNT; i++) {
            int slot = i + 1;
            QAction* action = menu->addAction(QString("Slot &%1").arg(slot));
            Qt::Key key = static_cast<Qt::Key>(Qt::Key_F1 + i);
            action->setShortcut(is_save
                ? QKeySequence(QKeyCombination(Qt::ShiftModifier, key))
                : QKeySequence(key));
            if (is_save) {
                connect(action, &QAction::triggered, this, [this, slot]() { OnSaveState(slot); });
            } else {
                m_load_slot_actions[i] = action;
                connect(action, &QAction::triggered, this, [this, slot]() { OnLoadState(slot); });
            }
        }
        if (!is_save) {
            // Grey out empty slots when the menu opens
            connect(menu, &QMenu::aboutToShow, this, [this]() {
                for (int i = 0; i < GameBoy::STATE_SLOT_COUNT; i++) {
                    m_load_slot_actions[i]->setEnabled(m_gameboy->StateSlotExists(i + 1));
                }
            });
        }
        return menu;
    }

    void CreateMenus() {
        // File
        QMenu* fileMenu = menuBar()->addMenu("&File");

        QAction* openAction = fileMenu->addAction("&Open ROM...");
        openAction->setShortcut(QKeySequence::Open);
        connect(openAction, &QAction::triggered, this, &MainWindow::OnFileOpen);

        QAction* screenshotAction = fileMenu->addAction("&Screenshot...");
        screenshotAction->setShortcut(Qt::Key_F12);
        connect(screenshotAction, &QAction::triggered, this, &MainWindow::OnScreenshot);

        fileMenu->addSeparator();

        QAction* exitAction = fileMenu->addAction("E&xit");
        exitAction->setShortcut(QKeySequence::Quit);
        connect(exitAction, &QAction::triggered, this, &QWidget::close);

        // Emulation
        QMenu* emulationMenu = menuBar()->addMenu("&Emulation");

        m_pause_action = emulationMenu->addAction("&Pause");
        m_pause_action->setShortcut(Qt::Key_P);
        connect(m_pause_action, &QAction::triggered, this, &MainWindow::OnPause);

        QAction* resetAction = emulationMenu->addAction("&Reset");
        resetAction->setShortcut(Qt::Key_R);
        connect(resetAction, &QAction::triggered, this, &MainWindow::OnReset);

        QMenu* speedMenu = emulationMenu->addMenu("Spee&d");
        m_speed_group = new QActionGroup(this);
        const char* speed_names[] = {"1x", "2x", "4x", "8x"};
        for (int i = 0; i < 4; i++) {
            QAction* action = speedMenu->addAction(speed_names[i]);
            action->setCheckable(true);
            action->setChecked(i == 0);
            m_speed_group->addAction(action);
            connect(action, &QAction::triggered, this, [this, i]() { OnSpeedChanged(i); });
        }

        emulationMenu->addSeparator();

        QAction* quickSaveAction = emulationMenu->addAction("&Quick Save");
        quickSaveAction->setShortcut(Qt::Key_F5);
        connect(quickSaveAction, &QAction::triggered, this, [this]() { OnSaveState(m_current_slot); });

        QAction* quickLoadAction = emulationMenu->addAction("Quick &Load");
        quickLoadAction->setShortcut(Qt::Key_F7);
        connect(quickLoadAction, &QAction::triggered, this, [this]() { OnLoadState(m_current_slot); });

        emulationMenu->addMenu(BuildStateMenu("&Save State", true));
        emulationMenu->addMenu(BuildStateMenu("L&oad State", false));

        // Audio
        QMenu* audioMenu = menuBar()->addMenu("&Audio");
        m_mute_action = audioMenu->addAction("&Mute");
        m_mute_action->setCheckable(true);
        m_mute_action->setShortcut(Qt::Key_M);
        connect(m_mute_action, &QAction::triggered, this, &MainWindow::OnToggleMute);

        audioMenu->addSeparator();
        audioMenu->addAction("Volume")->setEnabled(false);

        m_volume_slider = new QSlider(Qt::Horizontal, audioMenu);
        m_volume_slider->setRange(0, 100);
        m_volume_slider->setValue(100);
        m_volume_slider->setMinimumWidth(140);
        connect(m_volume_slider, &QSlider::valueChanged, this, &MainWindow::OnVolumeChanged);
        QWidgetAction* volumeAction = new QWidgetAction(audioMenu);
        volumeAction->setDefaultWidget(m_volume_slider);
        audioMenu->addAction(volumeAction);

        // Graphics
        QMenu* graphicsMenu = menuBar()->addMenu("&Graphics");

        QMenu* paletteMenu = graphicsMenu->addMenu("&Palette");
        m_palette_group = new QActionGroup(this);

        // First entry: the colorization a real GBC assigns DMG games
        QAction* gbcAction = paletteMenu->addAction("Game Boy Color");
        gbcAction->setCheckable(true);
        gbcAction->setChecked(true);
        m_palette_group->addAction(gbcAction);
        connect(gbcAction, &QAction::triggered, this, [this]() { OnPaletteChanged(-1); });
        paletteMenu->addSeparator();

        for (int i = 0; i < PALETTE_COUNT; i++) {
            QAction* action = paletteMenu->addAction(PALETTES[i].name);
            action->setCheckable(true);
            m_palette_group->addAction(action);
            connect(action, &QAction::triggered, this, [this, i]() { OnPaletteChanged(i); });
        }

        QMenu* scalingMenu = graphicsMenu->addMenu("&Scaling");
        m_scaling_group = new QActionGroup(this);
        const char* scaling_names[] = {"Stretch", "Fit (Keep Aspect)", "Integer"};
        for (int i = 0; i < 3; i++) {
            QAction* action = scalingMenu->addAction(scaling_names[i]);
            action->setCheckable(true);
            action->setChecked(i == 1);  // GLWidget defaults to FIT
            m_scaling_group->addAction(action);
            connect(action, &QAction::triggered, this, [this, i]() { OnScalingChanged(i); });
        }

        graphicsMenu->addSeparator();

        QAction* fullscreenAction = graphicsMenu->addAction("&Fullscreen");
        fullscreenAction->setShortcut(Qt::Key_F11);
        connect(fullscreenAction, &QAction::triggered, this, &MainWindow::OnToggleFullscreen);

        // Link (cable over TCP)
        QMenu* linkMenu = menuBar()->addMenu("&Link");

        QAction* hostAction = linkMenu->addAction("&Host...");
        connect(hostAction, &QAction::triggered, this, &MainWindow::OnLinkHost);

        QAction* connectAction = linkMenu->addAction("&Connect...");
        connect(connectAction, &QAction::triggered, this, &MainWindow::OnLinkConnect);

        QAction* disconnectAction = linkMenu->addAction("&Disconnect");
        connect(disconnectAction, &QAction::triggered, this, [this]() {
            m_serial_link->Disconnect();
            statusBar()->showMessage("Link: disconnected", 3000);
        });

        // Tools
        QMenu* toolsMenu = menuBar()->addMenu("&Tools");

        QAction* controlsAction = toolsMenu->addAction("&Controls...");
        connect(controlsAction, &QAction::triggered, this, &MainWindow::OnControls);

        QAction* cheatsAction = toolsMenu->addAction("Chea&ts...");
        connect(cheatsAction, &QAction::triggered, this, &MainWindow::OnCheats);
    }

    void LoadSettings() {
        // Key bindings
        for (int i = 0; i < SettingsDialog::BUTTON_COUNT; i++) {
            m_key_bindings[i] = m_settings.value(
                QString("input/key%1").arg(i),
                SettingsDialog::DEFAULT_KEYS[i]).toInt();
        }

        // Audio
        int volume = m_settings.value("audio/volume", 100).toInt();
        m_volume_slider->setValue(volume);
        m_audio->SetVolume(volume / 100.0f);
        bool muted = m_settings.value("audio/muted", false).toBool();
        m_mute_action->setChecked(muted);
        m_audio->SetMuted(muted);

        // Video: palette -1 is GBC colorization (menu entry 0, and the
        // default LoadROM applies on its own), 0..N-1 are manual palettes
        int palette = m_settings.value("video/palette", -1).toInt();
        if (palette >= 0 && palette < PALETTE_COUNT) {
            m_palette_group->actions()[palette + 1]->setChecked(true);
            m_gameboy->GetPPU().SetDisplayPalette(PALETTES[palette].colors);
        } else {
            m_palette_group->actions()[0]->setChecked(true);
        }
        int scaling = m_settings.value("video/scaling", 1).toInt();
        if (scaling >= 0 && scaling < 3) {
            m_scaling_group->actions()[scaling]->setChecked(true);
            m_gl_widget->SetScalingMode(static_cast<GLWidget::ScalingMode>(scaling));
        }

        // Speed
        int speed = m_settings.value("emulation/speed", 0).toInt();
        if (speed >= 0 && speed < 4) {
            m_speed_group->actions()[speed]->setChecked(true);
            m_speed_index = speed;
        }
    }

    // Cheats persist per game, keyed by the ROM header title
    QString CheatSettingsKey() const {
        QString title = QString::fromStdString(m_gameboy->GetROMTitle());
        title.replace('/', '_');
        return "cheats/" + (title.isEmpty() ? "unknown" : title);
    }

#ifdef Q_OS_ANDROID
    // Copies a picked ROM (possibly a content:// URI) into app-private
    // storage and returns the local path, or an empty string on failure.
    QString CopyROMToLocalStorage(const QString& source_path) {
        QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + "/roms";
        QDir().mkpath(dir);

        QString name = QFileInfo(source_path).fileName();
        if (name.isEmpty()) name = "rom.gb";
        QString dest_path = dir + "/" + name;

        QFile source(source_path);
        if (!source.open(QIODevice::ReadOnly)) {
            spdlog::error("Failed to open picked ROM: {}", source_path.toStdString());
            return QString();
        }
        QByteArray data = source.readAll();
        source.close();

        QFile dest(dest_path);
        if (!dest.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            spdlog::error("Failed to write local ROM copy: {}", dest_path.toStdString());
            return QString();
        }
        dest.write(data);
        dest.close();

        return dest_path;
    }
#endif

    void SaveCheats() {
        if (!m_gameboy || !m_gameboy->IsRunning()) return;
        QStringList list;
        for (const Cheat& cheat : m_gameboy->GetCheats()) {
            list << QString("%1|%2|%3")
                        .arg(cheat.enabled ? "1" : "0")
                        .arg(QString::fromStdString(cheat.name))
                        .arg(QString::fromStdString(cheat.code));
        }
        if (list.isEmpty()) {
            m_settings.remove(CheatSettingsKey());
        } else {
            m_settings.setValue(CheatSettingsKey(), list);
        }
    }

    void LoadCheats() {
        QStringList list = m_settings.value(CheatSettingsKey()).toStringList();
        for (const QString& entry : list) {
            QStringList parts = entry.split('|');
            if (parts.size() != 3) continue;
            if (m_gameboy->AddCheat(parts[1].toStdString(), parts[2].toStdString())) {
                if (parts[0] != "1") {
                    m_gameboy->SetCheatEnabled(m_gameboy->GetCheats().size() - 1, false);
                }
            }
        }
    }

    void UpdateJoypad(int key, bool released) {
        // m_key_bindings index matches the joypad state bit:
        // 0=Right 1=Left 2=Up 3=Down 4=A 5=B 6=Select 7=Start
        // (0 = pressed, 1 = released)
        for (int i = 0; i < SettingsDialog::BUTTON_COUNT; i++) {
            if (m_key_bindings[i] == key) {
                u8 button_mask = static_cast<u8>(1 << i);
                if (released) {
                    m_joypad_state |= button_mask;
                } else {
                    m_joypad_state &= ~button_mask;
                }
                return;
            }
        }
    }

    std::unique_ptr<GameBoy> m_gameboy;
    std::unique_ptr<AudioOutput> m_audio;
    SerialLink* m_serial_link = nullptr;
    GLWidget* m_gl_widget;
#ifdef Q_OS_ANDROID
    TouchControls* m_touch_controls = nullptr;
#endif
    QTimer* m_frame_timer;
    QAction* m_pause_action = nullptr;
    QAction* m_mute_action = nullptr;
    QSlider* m_volume_slider = nullptr;
    QActionGroup* m_speed_group = nullptr;
    QActionGroup* m_palette_group = nullptr;
    QActionGroup* m_scaling_group = nullptr;
    std::array<QAction*, GameBoy::STATE_SLOT_COUNT> m_load_slot_actions{};
    std::array<int, SettingsDialog::BUTTON_COUNT> m_key_bindings{};
    QSettings m_settings{"dmwss", "dmwss"};
    u8 m_joypad_state;
    int m_current_slot;
    int m_speed_index = 0;
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
