#pragma once
#include <QDialog>
#include <QPushButton>
#include <array>

// Button that captures the next key press when clicked
class KeyCaptureButton : public QPushButton {
    Q_OBJECT

public:
    explicit KeyCaptureButton(QWidget* parent = nullptr);

    void SetKey(int key);
    int GetKey() const { return m_key; }

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    int m_key = 0;
    bool m_capturing = false;

    void StartCapture();
    void StopCapture();
};

// Controls configuration: remap the eight Game Boy buttons.
// Indices match the joypad state bits:
//   0=Right 1=Left 2=Up 3=Down 4=A 5=B 6=Select 7=Start
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    static constexpr int BUTTON_COUNT = 8;
    static const std::array<int, BUTTON_COUNT> DEFAULT_KEYS;

    explicit SettingsDialog(const std::array<int, BUTTON_COUNT>& bindings,
                            QWidget* parent = nullptr);

    std::array<int, BUTTON_COUNT> GetBindings() const;

private:
    std::array<KeyCaptureButton*, BUTTON_COUNT> m_key_buttons{};
};
