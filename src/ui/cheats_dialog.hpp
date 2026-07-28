#pragma once
#include <QDialog>

class GameBoy;
class QTableWidget;
class QLineEdit;

// Cheat manager: list, add, remove, and toggle GameShark / Game Genie
// codes for the loaded game. Changes apply to the GameBoy immediately;
// the caller persists them after the dialog closes.
class CheatsDialog : public QDialog {
    Q_OBJECT

public:
    explicit CheatsDialog(GameBoy& gameboy, QWidget* parent = nullptr);

private slots:
    void OnAdd();
    void OnRemove();
    void OnItemChanged();

private:
    GameBoy& m_gameboy;
    QTableWidget* m_table;
    QLineEdit* m_name_edit;
    QLineEdit* m_code_edit;
    bool m_updating = false;

    void RefreshTable();
};
