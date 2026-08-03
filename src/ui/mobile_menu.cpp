#include "mobile_menu.hpp"
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QSignalBlocker>

static const char* BUTTON_STYLE =
    "QPushButton { background-color: rgba(255,255,255,40); color: white; "
    "border: 1px solid rgba(255,255,255,90); border-radius: 8px; "
    "padding: 14px; font-size: 16px; text-align: left; }"
    "QPushButton:checkable:checked { background-color: rgba(120,190,255,130); }"
    "QPushButton:pressed { background-color: rgba(255,255,255,90); }";

static const char* LABEL_STYLE = "color: white; font-size: 13px;";

// Static scaling option names, in GLWidget::ScalingMode order
static const char* SCALING_NAMES[3] = {"Stretch", "Fit (Keep Aspect)", "Fixed Size (Integer)"};

MobileMenu::MobileMenu(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NoSystemBackground, true);
    setStyleSheet("background-color: rgba(0,0,0,195);");

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background: transparent; border: none;");
    scroll->viewport()->setStyleSheet("background: transparent;");

    auto* card = new QWidget();
    card->setStyleSheet("background: transparent;");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(10);

    auto make_button = [&](const QString& text) {
        auto* button = new QPushButton(text, card);
        button->setStyleSheet(BUTTON_STYLE);
        button->setMinimumHeight(50);
        layout->addWidget(button);
        return button;
    };

    QPushButton* close_button = make_button(QString::fromUtf8("\xE2\x9C\x95  Close"));  // "✕ Close"
    connect(close_button, &QPushButton::clicked, this, &MobileMenu::CloseRequested);

    layout->addSpacing(8);
    QPushButton* open_rom_button = make_button("Open ROM...");
    connect(open_rom_button, &QPushButton::clicked, this, &MobileMenu::OpenROMRequested);

    m_pause_button = make_button("Pause");
    connect(m_pause_button, &QPushButton::clicked, this, &MobileMenu::PauseRequested);

    QPushButton* reset_button = make_button("Reset");
    connect(reset_button, &QPushButton::clicked, this, &MobileMenu::ResetRequested);

    QPushButton* quick_save_button = make_button("Quick Save");
    connect(quick_save_button, &QPushButton::clicked, this, &MobileMenu::QuickSaveRequested);

    QPushButton* quick_load_button = make_button("Quick Load");
    connect(quick_load_button, &QPushButton::clicked, this, &MobileMenu::QuickLoadRequested);

    QPushButton* cheats_button = make_button("Cheats...");
    connect(cheats_button, &QPushButton::clicked, this, &MobileMenu::CheatsRequested);

    layout->addSpacing(8);

    m_mute_button = make_button("Mute");
    m_mute_button->setCheckable(true);
    connect(m_mute_button, &QPushButton::clicked, this, &MobileMenu::MuteToggled);

    auto* volume_label = new QLabel("Volume", card);
    volume_label->setStyleSheet(LABEL_STYLE);
    layout->addWidget(volume_label);
    m_volume_slider = new QSlider(Qt::Horizontal, card);
    m_volume_slider->setRange(0, 100);
    m_volume_slider->setMinimumHeight(40);
    connect(m_volume_slider, &QSlider::valueChanged, this, &MobileMenu::VolumeChanged);
    layout->addWidget(m_volume_slider);

    layout->addSpacing(8);

    m_palette_button = make_button("Palette: Game Boy Color");
    connect(m_palette_button, &QPushButton::clicked, this, [this]() {
        if (m_palette_names.isEmpty()) return;
        m_palette_display_index = (m_palette_display_index + 1) % m_palette_names.size();
        UpdatePaletteLabel();
        emit PaletteChangeRequested(m_palette_display_index - 1);
    });

    m_scaling_button = make_button("Scaling: Fixed Size (Integer)");
    connect(m_scaling_button, &QPushButton::clicked, this, [this]() {
        m_scaling_index = (m_scaling_index + 1) % 3;
        UpdateScalingLabel();
        emit ScalingChangeRequested(m_scaling_index);
    });

    layout->addStretch();

    scroll->setWidget(card);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
}

void MobileMenu::SetPaletteOptions(const QStringList& names) {
    m_palette_names = names;
    m_palette_display_index = 0;
    UpdatePaletteLabel();
}

void MobileMenu::SetPaused(bool paused) {
    m_pause_button->setText(paused ? "Resume" : "Pause");
}

void MobileMenu::SetMuted(bool muted) {
    QSignalBlocker blocker(m_mute_button);
    m_mute_button->setChecked(muted);
}

void MobileMenu::SetVolume(int volume) {
    QSignalBlocker blocker(m_volume_slider);
    m_volume_slider->setValue(volume);
}

void MobileMenu::SetPaletteIndex(int index) {
    if (m_palette_names.isEmpty()) return;
    m_palette_display_index = qBound(0, index + 1, m_palette_names.size() - 1);
    UpdatePaletteLabel();
}

void MobileMenu::SetScalingIndex(int index) {
    m_scaling_index = qBound(0, index, 2);
    UpdateScalingLabel();
}

void MobileMenu::UpdatePaletteLabel() {
    if (m_palette_display_index < 0 || m_palette_display_index >= m_palette_names.size()) return;
    m_palette_button->setText("Palette: " + m_palette_names[m_palette_display_index]);
}

void MobileMenu::UpdateScalingLabel() {
    m_scaling_button->setText(QString("Scaling: %1").arg(SCALING_NAMES[m_scaling_index]));
}
