#pragma once
#include <QWidget>

class QPushButton;

// Two small always-on-screen touch buttons for Android: a fast-forward
// toggle (top-left) and a hamburger button that opens MobileMenu
// (top-right). The widget itself is transparent to touch everywhere
// except the two buttons, so it can sit above TouchControls without
// blocking the D-pad/face buttons underneath it.
class MobileHud : public QWidget {
    Q_OBJECT

public:
    explicit MobileHud(QWidget* parent = nullptr);

signals:
    void FastForwardToggled(bool active);
    void MenuRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    QPushButton* m_ff_button;
    QPushButton* m_menu_button;

    void UpdateLayout();
};
