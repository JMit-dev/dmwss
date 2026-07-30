#include "touch_controls.hpp"
#include <QPainter>
#include <QTouchEvent>
#include <QResizeEvent>

// Joypad bit masks (0 = pressed): 0=Right 1=Left 2=Up 3=Down
// 4=A 5=B 6=Select 7=Start
static constexpr u8 BIT_RIGHT  = 0x01;
static constexpr u8 BIT_LEFT   = 0x02;
static constexpr u8 BIT_UP     = 0x04;
static constexpr u8 BIT_DOWN   = 0x08;
static constexpr u8 BIT_A      = 0x10;
static constexpr u8 BIT_B      = 0x20;
static constexpr u8 BIT_SELECT = 0x40;
static constexpr u8 BIT_START  = 0x80;

TouchControls::TouchControls(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    // Only the drawn control shapes should be opaque to the eye; the
    // widget itself has no window background to erase
    setAttribute(Qt::WA_NoSystemBackground, true);
}

void TouchControls::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    UpdateLayout();
}

void TouchControls::UpdateLayout() {
    const int w = width();
    const int h = height();
    const int margin = qMax(16, qMin(w, h) / 24);

    // D-pad: a square in the bottom-left, sized off the shorter dimension
    int dpad_size = qMin(w, h) / 3;
    m_dpad_rect = QRect(margin, h - margin - dpad_size, dpad_size, dpad_size);

    // A/B: two circles bottom-right, B slightly lower-left of A (matches
    // the physical GBC layout)
    int btn_size = dpad_size * 2 / 5;
    m_button_a_rect = QRect(w - margin - btn_size,
                            h - margin - dpad_size + btn_size / 2,
                            btn_size, btn_size);
    m_button_b_rect = QRect(w - margin - btn_size - btn_size,
                            h - margin - dpad_size + btn_size + btn_size / 2,
                            btn_size, btn_size);

    // Select/Start: small pills centered along the bottom, between the
    // D-pad and the face buttons
    int pill_w = btn_size;
    int pill_h = btn_size / 2;
    int mid_x = w / 2;
    m_button_select_rect = QRect(mid_x - pill_w - margin / 2, h - margin - pill_h,
                                 pill_w, pill_h);
    m_button_start_rect = QRect(mid_x + margin / 2, h - margin - pill_h,
                                pill_w, pill_h);
}

u8 TouchControls::HitTest(const QPointF& pos) const {
    // D-pad: divide the bounding square into a 3x3 grid. Edge cells are a
    // single direction, corner cells are two directions (diagonal), the
    // center cell is a dead zone.
    if (m_dpad_rect.contains(pos.toPoint())) {
        double local_x = (pos.x() - m_dpad_rect.left()) / m_dpad_rect.width();
        double local_y = (pos.y() - m_dpad_rect.top()) / m_dpad_rect.height();
        int col = local_x < 1.0 / 3 ? 0 : (local_x < 2.0 / 3 ? 1 : 2);
        int row = local_y < 1.0 / 3 ? 0 : (local_y < 2.0 / 3 ? 1 : 2);

        u8 bits = 0;
        if (row == 0) bits |= BIT_UP;
        if (row == 2) bits |= BIT_DOWN;
        if (col == 0) bits |= BIT_LEFT;
        if (col == 2) bits |= BIT_RIGHT;
        return bits;
    }

    auto in_circle = [](const QPointF& p, const QRect& bounds) {
        QPointF center = bounds.center();
        double radius = bounds.width() / 2.0;
        double dx = p.x() - center.x();
        double dy = p.y() - center.y();
        return (dx * dx + dy * dy) <= (radius * radius);
    };

    if (in_circle(pos, m_button_a_rect)) return BIT_A;
    if (in_circle(pos, m_button_b_rect)) return BIT_B;
    if (m_button_select_rect.contains(pos.toPoint())) return BIT_SELECT;
    if (m_button_start_rect.contains(pos.toPoint())) return BIT_START;

    return 0;
}

void TouchControls::RecomputeState() {
    u8 held = 0;
    for (u8 bits : m_touch_bits) {
        held |= bits;
    }
    // Convert "bits held" to the active-low joypad byte
    m_state = static_cast<u8>(~held);
}

bool TouchControls::event(QEvent* event) {
    switch (event->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate: {
            auto* touch_event = static_cast<QTouchEvent*>(event);
            for (const QEventPoint& point : touch_event->points()) {
                if (point.state() == QEventPoint::Released) {
                    m_touch_bits.remove(point.id());
                } else {
                    m_touch_bits[point.id()] = HitTest(point.position());
                }
            }
            RecomputeState();
            update();
            event->accept();
            return true;
        }
        case QEvent::TouchEnd:
        case QEvent::TouchCancel: {
            m_touch_bits.clear();
            RecomputeState();
            update();
            event->accept();
            return true;
        }
        default:
            return QWidget::event(event);
    }
}

void TouchControls::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QColor idle(255, 255, 255, 60);
    const QColor held(255, 255, 255, 130);
    const QPen outline(QColor(0, 0, 0, 100), 2);

    auto shade = [&](u8 bit) {
        return (~m_state & bit) ? held : idle;
    };

    // D-pad: a plus/cross shape divided into three columns and three rows
    painter.setPen(outline);
    int third_w = m_dpad_rect.width() / 3;
    int third_h = m_dpad_rect.height() / 3;
    QRect up(m_dpad_rect.left() + third_w, m_dpad_rect.top(), third_w, third_h * 2);
    QRect down(m_dpad_rect.left() + third_w, m_dpad_rect.top() + third_h, third_w, third_h * 2);
    QRect left(m_dpad_rect.left(), m_dpad_rect.top() + third_h, third_w * 2, third_h);
    QRect right(m_dpad_rect.left() + third_w, m_dpad_rect.top() + third_h, third_w * 2, third_h);

    painter.setBrush(shade(BIT_UP));
    painter.drawRect(up);
    painter.setBrush(shade(BIT_DOWN));
    painter.drawRect(down);
    painter.setBrush(shade(BIT_LEFT));
    painter.drawRect(left);
    painter.setBrush(shade(BIT_RIGHT));
    painter.drawRect(right);

    // Face buttons
    painter.setBrush(shade(BIT_A));
    painter.drawEllipse(m_button_a_rect);
    painter.drawText(m_button_a_rect, Qt::AlignCenter, "A");

    painter.setBrush(shade(BIT_B));
    painter.drawEllipse(m_button_b_rect);
    painter.drawText(m_button_b_rect, Qt::AlignCenter, "B");

    painter.setBrush(shade(BIT_SELECT));
    painter.drawRoundedRect(m_button_select_rect, 6, 6);
    painter.drawText(m_button_select_rect, Qt::AlignCenter, "SELECT");

    painter.setBrush(shade(BIT_START));
    painter.drawRoundedRect(m_button_start_rect, 6, 6);
    painter.drawText(m_button_start_rect, Qt::AlignCenter, "START");
}
