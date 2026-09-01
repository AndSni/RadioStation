#include "SmartPlaylistPanelWidget.h"
#include "SmartPlaylistFilterProxyModel.h"
#include "TrackTableModel.h"

#include "db/PlaylistRepository.h"
#include "db/RotationCategoryRepository.h"
#include "db/SmartPlaylistRepository.h"
#include "db/TrackRepository.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QTableView>
#include <QVBoxLayout>
#include <QVector>

namespace radio::ui {

using radio::db::PlaylistRepository;
using radio::db::RotationCategoryRepository;
using radio::db::SmartPlaylistRepository;
using radio::db::TrackRepository;

namespace {
constexpr int kNewFilterComboData = -1;
}

SmartPlaylistPanelWidget::SmartPlaylistPanelWidget(QWidget* parent)
    : QWidget(parent)
{
    m_sourceModel = new TrackTableModel(this);
    m_proxyModel = new SmartPlaylistFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_sourceModel);

    m_genreEdit = new QLineEdit(this);
    m_genreEdit->setObjectName(QStringLiteral("smartPlaylistGenreEdit"));
    m_genreEdit->setPlaceholderText(QStringLiteral("Any genre (substring match)"));

    m_categoryList = new QListWidget(this);
    m_categoryList->setObjectName(QStringLiteral("smartPlaylistCategoryList"));
    m_categoryList->setMaximumHeight(90);
    auto* categoryGroup = new QGroupBox(QStringLiteral("Categories (none checked = any)"), this);
    auto* categoryLayout = new QVBoxLayout(categoryGroup);
    categoryLayout->addWidget(m_categoryList);

    m_yearEnableCheck = new QCheckBox(QStringLiteral("Filter by Year"), this);
    m_yearMinSpin = new QSpinBox(this);
    m_yearMinSpin->setRange(0, 2100);
    m_yearMaxSpin = new QSpinBox(this);
    m_yearMaxSpin->setRange(0, 2100);
    m_yearMaxSpin->setValue(2100);
    auto* yearRow = new QHBoxLayout();
    yearRow->addWidget(new QLabel(QStringLiteral("Min"), this));
    yearRow->addWidget(m_yearMinSpin);
    yearRow->addWidget(new QLabel(QStringLiteral("Max"), this));
    yearRow->addWidget(m_yearMaxSpin);

    m_energyEnableCheck = new QCheckBox(QStringLiteral("Filter by Energy"), this);
    m_energyMinSpin = new QDoubleSpinBox(this);
    m_energyMinSpin->setRange(0.0, 1.0);
    m_energyMinSpin->setSingleStep(0.05);
    m_energyMaxSpin = new QDoubleSpinBox(this);
    m_energyMaxSpin->setRange(0.0, 1.0);
    m_energyMaxSpin->setSingleStep(0.05);
    m_energyMaxSpin->setValue(1.0);
    auto* energyRow = new QHBoxLayout();
    energyRow->addWidget(new QLabel(QStringLiteral("Min"), this));
    energyRow->addWidget(m_energyMinSpin);
    energyRow->addWidget(new QLabel(QStringLiteral("Max"), this));
    energyRow->addWidget(m_energyMaxSpin);

    m_bpmEnableCheck = new QCheckBox(QStringLiteral("Filter by BPM"), this);
    m_bpmMinSpin = new QSpinBox(this);
    m_bpmMinSpin->setRange(0, 300);
    m_bpmMaxSpin = new QSpinBox(this);
    m_bpmMaxSpin->setRange(0, 300);
    m_bpmMaxSpin->setValue(300);
    auto* bpmRow = new QHBoxLayout();
    bpmRow->addWidget(new QLabel(QStringLiteral("Min"), this));
    bpmRow->addWidget(m_bpmMinSpin);
    bpmRow->addWidget(new QLabel(QStringLiteral("Max"), this));
    bpmRow->addWidget(m_bpmMaxSpin);

    for (QSpinBox* spin : { m_yearMinSpin, m_yearMaxSpin, m_bpmMinSpin, m_bpmMaxSpin })
        spin->setEnabled(false);
    for (QDoubleSpinBox* spin : { m_energyMinSpin, m_energyMaxSpin })
        spin->setEnabled(false);

    auto* filterForm = new QFormLayout();
    filterForm->addRow(QStringLiteral("Genre"), m_genreEdit);
    filterForm->addRow(m_yearEnableCheck);
    filterForm->addRow(yearRow);
    filterForm->addRow(m_energyEnableCheck);
    filterForm->addRow(energyRow);
    filterForm->addRow(m_bpmEnableCheck);
    filterForm->addRow(bpmRow);

