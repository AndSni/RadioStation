#include "ClockEditorDialog.h"
#include "ClockElementEditorDialog.h"

#include "db/ClockRepository.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace radio::ui {

using radio::db::ClockElementRecord;
using radio::db::ClockRepository;

namespace {
constexpr int kElementIdRole = Qt::UserRole;

QString elementTypeLabel(const QString& elementType)
{
    if (elementType == QStringLiteral("music_playlist"))
        return QStringLiteral("Music Playlist");
    if (elementType == QStringLiteral("music_smart_playlist"))
        return QStringLiteral("Music Smart Playlist");
    if (elementType == QStringLiteral("cart_random"))
        return QStringLiteral("Cart (Random)");
    if (elementType == QStringLiteral("cart_color"))
        return QStringLiteral("Cart (Color Seq)");
    if (elementType == QStringLiteral("cart_specific"))
        return QStringLiteral("Cart (Specific)");
    return elementType;
}

QString formatElementSummary(int position, const ClockElementRecord& element)
{
    const QString timing = element.minuteOffset < 0
        ? QStringLiteral("Flow")
        : QStringLiteral(":%1 %2").arg(element.minuteOffset, 2, 10, QChar('0')).arg(element.timingMode);

    QString summary = QStringLiteral("%1. %2 — %3 · %4")
                           .arg(position + 1)
                           .arg(element.label.isEmpty() ? elementTypeLabel(element.elementType) : element.label,
                               elementTypeLabel(element.elementType), timing);
    return summary;
}
}

ClockEditorDialog::ClockEditorDialog(qint64 clockId, QWidget* parent)
    : QDialog(parent)
    , m_clockId(clockId)
{
    const auto clock = ClockRepository::clockById(clockId);
    setWindowTitle(QStringLiteral("Edit Clock — %1").arg(clock.name));
    resize(480, 420);

    m_nameEdit = new QLineEdit(clock.name, this);

    m_elementList = new QListWidget(this);
    m_elementList->setObjectName(QStringLiteral("clockElementList"));
    m_elementList->setDragDropMode(QAbstractItemView::InternalMove);
    m_elementList->setDefaultDropAction(Qt::MoveAction);

    auto* elementButtonRow = new QHBoxLayout();
    m_addElementButton = new QPushButton(QStringLiteral("Add Element"), this);
    m_editElementButton = new QPushButton(QStringLiteral("Edit Element"), this);
    m_deleteElementButton = new QPushButton(QStringLiteral("Delete Element"), this);
    elementButtonRow->addWidget(m_addElementButton);
    elementButtonRow->addWidget(m_editElementButton);
    elementButtonRow->addWidget(m_deleteElementButton);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Name"), this));
    layout->addWidget(m_nameEdit);
    layout->addWidget(m_elementList, 1);
    layout->addLayout(elementButtonRow);
    layout->addWidget(buttons);
    setLayout(layout);

    connect(m_nameEdit, &QLineEdit::editingFinished, this, &ClockEditorDialog::onNameEditingFinished);
    connect(m_elementList, &QListWidget::itemDoubleClicked, this, &ClockEditorDialog::onEditElementClicked);
    connect(m_elementList->model(), &QAbstractItemModel::rowsMoved, this, &ClockEditorDialog::onElementRowsMoved);
    connect(m_addElementButton, &QPushButton::clicked, this, &ClockEditorDialog::onAddElementClicked);
    connect(m_editElementButton, &QPushButton::clicked, this, &ClockEditorDialog::onEditElementClicked);
    connect(m_deleteElementButton, &QPushButton::clicked, this, &ClockEditorDialog::onDeleteElementClicked);

    refreshElementList();
}

void ClockEditorDialog::onNameEditingFinished()
{
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty() || name == ClockRepository::clockById(m_clockId).name)
        return;
    ClockRepository::renameClock(m_clockId, name);
    setWindowTitle(QStringLiteral("Edit Clock — %1").arg(name));
    emit clockChanged();
}

qint64 ClockEditorDialog::selectedElementId() const
{
    QListWidgetItem* item = m_elementList->currentItem();
    return item ? item->data(kElementIdRole).toLongLong() : -1;
}

void ClockEditorDialog::refreshElementList()
{
    const qint64 previouslySelected = selectedElementId();

    m_elementList->blockSignals(true);
    m_elementList->clear();

    const auto elements = ClockRepository::elementsForClock(m_clockId);
    QListWidgetItem* itemToReselect = nullptr;
    for (int i = 0; i < elements.size(); ++i) {
        const auto& element = elements.at(i);
        auto* item = new QListWidgetItem(formatElementSummary(i, element), m_elementList);
        item->setData(kElementIdRole, element.id);
        if (element.id == previouslySelected)
            itemToReselect = item;
    }

    m_elementList->blockSignals(false);
    if (itemToReselect)
        m_elementList->setCurrentItem(itemToReselect);
}

void ClockEditorDialog::onAddElementClicked()
{
    ClockElementRecord element;
    element.clockId = m_clockId;
    element.elementType = QStringLiteral("music_playlist");
    openElementEditor(element);
}

void ClockEditorDialog::onEditElementClicked()
{
    const qint64 elementId = selectedElementId();
    if (elementId < 0)
        return;
    openElementEditor(ClockRepository::elementById(elementId));
}

void ClockEditorDialog::openElementEditor(const ClockElementRecord& element)
{
    if (m_openEditor) {
        m_openEditor->raise();
        m_openEditor->activateWindow();
        return;
    }

    // Heap-allocated + non-modal, same rationale as
    // AutoDjPanelWidget::openBlockEditor().
    auto* dialog = new ClockElementEditorDialog(m_clockId, element, this);
    m_openEditor = dialog;
    connect(dialog, &ClockElementEditorDialog::accepted, this, [this, dialog, isNew = element.id < 0]() {
        if (isNew)
            ClockRepository::createElement(dialog->result());
        else
            ClockRepository::updateElement(dialog->result());
        refreshElementList();
        emit clockChanged();
    });
    connect(dialog, &ClockElementEditorDialog::finished, dialog, &QObject::deleteLater);
    dialog->show();
}

void ClockEditorDialog::onDeleteElementClicked()
{
    const qint64 elementId = selectedElementId();
    if (elementId < 0)
        return;
    if (QMessageBox::question(this, QStringLiteral("Delete Element"), QStringLiteral("Delete this clock element?"))
        != QMessageBox::Yes)
        return;
    ClockRepository::deleteElement(elementId);
    refreshElementList();
    emit clockChanged();
}

void ClockEditorDialog::onElementRowsMoved()
{
    QVector<qint64> order;
    order.reserve(m_elementList->count());
    for (int i = 0; i < m_elementList->count(); ++i)
        order.append(m_elementList->item(i)->data(kElementIdRole).toLongLong());
    ClockRepository::setElementOrder(order);
    refreshElementList(); // renumber the "N." prefixes to match the new order
}

} // namespace radio::ui
