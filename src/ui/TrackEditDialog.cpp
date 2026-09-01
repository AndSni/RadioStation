#include "TrackEditDialog.h"

#include "db/RotationCategoryRepository.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QVBoxLayout>

namespace radio::ui {

using radio::db::RotationCategoryRepository;
using radio::db::TrackRecord;

TrackEditDialog::TrackEditDialog(const TrackRecord& track, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Edit Track"));

    m_titleEdit = new QLineEdit(track.title, this);
    m_artistEdit = new QLineEdit(track.artist, this);
    m_albumEdit = new QLineEdit(track.album, this);
    m_genreEdit = new QLineEdit(track.genre, this);

    m_categoryCombo = new QComboBox(this);
    m_categoryCombo->addItem(QStringLiteral("(uncategorized)"), QVariant::fromValue<qint64>(-1));
    int selectIndex = 0;
    const auto categories = RotationCategoryRepository::allCategories();
    for (const auto& category : categories) {
        m_categoryCombo->addItem(category.name, QVariant::fromValue<qint64>(category.id));
        if (category.id == track.categoryId)
            selectIndex = m_categoryCombo->count() - 1;
    }
    m_categoryCombo->setCurrentIndex(selectIndex);

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Title"), m_titleEdit);
    form->addRow(QStringLiteral("Artist"), m_artistEdit);
    form->addRow(QStringLiteral("Album"), m_albumEdit);
    form->addRow(QStringLiteral("Genre"), m_genreEdit);
    form->addRow(QStringLiteral("Category"), m_categoryCombo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    setLayout(layout);
}

QString TrackEditDialog::title() const
{
    return m_titleEdit->text();
}

QString TrackEditDialog::artist() const
{
    return m_artistEdit->text();
}

QString TrackEditDialog::album() const
{
    return m_albumEdit->text();
}

QString TrackEditDialog::genre() const
{
    return m_genreEdit->text();
}

qint64 TrackEditDialog::categoryId() const
{
    return m_categoryCombo->currentData().value<qint64>();
}

} // namespace radio::ui