    auto* filterRow = new QHBoxLayout();
    filterRow->addLayout(filterForm, 1);
    filterRow->addWidget(categoryGroup, 1);

    m_view = new QTableView(this);
    m_view->setObjectName(QStringLiteral("smartPlaylistTableView"));
    m_view->setModel(m_proxyModel);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->verticalHeader()->setVisible(false);

    m_savedSmartPlaylistCombo = new QComboBox(this);
    m_savedSmartPlaylistCombo->setObjectName(QStringLiteral("smartPlaylistCombo"));
    m_saveAsSmartPlaylistButton = new QPushButton(QStringLiteral("Save as Smart Playlist..."), this);
    m_saveAsSmartPlaylistButton->setObjectName(QStringLiteral("smartPlaylistSaveButton"));
    m_deleteSmartPlaylistButton = new QPushButton(QStringLiteral("Delete"), this);
    m_deleteSmartPlaylistButton->setObjectName(QStringLiteral("smartPlaylistDeleteButton"));
    m_createPlaylistButton = new QPushButton(QStringLiteral("Create Playlist from these results"), this);
    m_createPlaylistButton->setObjectName(QStringLiteral("smartPlaylistCreatePlaylistButton"));

    auto* actionRow = new QHBoxLayout();
    actionRow->addWidget(m_savedSmartPlaylistCombo, 1);
    actionRow->addWidget(m_saveAsSmartPlaylistButton);
    actionRow->addWidget(m_deleteSmartPlaylistButton);
    actionRow->addWidget(m_createPlaylistButton);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(filterRow);
    layout->addWidget(m_view, 1);
    layout->addLayout(actionRow);
    setLayout(layout);

    connect(m_genreEdit, &QLineEdit::textChanged, this, &SmartPlaylistPanelWidget::onFilterControlChanged);
    connect(m_categoryList, &QListWidget::itemChanged, this, &SmartPlaylistPanelWidget::onCategoryItemChanged);

    connect(m_yearEnableCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_yearMinSpin->setEnabled(checked);
        m_yearMaxSpin->setEnabled(checked);
        onFilterControlChanged();
    });
    connect(m_energyEnableCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_energyMinSpin->setEnabled(checked);
        m_energyMaxSpin->setEnabled(checked);
        onFilterControlChanged();
    });
    connect(m_bpmEnableCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_bpmMinSpin->setEnabled(checked);
        m_bpmMaxSpin->setEnabled(checked);
        onFilterControlChanged();
    });

    connect(m_yearMinSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &SmartPlaylistPanelWidget::onFilterControlChanged);
    connect(m_yearMaxSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &SmartPlaylistPanelWidget::onFilterControlChanged);
    connect(m_energyMinSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SmartPlaylistPanelWidget::onFilterControlChanged);
    connect(m_energyMaxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SmartPlaylistPanelWidget::onFilterControlChanged);
    connect(m_bpmMinSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &SmartPlaylistPanelWidget::onFilterControlChanged);
    connect(m_bpmMaxSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &SmartPlaylistPanelWidget::onFilterControlChanged);

    connect(m_saveAsSmartPlaylistButton, &QPushButton::clicked, this, &SmartPlaylistPanelWidget::onSaveAsSmartPlaylistClicked);
    connect(m_deleteSmartPlaylistButton, &QPushButton::clicked, this, &SmartPlaylistPanelWidget::onDeleteSmartPlaylistClicked);
    connect(m_createPlaylistButton, &QPushButton::clicked, this, &SmartPlaylistPanelWidget::onCreatePlaylistFromResultsClicked);
    connect(m_savedSmartPlaylistCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        &SmartPlaylistPanelWidget::onSavedSmartPlaylistChanged);

    refresh();
}

void SmartPlaylistPanelWidget::refresh()
{
    m_sourceModel->setTracks(TrackRepository::allTracks());

    QHash<qint64, QString> categoryNames;
    for (const auto& category : RotationCategoryRepository::allCategories())
        categoryNames.insert(category.id, category.name);
    m_sourceModel->setCategoryNames(categoryNames);

    reloadCategoryList();
    reloadSavedSmartPlaylistCombo();
}

