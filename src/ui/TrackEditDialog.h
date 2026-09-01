#pragma once

#include "db/TrackRecord.h"

#include <QDialog>

class QLineEdit;
class QComboBox;

namespace radio::ui {

class TrackEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit TrackEditDialog(const radio::db::TrackRecord& track, QWidget* parent = nullptr);

    QString title() const;
    QString artist() const;
    QString album() const;
    QString genre() const;
    qint64 categoryId() const; // -1 == uncategorized

private:
    QLineEdit* m_titleEdit;
    QLineEdit* m_artistEdit;
    QLineEdit* m_albumEdit;
    QLineEdit* m_genreEdit;
    QComboBox* m_categoryCombo;
};

} // namespace radio::ui
