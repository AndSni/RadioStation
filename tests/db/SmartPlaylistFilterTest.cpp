#include "db/SmartPlaylistFilter.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace radio::db;

namespace {
QJsonObject filterFrom(const char* json)
{
    return QJsonDocument::fromJson(QByteArray(json)).object();
}
}

class SmartPlaylistFilterTest : public QObject {
    Q_OBJECT

private slots:
    void emptyFilterMatchesEverything();
    void genreFilterIsCaseInsensitiveSubstring();
    void categoryIdsFilterRestrictsToListedCategories();
    void yearRangeExcludesOutOfRangeTracks();
    void energyRangeExcludesUnanalyzedTracks();
    void bpmRangeExcludesUnanalyzedTracks();
    void combinedFiltersAllMustMatch();
};

void SmartPlaylistFilterTest::emptyFilterMatchesEverything()
{
    TrackRecord track;
    track.genre = QStringLiteral("Ambient");
    track.year = 1990;
    track.energy = 0.1;
    track.bpm = 60;

    QVERIFY(matchesSmartPlaylistFilter(track, QJsonObject()));
}

void SmartPlaylistFilterTest::genreFilterIsCaseInsensitiveSubstring()
{
    TrackRecord track;
    track.genre = QStringLiteral("Dark Ambient");

    QVERIFY(matchesSmartPlaylistFilter(track, filterFrom(R"({"genre": "ambient"})")));
    QVERIFY(!matchesSmartPlaylistFilter(track, filterFrom(R"({"genre": "Metal"})")));
}

void SmartPlaylistFilterTest::categoryIdsFilterRestrictsToListedCategories()
{
    TrackRecord track;
    track.categoryId = 7;

    QVERIFY(matchesSmartPlaylistFilter(track, filterFrom(R"({"categoryIds": [4, 7]})")));
    QVERIFY(!matchesSmartPlaylistFilter(track, filterFrom(R"({"categoryIds": [4, 9]})")));
    // An empty array means "no constraint", matching the "absent means any" convention.
    QVERIFY(matchesSmartPlaylistFilter(track, filterFrom(R"({"categoryIds": []})")));
}

void SmartPlaylistFilterTest::yearRangeExcludesOutOfRangeTracks()
{
    TrackRecord track;
    track.year = 1995;

    QVERIFY(matchesSmartPlaylistFilter(track, filterFrom(R"({"yearMin": 1990, "yearMax": 2000})")));
    QVERIFY(!matchesSmartPlaylistFilter(track, filterFrom(R"({"yearMin": 2001})")));
    QVERIFY(!matchesSmartPlaylistFilter(track, filterFrom(R"({"yearMax": 1994})")));
}

void SmartPlaylistFilterTest::energyRangeExcludesUnanalyzedTracks()
{
    TrackRecord analyzed;
    analyzed.energy = 0.5;
    QVERIFY(matchesSmartPlaylistFilter(analyzed, filterFrom(R"({"energyMin": 0.3, "energyMax": 0.7})")));
    QVERIFY(!matchesSmartPlaylistFilter(analyzed, filterFrom(R"({"energyMin": 0.8})")));

    TrackRecord unanalyzed; // energy defaults to -1 -- "not analyzed"
    QVERIFY(!matchesSmartPlaylistFilter(unanalyzed, filterFrom(R"({"energyMin": 0.0})")));
    // With no energy constraint at all, an unanalyzed track still matches.
    QVERIFY(matchesSmartPlaylistFilter(unanalyzed, QJsonObject()));
}

void SmartPlaylistFilterTest::bpmRangeExcludesUnanalyzedTracks()
{
    TrackRecord analyzed;
    analyzed.bpm = 130;
    QVERIFY(matchesSmartPlaylistFilter(analyzed, filterFrom(R"({"bpmMin": 128, "bpmMax": 135})")));
    QVERIFY(!matchesSmartPlaylistFilter(analyzed, filterFrom(R"({"bpmMin": 140})")));

    TrackRecord unanalyzed; // bpm defaults to -1 -- "not analyzed"
    QVERIFY(!matchesSmartPlaylistFilter(unanalyzed, filterFrom(R"({"bpmMax": 200})")));
}

void SmartPlaylistFilterTest::combinedFiltersAllMustMatch()
{
    TrackRecord track;
    track.genre = QStringLiteral("Ambient");
    track.categoryId = 4;
    track.year = 1995;
    track.energy = 0.2;
    track.bpm = 70;

    const QJsonObject filter = filterFrom(R"({
        "genre": "ambient", "categoryIds": [4], "yearMin": 1990, "yearMax": 2000,
        "energyMin": 0.0, "energyMax": 0.4, "bpmMin": 60, "bpmMax": 90
    })");
    QVERIFY(matchesSmartPlaylistFilter(track, filter));

    TrackRecord wrongGenre = track;
    wrongGenre.genre = QStringLiteral("Metal");
    QVERIFY(!matchesSmartPlaylistFilter(wrongGenre, filter));
}

QTEST_MAIN(SmartPlaylistFilterTest)
#include "SmartPlaylistFilterTest.moc"
