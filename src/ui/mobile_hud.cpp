#include "mobile_hud.hpp"
#include <QPushButton>
#include <QResizeEvent>

static const char* BUTTON_STYLE =
    "QPushButton { background-color: rgba(255,255,255,50); color: white; "
    "border: 2px solid rgba(0,0,0,110); border-radius: 28px; "
    "font-size: 20px; font-weight: bold; }"
    "QPushButton:checked { background-color: rgba(120,200,255,160); }"
    "QPushButton:pressed { background-color: rgba(255,255,255,120); }";

MobileHud::MobileHud(QWidget* parent) : QWidget(parent) {
    // The container itself must not intercept touches - only the two
    // buttons (real child widgets) should. Qt hit-tests children first,
    // so this lets clicks anywhere else on the overlay fall through to
    // TouchControls/GLWidget underneath.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);

    m_ff_button = new QPushButton(QString::fromUtf8("\xC2\xBB"), this);  // "»"
    m_ff_button->setCheckable(true);
    m_ff_button->setFixedSize(56, 56);
    m_ff_button->setStyleSheet(BUTTON_STYLE);
    m_ff_button->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    connect(m_ff_button, &QPushButton::toggled, this, &MobileHud::FastForwardToggled);

    m_menu_button = new QPushButton(QString::fromUtf8("\xE2\x98\xB0"), this);  // "☰"
    m_menu_button->setFixedSize(56, 56);
    m_menu_button->setStyleSheet(BUTTON_STYLE);
    m_menu_button->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    connect(m_menu_button, &QPushButton::clicked, this, &MobileHud::MenuRequested);

    UpdateLayout();
}

void MobileHud::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    UpdateLayout();
}

void MobileHud::UpdateLayout() {
    const int margin = qMax(16, qMin(width(), height()) / 24);
    m_ff_button->move(margin, margin);
    m_menu_button->move(width() - margin - m_menu_button->width(), margin);
}
