#pragma once
#include <QWidget>
#include <QStringList>

class QPushButton;
class QSlider;

// Full-screen, semi-transparent settings overlay for Android, opened via
// MobileHud's hamburger button. Replaces the desktop menu bar, which
// Android has no room (or convention) for: everything reachable from
// File/Emulation/Audio/Graphics on desktop that still makes sense with
// touch-only input and no keyboard lives here instead.
class MobileMenu : public QWidget {
    Q_OBJECT

public:
    explicit MobileMenu(QWidget* parent = nullptr);

    // First entry is assumed to be the "Game Boy Color" auto-colorization
    // option (palette index -1); the rest map to palette indices 0..N-1.
    void SetPaletteOptions(const QStringList& names);

    void SetPaused(bool paused);
    void SetMuted(bool muted);
    void SetVolume(int volume);        // 0-100
    void SetPaletteIndex(int index);   // -1..N-1
    void SetScalingIndex(int index);   // 0=Stretch 1=Fit 2=Fixed Size

signals:
    void OpenROMRequested();
    void PauseRequested();
    void ResetRequested();
    void QuickSaveRequested();
    void QuickLoadRequested();
    void MuteToggled();
    void VolumeChanged(int value);
    void PaletteChangeRequested(int index);
    void ScalingChangeRequested(int index);
    void CheatsRequested();
    void CloseRequested();

private:
    QPushButton* m_pause_button;
    QPushButton* m_mute_button;
    QSlider* m_volume_slider;
    QPushButton* m_palette_button;
    QPushButton* m_scaling_button;

    QStringList m_palette_names;
    int m_palette_display_index = 0;  // index into m_palette_names
    int m_scaling_index = 2;

    void UpdatePaletteLabel();
    void UpdateScalingLabel();
};
