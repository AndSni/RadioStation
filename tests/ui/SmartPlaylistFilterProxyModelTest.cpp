#include "ui/SmartPlaylistFilterProxyModel.h"
#include "ui/TrackTableModel.h"

#include "db/TrackRecord.h"

#include <QSet>
#include <QTest>

using namespace radio::ui;
using namespace radio::db;

namespace {
TrackRecord makeTrack(qint64 id, const QString& genre, qint64 categoryId, int year, double energy, double bpm)
{
    TrackRecord track;
    track.id = id;
    track.title = QStringLiteral("Track %1").arg(id);
    track.genre = genre;
    track.categoryId = categoryId;
    track.year = year;
    track.energy = energy;
    track.bpm = bpm;
    return track;
}
}

class SmartPlaylistFilterProxyModelTest : public QObject {
    Q_OBJECT

private slots:
    void noFilterAcceptsEverything();
    void genreFilterNarrowsResults();
    void categoryIdsFilterNarrowsResults();
    void yearRangeFilterNarrowsResults();
    void energyRangeExcludesUnanalyzedTracks();
    void bpmRangeExcludesUnanalyzedTracks();
    void filterJsonRoundTrips();
    void clearingARangeRemovesTheConstraint();
};

void SmartPlaylistFilterProxyModelTest::noFilterAcceptsEverything()
{
    TrackTableModel source;
    source.setTracks({ makeTrack(1, QStringLiteral("Ambient"), 1, 1990, 0.2, 60),
        makeTrack(2, QStringLiteral("Metal"), 2, 2020, 0.9, 180) });

    SmartPlaylistFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    QCOMPARE(proxy.rowCount(), 2);
}

void SmartPlaylistFilterProxyModelTest::genreFilterNarrowsResults()
{
    TrackTableModel source;
    source.setTracks({ makeTrack(1, QStringLiteral("Dark Ambient"), -1, 0, -1, -1),
        makeTrack(2, QStringLiteral("Metal"), -1, 0, -1, -1) });

    SmartPlaylistFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setGenreFilter(QStringLiteral("ambient"));

    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 0);
}

void SmartPlaylistFilterProxyModelTest::categoryIdsFilterNarrowsResults()
{
    TrackTableModel source;
    source.setTracks({ makeTrack(1, QString(), 4, 0, -1, -1), makeTrack(2, QString(), 9, 0, -1, -1) });

    SmartPlaylistFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setCategoryIds({ 4 });

    QCOMPARE(proxy.rowCount(), 1);
}

void SmartPlaylistFilterProxyModelTest::yearRangeFilterNarrowsResults()
{
    TrackTableModel source;
    source.setTracks(
        { makeTrack(1, QString(), -1, 1985, -1, -1), makeTrack(2, QString(), -1, 2015, -1, -1) });

    SmartPlaylistFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setYearRange(1990, 2020);

    QCOMPARE(proxy.rowCount(), 1);
}

void SmartPlaylistFilterProxyModelTest::energyRangeExcludesUnanalyzedTracks()
{
    TrackTableModel source;
    source.setTracks(
        { makeTrack(1, QString(), -1, 0, 0.8, -1), makeTrack(2, QString(), -1, 0, -1, -1) }); // -1 == not analyzed

    SmartPlaylistFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setEnergyRange(0.5, 1.0);

    QCOMPARE(proxy.rowCount(), 1);
}

void SmartPlaylistFilterProxyModelTest::bpmRangeExcludesUnanalyzedTracks()
{
    TrackTableModel source;
    source.setTracks({ makeTrack(1, QString(), -1, 0, -1, 130), makeTrack(2, QString(), -1, 0, -1, -1) });

    SmartPlaylistFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setBpmRange(120, 140);

    QCOMPARE(proxy.rowCount(), 1);
}

void SmartPlaylistFilterProxyModelTest::filterJsonRoundTrips()
{
    SmartPlaylistFilterProxyModel proxy;
    proxy.setGenreFilter(QStringLiteral("Ambient"));
    proxy.setCategoryIds({ 4, 7 });
    proxy.setYearRange(1990, 2010);
    proxy.setEnergyRange(0.0, 0.4);
    proxy.setBpmRange(60, 90);

    const QString json = proxy.toFilterJson();

    SmartPlaylistFilterProxyModel restored;
    restored.setFromFilterJson(json);

    TrackTableModel source;
    source.setTracks({ makeTrack(1, QStringLiteral("Ambient"), 4, 1995, 0.2, 70) });
    restored.setSourceModel(&source);

    QCOMPARE(restored.rowCount(), 1);
}

void SmartPlaylistFilterProxyModelTest::clearingARangeRemovesTheConstraint()
{
    TrackTableModel source;
    source.setTracks({ makeTrack(1, QString(), -1, 0, -1, -1) }); // never analyzed

    SmartPlaylistFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setEnergyRange(0.5, 1.0);
    QCOMPARE(proxy.rowCount(), 0); // excluded -- unanalyzed track can't satisfy an energy constraint

    proxy.setEnergyRange(std::nullopt, std::nullopt); // clear it back to "no constraint"
    QCOMPARE(proxy.rowCount(), 1);
}

QTEST_MAIN(SmartPlaylistFilterProxyModelTest)
#include "SmartPlaylistFilterProxyModelTest.moc"
