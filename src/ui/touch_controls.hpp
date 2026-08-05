#pragma once
#include <QWidget>
#include <QHash>
#include "../core/types.hpp"

// On-screen D-pad and buttons for touchscreen platforms (Android). Drawn
// as a semi-transparent overlay on top of the game display.
//
// Tracks each active touch point independently (QTouchEvent, not
// synthesized mouse events) so holding a D-pad direction and a face
// button at the same time - or two D-pad directions for a diagonal -
// works correctly.
class TouchControls : public QWidget {
    Q_OBJECT

public:
    explicit TouchControls(QWidget* parent = nullptr);

    // Joypad byte in the same convention as GameBoy::SetJoypadState:
    // bit clear = pressed. Bits with no finger on them read 1.
    // 0=Right 1=Left 2=Up 3=Down 4=A 5=B 6=Select 7=Start
    u8 State() const { return m_state; }

    // Drops all tracked touch points and resets to "nothing held". Needed
    // when the widget is hidden (e.g. the mobile settings menu covering
    // it) while a finger is still down - otherwise that touch's bits stay
    // latched with no TouchEnd ever arriving to clear them.
    void ClearTouches() {
        m_touch_bits.clear();
        m_state = 0xFF;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;

private:
    // Layout, recomputed on resize
    QRect m_dpad_rect;
    QRect m_button_a_rect;
    QRect m_button_b_rect;
    QRect m_button_select_rect;
    QRect m_button_start_rect;

    // Bits currently held by each active touch point, keyed by touch
    // point id; State() is the AND of "everything not currently held"
    QHash<int, u8> m_touch_bits;
    u8 m_state = 0xFF;

    void UpdateLayout();
    u8 HitTest(const QPointF& pos) const;
    void RecomputeState();
};
