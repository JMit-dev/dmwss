#include "mobile_hud.hpp"
#include <QPushButton>
#include <QResizeEvent>
#include <QRegion>

// Icon-only, no circle/background - just the glyph, dimmed slightly so it
// reads on both light and dark game content. The button's hit area (set
// via mask in UpdateLayout) stays a full 56x56 square for a comfortable
// touch target even though the glyph itself is small.
static const char* BUTTON_STYLE =
    "QPushButton { background: transparent; border: none; "
    "color: rgba(255,255,255,210); font-size: 26px; font-weight: bold; }"
    "QPushButton:checked { color: rgb(120,200,255); }"
    "QPushButton:pressed { color: rgba(255,255,255,120); }";

MobileHud::MobileHud(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NoSystemBackground, true);

    m_ff_button = new QPushButton(QString::fromUtf8("\xC2\xBB"), this);  // "»"
    m_ff_button->setCheckable(true);
    m_ff_button->setFixedSize(56, 56);
    m_ff_button->setStyleSheet(BUTTON_STYLE);
    connect(m_ff_button, &QPushButton::toggled, this, &MobileHud::FastForwardToggled);

    m_menu_button = new QPushButton(QString::fromUtf8("\xE2\x98\xB0"), this);  // "☰"
    m_menu_button->setFixedSize(56, 56);
    m_menu_button->setStyleSheet(BUTTON_STYLE);
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

    // This container spans the whole game area so button positions can be
    // computed from width()/height(), but only the two button rects should
    // actually be clickable - everywhere else needs to fall through to
    // TouchControls/GLWidget underneath. WA_TransparentForMouseEvents can't
    // do that: on a parent widget it also makes Qt skip its children during
    // hit-testing, not just the widget itself, so the buttons would never
    // receive clicks either. A mask does exactly what's needed instead -
    // clicks outside it pass straight through, clicks inside are delivered
    // to whichever child widget occupies that spot, normally.
    setMask(QRegion(m_ff_button->geometry()) + QRegion(m_menu_button->geometry()));
}