void SmartPlaylistPanelWidget::reloadCategoryList()
{
    QSet<qint64> previouslyChecked;
    for (int i = 0; i < m_categoryList->count(); ++i) {
        auto* item = m_categoryList->item(i);
        if (item->checkState() == Qt::Checked)
            previouslyChecked.insert(item->data(Qt::UserRole).toLongLong());
    }

    m_categoryList->blockSignals(true);
    m_categoryList->clear();
    for (const auto& category : RotationCategoryRepository::allCategories()) {
        auto* item = new QListWidgetItem(category.name, m_categoryList);
        item->setData(Qt::UserRole, category.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(previouslyChecked.contains(category.id) ? Qt::Checked : Qt::Unchecked);
    }
    m_categoryList->blockSignals(false);
}

void SmartPlaylistPanelWidget::reloadSavedSmartPlaylistCombo()
{
    m_savedSmartPlaylistCombo->blockSignals(true);
    m_savedSmartPlaylistCombo->clear();
    m_savedSmartPlaylistCombo->addItem(QStringLiteral("(New Filter)"), kNewFilterComboData);

    int indexToSelect = 0;
    const auto smartPlaylists = SmartPlaylistRepository::allSmartPlaylists();
    for (const auto& smartPlaylist : smartPlaylists) {
        m_savedSmartPlaylistCombo->addItem(smartPlaylist.name, smartPlaylist.id);
        if (smartPlaylist.id == m_loadedSmartPlaylistId)
            indexToSelect = m_savedSmartPlaylistCombo->count() - 1;
    }
    if (indexToSelect == 0)
        m_loadedSmartPlaylistId = -1; // previously-loaded one no longer exists

    m_savedSmartPlaylistCombo->setCurrentIndex(indexToSelect);
    m_deleteSmartPlaylistButton->setEnabled(m_loadedSmartPlaylistId >= 0);
    m_savedSmartPlaylistCombo->blockSignals(false);
}

void SmartPlaylistPanelWidget::onCategoryItemChanged(QListWidgetItem*)
{
    onFilterControlChanged();
}

void SmartPlaylistPanelWidget::onFilterControlChanged()
{
    if (m_loadingFilter)
        return;
    applyFiltersFromUi();
}

void SmartPlaylistPanelWidget::applyFiltersFromUi()
{
    m_proxyModel->setGenreFilter(m_genreEdit->text().trimmed());

    QSet<qint64> categoryIds;
    for (int i = 0; i < m_categoryList->count(); ++i) {
        auto* item = m_categoryList->item(i);
        if (item->checkState() == Qt::Checked)
            categoryIds.insert(item->data(Qt::UserRole).toLongLong());
    }
    m_proxyModel->setCategoryIds(categoryIds);

    m_proxyModel->setYearRange(m_yearEnableCheck->isChecked() ? std::optional(m_yearMinSpin->value()) : std::nullopt,
        m_yearEnableCheck->isChecked() ? std::optional(m_yearMaxSpin->value()) : std::nullopt);
    m_proxyModel->setEnergyRange(
        m_energyEnableCheck->isChecked() ? std::optional(m_energyMinSpin->value()) : std::nullopt,
        m_energyEnableCheck->isChecked() ? std::optional(m_energyMaxSpin->value()) : std::nullopt);
    m_proxyModel->setBpmRange(m_bpmEnableCheck->isChecked() ? std::optional(m_bpmMinSpin->value()) : std::nullopt,
        m_bpmEnableCheck->isChecked() ? std::optional(m_bpmMaxSpin->value()) : std::nullopt);
}

void SmartPlaylistPanelWidget::resetFiltersToDefaults()
{
    m_loadingFilter = true;
    m_genreEdit->clear();
    for (int i = 0; i < m_categoryList->count(); ++i)
        m_categoryList->item(i)->setCheckState(Qt::Unchecked);
    m_yearEnableCheck->setChecked(false);
    m_energyEnableCheck->setChecked(false);
    m_bpmEnableCheck->setChecked(false);
    m_loadingFilter = false;

    m_loadedSmartPlaylistId = -1;
    m_proxyModel->setFromFilterJson(QStringLiteral("{}"));
}

void SmartPlaylistPanelWidget::loadFilterIntoUi(const QString& filterJson)
{
    const QJsonObject filter = QJsonDocument::fromJson(filterJson.toUtf8()).object();

    m_loadingFilter = true;

    m_genreEdit->setText(filter.value(QStringLiteral("genre")).toString());

    const QSet<qint64> categoryIds = [&filter] {
        QSet<qint64> ids;
        for (const auto& value : filter.value(QStringLiteral("categoryIds")).toArray())
            ids.insert(value.toVariant().toLongLong());
        return ids;
    }();
    for (int i = 0; i < m_categoryList->count(); ++i) {
        auto* item = m_categoryList->item(i);
        item->setCheckState(categoryIds.contains(item->data(Qt::UserRole).toLongLong()) ? Qt::Checked : Qt::Unchecked);
    }

    const bool hasYear = filter.contains(QStringLiteral("yearMin")) || filter.contains(QStringLiteral("yearMax"));
    m_yearEnableCheck->setChecked(hasYear);
    m_yearMinSpin->setEnabled(hasYear);
    m_yearMaxSpin->setEnabled(hasYear);
    if (filter.contains(QStringLiteral("yearMin")))
        m_yearMinSpin->setValue(filter.value(QStringLiteral("yearMin")).toInt());
    if (filter.contains(QStringLiteral("yearMax")))
        m_yearMaxSpin->setValue(filter.value(QStringLiteral("yearMax")).toInt());

    const bool hasEnergy = filter.contains(QStringLiteral("energyMin")) || filter.contains(QStringLiteral("energyMax"));
    m_energyEnableCheck->setChecked(hasEnergy);
    m_energyMinSpin->setEnabled(hasEnergy);
    m_energyMaxSpin->setEnabled(hasEnergy);
    if (filter.contains(QStringLiteral("energyMin")))
        m_energyMinSpin->setValue(filter.value(QStringLiteral("energyMin")).toDouble());
    if (filter.contains(QStringLiteral("energyMax")))
        m_energyMaxSpin->setValue(filter.value(QStringLiteral("energyMax")).toDouble());

    const bool hasBpm = filter.contains(QStringLiteral("bpmMin")) || filter.contains(QStringLiteral("bpmMax"));
    m_bpmEnableCheck->setChecked(hasBpm);
    m_bpmMinSpin->setEnabled(hasBpm);
    m_bpmMaxSpin->setEnabled(hasBpm);
    if (filter.contains(QStringLiteral("bpmMin")))
        m_bpmMinSpin->setValue(filter.value(QStringLiteral("bpmMin")).toInt());
    if (filter.contains(QStringLiteral("bpmMax")))
        m_bpmMaxSpin->setValue(filter.value(QStringLiteral("bpmMax")).toInt());

    m_loadingFilter = false;

    // Apply the exact saved JSON rather than reconstructing it from the UI
    // controls -- avoids any precision loss the spin boxes' rounding could
    // introduce (e.g. an odd bpm bound) on a filter the user isn't actively editing yet.
    m_proxyModel->setFromFilterJson(filterJson);
}

void SmartPlaylistPanelWidget::onSaveAsSmartPlaylistClicked()
{
    QString defaultName;
    if (m_loadedSmartPlaylistId >= 0)
        defaultName = SmartPlaylistRepository::byId(m_loadedSmartPlaylistId).name;

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Save as Smart Playlist"), QStringLiteral("Name:"), QLineEdit::Normal, defaultName, &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    const QString filterJson = m_proxyModel->toFilterJson();
    if (m_loadedSmartPlaylistId >= 0) {
        SmartPlaylistRepository::update(m_loadedSmartPlaylistId, name, filterJson);
    } else {
        m_loadedSmartPlaylistId = SmartPlaylistRepository::create(name, filterJson);
    }

    reloadSavedSmartPlaylistCombo();
}

void SmartPlaylistPanelWidget::onDeleteSmartPlaylistClicked()
{
    if (m_loadedSmartPlaylistId < 0)
        return;

    const auto reply = QMessageBox::question(this, QStringLiteral("Delete Smart Playlist"),
        QStringLiteral("Delete this smart playlist? Any schedule block targeting it will fall back to no target."));
    if (reply != QMessageBox::Yes)
        return;

    SmartPlaylistRepository::remove(m_loadedSmartPlaylistId);
    resetFiltersToDefaults();
    reloadSavedSmartPlaylistCombo();
}

void SmartPlaylistPanelWidget::onCreatePlaylistFromResultsClicked()
{
    QVector<qint64> trackIds;
    for (int row = 0; row < m_proxyModel->rowCount(); ++row) {
        const QModelIndex sourceIndex = m_proxyModel->mapToSource(m_proxyModel->index(row, TrackTableModel::TitleColumn));
        trackIds.append(m_sourceModel->trackAt(sourceIndex.row()).id);
    }

    if (trackIds.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Create Playlist"), QStringLiteral("No tracks match the current filter."));
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Create Playlist from these results"), QStringLiteral("Playlist name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    PlaylistRepository::createPlaylistFromTracks(name, trackIds);
    emit playlistCreated();
}

void SmartPlaylistPanelWidget::onSavedSmartPlaylistChanged(int index)
{
    const qint64 id = m_savedSmartPlaylistCombo->itemData(index).toLongLong();
    if (id == kNewFilterComboData) {
        resetFiltersToDefaults();
        return;
    }

    const auto smartPlaylist = SmartPlaylistRepository::byId(id);
    if (smartPlaylist.id < 0)
        return;

    m_loadedSmartPlaylistId = smartPlaylist.id;
    m_deleteSmartPlaylistButton->setEnabled(true);
    loadFilterIntoUi(smartPlaylist.filterJson);
}

} // namespace radio::ui
