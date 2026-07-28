#include "settings_dialog.hpp"
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QVBoxLayout>

const std::array<int, SettingsDialog::BUTTON_COUNT> SettingsDialog::DEFAULT_KEYS = {
    Qt::Key_Right, Qt::Key_Left, Qt::Key_Up, Qt::Key_Down,
    Qt::Key_Z, Qt::Key_X, Qt::Key_Space, Qt::Key_Return
};

static const char* BUTTON_NAMES[SettingsDialog::BUTTON_COUNT] = {
    "Right", "Left", "Up", "Down", "A", "B", "Select", "Start"
};

KeyCaptureButton::KeyCaptureButton(QWidget* parent)
    : QPushButton(parent) {
    setFocusPolicy(Qt::ClickFocus);
    connect(this, &QPushButton::clicked, this, &KeyCaptureButton::StartCapture);
}

void KeyCaptureButton::SetKey(int key) {
    m_key = key;
    setText(QKeySequence(key).toString());
}

void KeyCaptureButton::StartCapture() {
    m_capturing = true;
    setText("Press a key...");
    grabKeyboard();
}

void KeyCaptureButton::StopCapture() {
    m_capturing = false;
    releaseKeyboard();
    setText(QKeySequence(m_key).toString());
}

void KeyCaptureButton::keyPressEvent(QKeyEvent* event) {
    if (!m_capturing) {
        QPushButton::keyPressEvent(event);
        return;
    }
    // Escape cancels; modifier keys alone are not bindable
    int key = event->key();
    if (key != Qt::Key_Escape && key != Qt::Key_Shift && key != Qt::Key_Control &&
        key != Qt::Key_Alt && key != Qt::Key_Meta) {
        m_key = key;
    }
    StopCapture();
}

void KeyCaptureButton::focusOutEvent(QFocusEvent* event) {
    if (m_capturing) {
        StopCapture();
    }
    QPushButton::focusOutEvent(event);
}

SettingsDialog::SettingsDialog(const std::array<int, BUTTON_COUNT>& bindings,
                               QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Controls");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Click a button, then press the key to bind:", this));

    QGridLayout* grid = new QGridLayout();
    for (int i = 0; i < BUTTON_COUNT; i++) {
        grid->addWidget(new QLabel(BUTTON_NAMES[i], this), i % 4, (i / 4) * 2);
        m_key_buttons[i] = new KeyCaptureButton(this);
        m_key_buttons[i]->SetKey(bindings[i]);
        m_key_buttons[i]->setMinimumWidth(110);
        grid->addWidget(m_key_buttons[i], i % 4, (i / 4) * 2 + 1);
    }
    layout->addLayout(grid);

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
        QDialogButtonBox::RestoreDefaults, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),
            &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < BUTTON_COUNT; i++) {
            m_key_buttons[i]->SetKey(DEFAULT_KEYS[i]);
        }
    });
    layout->addWidget(buttons);
}

std::array<int, SettingsDialog::BUTTON_COUNT> SettingsDialog::GetBindings() const {
    std::array<int, BUTTON_COUNT> bindings{};
    for (int i = 0; i < BUTTON_COUNT; i++) {
        bindings[i] = m_key_buttons[i]->GetKey();
    }
    return bindings;
}
