#include "cheats_dialog.hpp"
#include "../machine/gameboy.hpp"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

CheatsDialog::CheatsDialog(GameBoy& gameboy, QWidget* parent)
    : QDialog(parent)
    , m_gameboy(gameboy) {
    setWindowTitle("Cheats");
    resize(420, 360);

    QVBoxLayout* layout = new QVBoxLayout(this);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({"On", "Name", "Code"});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setColumnWidth(0, 36);
    connect(m_table, &QTableWidget::itemChanged, this, &CheatsDialog::OnItemChanged);
    layout->addWidget(m_table);

    QHBoxLayout* add_row = new QHBoxLayout();
    m_name_edit = new QLineEdit(this);
    m_name_edit->setPlaceholderText("Name");
    m_code_edit = new QLineEdit(this);
    m_code_edit->setPlaceholderText("Code (e.g. 01FF56D3 or ABC-DEF-GHI)");
    QPushButton* add_button = new QPushButton("Add", this);
    connect(add_button, &QPushButton::clicked, this, &CheatsDialog::OnAdd);
    connect(m_code_edit, &QLineEdit::returnPressed, this, &CheatsDialog::OnAdd);
    add_row->addWidget(m_name_edit, 1);
    add_row->addWidget(m_code_edit, 2);
    add_row->addWidget(add_button);
    layout->addLayout(add_row);

    QHBoxLayout* bottom_row = new QHBoxLayout();
    QPushButton* remove_button = new QPushButton("Remove Selected", this);
    connect(remove_button, &QPushButton::clicked, this, &CheatsDialog::OnRemove);
    QPushButton* close_button = new QPushButton("Close", this);
    connect(close_button, &QPushButton::clicked, this, &QDialog::accept);
    bottom_row->addWidget(remove_button);
    bottom_row->addStretch();
    bottom_row->addWidget(close_button);
    layout->addLayout(bottom_row);

    RefreshTable();
}

void CheatsDialog::RefreshTable() {
    m_updating = true;
    const auto& cheats = m_gameboy.GetCheats();
    m_table->setRowCount(static_cast<int>(cheats.size()));
    for (int i = 0; i < static_cast<int>(cheats.size()); i++) {
        QTableWidgetItem* enabled = new QTableWidgetItem();
        enabled->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        enabled->setCheckState(cheats[i].enabled ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(i, 0, enabled);

        QTableWidgetItem* name = new QTableWidgetItem(
            QString::fromStdString(cheats[i].name));
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(i, 1, name);

        QTableWidgetItem* code = new QTableWidgetItem(
            QString::fromStdString(cheats[i].code));
        code->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(i, 2, code);
    }
    m_updating = false;
}

void CheatsDialog::OnAdd() {
    QString name = m_name_edit->text().trimmed();
    QString code = m_code_edit->text().trimmed();
    if (code.isEmpty()) return;
    if (name.isEmpty()) name = code;

    if (m_gameboy.AddCheat(name.toStdString(), code.toStdString())) {
        m_name_edit->clear();
        m_code_edit->clear();
        RefreshTable();
    } else {
        QMessageBox::warning(this, "Invalid Code",
            "Not a valid GameShark (01XXYYYY) or Game Genie (ABC-DEF-GHI) code.");
    }
}

void CheatsDialog::OnRemove() {
    int row = m_table->currentRow();
    if (row < 0) return;
    m_gameboy.RemoveCheat(static_cast<size_t>(row));
    RefreshTable();
}

void CheatsDialog::OnItemChanged() {
    if (m_updating) return;
    for (int i = 0; i < m_table->rowCount(); i++) {
        QTableWidgetItem* item = m_table->item(i, 0);
        if (item) {
            m_gameboy.SetCheatEnabled(static_cast<size_t>(i),
                                      item->checkState() == Qt::Checked);
        }
    }
}
