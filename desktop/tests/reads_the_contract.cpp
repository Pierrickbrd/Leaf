// What the parser does with what the server actually sends.
//
// The interesting cases are not the happy one. They are: a required field the server forgot,
// a `null` that must not become an empty string, and a word this client has never heard of.
// The first two must fail loudly, the third must not fail at all — and the difference between
// those two reactions is the whole design.
//
// Fixtures are built as QJsonObject rather than parsed from text, because moc cannot read a
// raw string literal and escaped JSON is unreadable. It also means a typo in a field name is
// a typo in one place.

#include "Api.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using Qt::Literals::StringLiterals::operator""_s;

namespace {

QJsonObject aSeries()
{
    return QJsonObject{
        {u"id"_s, u"ed-1"_s},
        {u"workId"_s, u"wk-1"_s},
        {u"name"_s, u"Assassination Classroom"_s},
        {u"work"_s, u"Assassination Classroom"_s},
        {u"entryCount"_s, 21},
        {u"chapterCount"_s, 180},
        {u"arcCount"_s, 0},
        {u"universe"_s, QJsonValue::Null},
        {u"edition"_s, QJsonValue::Null},
        {u"author"_s, u"Yūsei Matsui"_s},
        {u"authors"_s, QJsonArray{u"Yūsei Matsui"_s}},
        {u"artists"_s, QJsonArray{u"Yūsei Matsui"_s}},
        {u"medium"_s, u"manga"_s},
        {u"readingDirection"_s, u"RIGHT_TO_LEFT"_s},
        {u"status"_s, u"completed"_s},
        {u"readStatus"_s, u"IN_PROGRESS"_s},
        {u"declaredVolumes"_s, 21},
        {u"ownedVolumes"_s, 21},
        {u"missingVolumes"_s, QJsonArray{}},
        {u"missingChapters"_s, QJsonArray{3.5}},
        {u"genres"_s, QJsonArray{u"Action"_s, u"Comédie"_s}},
        {u"tags"_s, QJsonArray{u"École"_s}},
        {u"ageRating"_s, u"12+"_s},
        {u"publisher"_s, u"Kana"_s},
        {u"collection"_s, QJsonValue::Null},
        {u"colour"_s, true},
        {u"language"_s, u"fr"_s},
        {u"addedAt"_s, 1750000000},
        {u"lastAddedAt"_s, 1755000000},
    };
}

QJsonObject anEntry()
{
    return QJsonObject{
        {u"id"_s, u"en-12"_s},
        {u"type"_s, u"VOLUME"_s},
        {u"number"_s, 12},
        {u"title"_s, QJsonValue::Null},
        {u"pageCount"_s, 190},
        {u"chapterCount"_s, 4},
        {u"file"_s, u"Tome 12.cbz"_s},
        {u"size"_s, 61234567},
    };
}

} // namespace

class ReadsTheContract : public QObject
{
    Q_OBJECT

private slots:
    void anyScriptSurvives();

    // ——— L'import ———————————————————————————————————————————————————————————

    /// The shape a file crosses the seam in, three times over: proposed, sent, filed.
    void a_proposal_arrives_with_everything_it_has_to_ask()
    {
        const QJsonObject body{
            {u"received"_s, u"rcv_1"_s},
            {u"name"_s, u"Tome 7.cbz"_s},
            {u"size"_s, 134217728},
            {u"read"_s,
             QJsonObject{{u"work"_s, u"Death Note"_s},
                         {u"edition"_s, u"Black Edition"_s},
                         {u"type"_s, u"VOLUME"_s},
                         {u"number"_s, 7},
                         {u"title"_s, u"Le pari"_s},
                         {u"chapterCount"_s, 12}}},
            {u"confidence"_s, u"AMBIGUOUS"_s},
            {u"reason"_s, u"deux séries portent ce nom"_s},
            {u"candidates"_s,
             QJsonArray{QJsonObject{{u"seriesId"_s, u"ed-1"_s}, {u"name"_s, u"Death Note"_s}},
                        QJsonObject{{u"seriesId"_s, u"ed-2"_s},
                                    {u"name"_s, u"Death Note · Black Edition"_s}}}},
            {u"replaces"_s, QJsonValue()},
            {u"concerns"_s, QJsonArray{u"un marqueur commence après la dernière page"_s}},
        };

        const Api::Read<Api::Proposal> got = Api::proposal(body);
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        const Api::Proposal &said = *got.value;

        QCOMPARE(said.received, u"rcv_1"_s);
        QCOMPARE(said.size, 134217728LL);
        QCOMPARE(said.confidence, Api::Proposal::Confidence::Ambiguous);
        QCOMPARE(said.read.work, std::optional<QString>(u"Death Note"_s));
        QCOMPARE(said.read.number, std::optional<double>(7));
        QCOMPARE(said.read.chapterCount, 12);
        QCOMPARE(said.candidates.size(), 2);
        QCOMPARE(said.candidates.at(1).name, u"Death Note · Black Edition"_s);
        // A JSON null is the contract's way of saying "not recorded", and it is not a
        // string this client should ever show.
        QVERIFY(!said.replaces.has_value());
        QCOMPARE(said.concerns.size(), 1);
    }

    /// A file that says nothing about itself is an ordinary file from somewhere else. It
    /// is offered anyway, and every field of its reading is simply absent.
    void a_file_that_declares_nothing_is_read_without_complaint()
    {
        const QJsonObject body{
            {u"received"_s, u"rcv_2"_s}, {u"name"_s, u"scan.cbz"_s},
            {u"size"_s, 12},             {u"read"_s, QJsonObject{}},
            {u"confidence"_s, u"UNKNOWN"_s}, {u"reason"_s, u"rien à quoi le rattacher"_s},
        };

        const Api::Read<Api::Proposal> got = Api::proposal(body);
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QVERIFY(!got.value->read.work.has_value());
        QCOMPARE(got.value->read.chapterCount, 0);
        QVERIFY(got.value->candidates.isEmpty());
        QVERIFY(got.value->concerns.isEmpty());
    }

    /// Structure is strict: a proposal without its reading is not a weaker proposal, it is
    /// an answer this client cannot act on, and it says which field was missing.
    void a_proposal_without_its_reading_is_refused_and_names_the_field()
    {
        const QJsonObject body{
            {u"received"_s, u"rcv_3"_s},     {u"name"_s, u"x.cbz"_s},
            {u"size"_s, 12},                 {u"confidence"_s, u"CERTAIN"_s},
            {u"reason"_s, u"une seule série"_s},
        };

        const Api::Read<Api::Proposal> got = Api::proposal(body);
        QVERIFY(!got.ok());
        QVERIFY2(got.trouble.contains(u"read"_s), qPrintable(got.trouble));
    }

    /// Vocabulary is loose. A confidence this client has not learned leaves the item
    /// standing, shown by the sentence the server wrote — the server may grow a word
    /// before the client does, and a file vanishing from a list for being described too
    /// well is the worse answer.
    void a_confidence_this_client_never_heard_of_leaves_the_file_standing()
    {
        const QJsonObject body{
            {u"received"_s, u"rcv_4"_s},  {u"name"_s, u"x.cbz"_s},
            {u"size"_s, 12},              {u"read"_s, QJsonObject{}},
            {u"confidence"_s, u"PERPLEXED"_s}, {u"reason"_s, u"quelque chose de neuf"_s},
        };

        const Api::Read<Api::Proposal> got = Api::proposal(body);
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->confidence, Api::Proposal::Confidence::Other);
        QCOMPARE(got.value->reason, u"quelque chose de neuf"_s);
    }

    /// The place held for a file, and the proposal inside it. A broken proposal breaks the
    /// reservation: there is nothing to do with an id whose answer cannot be read.
    void a_reservation_carries_its_proposal_and_falls_with_it()
    {
        const QJsonObject inside{
            {u"received"_s, u"rcv_5"_s},  {u"name"_s, u"Tome 1.cbz"_s},
            {u"size"_s, 40},              {u"read"_s, QJsonObject{}},
            {u"confidence"_s, u"CERTAIN"_s}, {u"reason"_s, u"une seule série"_s},
        };
        const Api::Read<Api::Reserved> got =
            Api::reserved(QJsonObject{{u"id"_s, u"rcv_5"_s}, {u"proposal"_s, inside}});
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->id, u"rcv_5"_s);
        QCOMPARE(got.value->proposal.confidence, Api::Proposal::Confidence::Certain);

        QJsonObject broken = inside;
        broken.remove(u"read"_s);
        const Api::Read<Api::Reserved> fell =
            Api::reserved(QJsonObject{{u"id"_s, u"rcv_5"_s}, {u"proposal"_s, broken}});
        QVERIFY(!fell.ok());
    }

    /// Where to resume. Both counts are required and neither may be guessed: a `received`
    /// read as zero because it was absent would send a whole volume again.
    void what_the_server_holds_is_read_as_a_number_and_never_guessed()
    {
        const Api::Read<Api::Staged> got = Api::staged(QJsonObject{
            {u"id"_s, u"rcv_6"_s},
            {u"name"_s, u"Tome 2.cbz"_s},
            {u"size"_s, 134217728},
            {u"received"_s, 104857600},
        });
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->size, 134217728LL);
        QCOMPARE(got.value->received, 104857600LL);

        const Api::Read<Api::Staged> without = Api::staged(QJsonObject{
            {u"id"_s, u"rcv_6"_s}, {u"name"_s, u"Tome 2.cbz"_s}, {u"size"_s, 134217728}});
        QVERIFY(!without.ok());
    }

    /// A file waiting for a decision, or for its bytes. `received` is what tells the two
    /// apart, so it cannot be optional here either.
    void a_waiting_file_says_how_much_of_it_is_actually_there()
    {
        const Api::Read<Api::Waiting> got = Api::waiting(QJsonObject{
            {u"id"_s, u"rcv_7"_s},
            {u"name"_s, u"Tome 3.cbz"_s},
            {u"size"_s, 900},
            {u"lastTouchedAt"_s, 1788463365455LL},
            {u"origin"_s, u"DROP"_s},
            {u"onlyCopy"_s, true},
            {u"received"_s, 0},
        });
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->lastTouchedAt, 1788463365455LL);
        QVERIFY(got.value->onlyCopy);
        QCOMPARE(got.value->received, 0LL);
    }

    /// Where it went. `renamed` is defaulted in the contract, so a server that leaves it
    /// out means false rather than nothing.
    void a_filed_volume_says_where_it_went_and_what_it_cost()
    {
        const Api::Read<Api::Filed> got = Api::filed(QJsonObject{
            {u"entryId"_s, u"en-9"_s},
            {u"path"_s, u"Death Note/Tome 7.cbz"_s},
            {u"replacement"_s, true},
            {u"note"_s, u"le compte déclaré est passé à 7"_s},
        });
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QVERIFY(got.value->replacement);
        QVERIFY(!got.value->renamed);
        QCOMPARE(got.value->note, std::optional<QString>(u"le compte déclaré est passé à 7"_s));
    }

    /// The question is about the volumes, never about the file names — which is why all of
    /// this crosses the wire rather than just the path.
    void a_collision_carries_what_each_of_the_two_says_about_itself()
    {
        const Api::Read<Api::Collision> got = Api::collision(QJsonObject{
            {u"path"_s, u"Death Note/Tome 7.cbz"_s},
            {u"entryId"_s, QJsonValue()},
            {u"occupies"_s, QJsonObject{{u"work"_s, u"Death Note"_s}, {u"number"_s, 7}}},
            {u"arriving"_s,
             QJsonObject{{u"work"_s, u"Death Note"_s},
                         {u"number"_s, 7},
                         {u"title"_s, u"Le pari"_s}}},
            {u"sameVolume"_s, true},
            {u"agrees"_s, QJsonArray{u"work"_s, u"number"_s}},
            {u"identical"_s, false},
            {u"wouldBecome"_s, u"Tome 7 (2).cbz"_s},
        });
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QVERIFY(got.value->sameVolume);
        QVERIFY(!got.value->identical);
        // Absent for a file dropped into the folder and not yet scanned.
        QVERIFY(!got.value->entryId.has_value());
        QCOMPARE(got.value->occupies.number, std::optional<double>(7));
        QCOMPARE(got.value->arriving.title, std::optional<QString>(u"Le pari"_s));
        QCOMPARE(got.value->agrees, QList<QString>({u"work"_s, u"number"_s}));
        QCOMPARE(got.value->wouldBecome, u"Tome 7 (2).cbz"_s);
    }

    /// What a folder would make. The sentence the dialog writes from is here and nowhere
    /// else: a reader cannot undo « it created a second universe » by deleting a file.
    void an_opened_folder_says_what_it_would_create()
    {
        const Api::Read<Api::Opened> got = Api::opened(QJsonObject{
            {u"id"_s, u"imp_1"_s},
            {u"root"_s, u"Terres d’Arran"_s},
            {u"creates"_s,
             QJsonArray{QJsonObject{{u"kind"_s, u"UNIVERSE"_s},
                                    {u"name"_s, u"Terres d’Arran"_s},
                                    {u"at"_s, u""_s}},
                        QJsonObject{{u"kind"_s, u"WORK"_s},
                                    {u"name"_s, u"Elfes"_s},
                                    {u"at"_s, u"Elfes"_s}}}},
            {u"toSend"_s, QJsonArray{u"Elfes/Tome 1.cbz"_s}},
            {u"alreadyThere"_s, QJsonArray{}},
            {u"bytesToSend"_s, 134217728},
        });
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->creates.size(), 2);
        QCOMPARE(got.value->creates.constFirst().kind, u"UNIVERSE"_s);
        QCOMPARE(got.value->creates.constLast().at, u"Elfes"_s);
        QCOMPARE(got.value->bytesToSend, 134217728LL);
        QCOMPARE(got.value->toSend, QList<QString>({u"Elfes/Tome 1.cbz"_s}));
    }

    /// And what it would *move*: a series the library already holds, that this folder
    /// declares somewhere else. It is not a creation and the dialog must not word it as one
    /// — the two are told apart only because a folder's identity travels in its sidecar.
    void an_opened_folder_says_what_it_would_move()
    {
        const Api::Read<Api::Opened> got = Api::opened(QJsonObject{
            {u"id"_s, u"imp_5"_s},
            {u"root"_s, u"Terres d’Arran"_s},
            {u"creates"_s, QJsonArray{}},
            {u"moves"_s,
             QJsonArray{QJsonObject{{u"workId"_s, u"w-1"_s},
                                    {u"name"_s, u"Elfes"_s},
                                    {u"from"_s, u"/srv/leaf/library/Mangas/Elfes"_s},
                                    {u"at"_s, u"Elfes"_s}}}},
            {u"toSend"_s, QJsonArray{}},
            {u"alreadyThere"_s, QJsonArray{}},
            {u"bytesToSend"_s, 0},
        });
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->moves.size(), 1);
        QCOMPARE(got.value->moves.constFirst().workId, u"w-1"_s);
        QCOMPARE(got.value->moves.constFirst().name, u"Elfes"_s);
        QCOMPARE(got.value->moves.constFirst().from, u"/srv/leaf/library/Mangas/Elfes"_s);
        QCOMPARE(got.value->moves.constFirst().at, u"Elfes"_s);

        // A line missing what a move is aimed with is refused by name: without the identity
        // the box would be there to tick and would file nothing.
        const Api::Read<Api::Opened> half = Api::opened(QJsonObject{
            {u"id"_s, u"imp_6"_s},
            {u"root"_s, u"Terres d’Arran"_s},
            {u"creates"_s, QJsonArray{}},
            {u"moves"_s, QJsonArray{QJsonObject{{u"name"_s, u"Elfes"_s}}}},
            {u"toSend"_s, QJsonArray{}},
            {u"alreadyThere"_s, QJsonArray{}},
            {u"bytesToSend"_s, 0},
        });
        QVERIFY(!half.ok());
        QVERIFY2(half.trouble.contains(u"workId"_s), qPrintable(half.trouble));
    }

    /// Nothing to create is an ordinary answer — everything announced is already there
    /// under a name the library knows.
    void a_folder_that_creates_nothing_reads_fine()
    {
        const Api::Read<Api::Opened> got = Api::opened(QJsonObject{
            {u"id"_s, u"imp_2"_s},   {u"root"_s, u"Bleach"_s},
            {u"creates"_s, QJsonArray{}}, {u"toSend"_s, QJsonArray{}},
            {u"alreadyThere"_s, QJsonArray{u"Tome 1.cbz"_s}},
            {u"bytesToSend"_s, 0},
        });
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QVERIFY(got.value->creates.isEmpty());
        QCOMPARE(got.value->bytesToSend, 0LL);
    }

    /// Where to resume, per file. The one map that crosses this seam, and a count in it
    /// that is not a number would send a whole volume again.
    void a_session_says_how_much_of_each_file_is_there()
    {
        const Api::Read<Api::Session> got = Api::session(QJsonObject{
            {u"id"_s, u"imp_3"_s},
            {u"root"_s, u"Bleach"_s},
            {u"received"_s,
             QJsonObject{{u"Tome 1.cbz"_s, 104857600}, {u"Tome 2.cbz"_s, 0}}},
            {u"missing"_s, QJsonArray{u"Tome 3.cbz"_s}},
        });
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->received.value(u"Tome 1.cbz"_s), 104857600LL);
        QCOMPARE(got.value->received.value(u"Tome 2.cbz"_s), 0LL);
        QCOMPARE(got.value->missing, QList<QString>({u"Tome 3.cbz"_s}));

        const Api::Read<Api::Session> lying = Api::session(QJsonObject{
            {u"id"_s, u"imp_4"_s},
            {u"root"_s, u"Bleach"_s},
            {u"received"_s, QJsonObject{{u"Tome 1.cbz"_s, u"beaucoup"_s}}},
        });
        QVERIFY(!lying.ok());
        QVERIFY2(lying.trouble.contains(u"Tome 1.cbz"_s), qPrintable(lying.trouble));
    }

    /// A commit that could not install everything is not a failure: it says what landed,
    /// what is still coming, what travelled wrong, and what it found that nobody named.
    void a_commit_says_what_landed_and_what_did_not()
    {
        const Api::Read<Api::Installed> got = Api::installed(QJsonObject{
            {u"root"_s, u"Bleach"_s},
            {u"installed"_s, 38},
            {u"orphans"_s, QJsonArray{u"Tome 99.cbz"_s}},
            {u"corrupt"_s, QJsonArray{u"Tome 12.cbz"_s}},
            {u"pending"_s, QJsonArray{u"Tome 40.cbz"_s, u"Tome 41.cbz"_s}},
            {u"open"_s, true},
        });
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->installed, 38);
        QCOMPARE(got.value->orphans.size(), 1);
        QCOMPARE(got.value->pending.size(), 2);
        QVERIFY(got.value->open);
        QVERIFY(got.value->moved.isEmpty());

        // And what it filed under the folder it installed, when it was asked to.
        const Api::Read<Api::Installed> filed = Api::installed(QJsonObject{
            {u"root"_s, u"Terres d’Arran"_s},
            {u"installed"_s, 0},
            {u"orphans"_s, QJsonArray{}},
            {u"moved"_s, QJsonArray{u"w-1"_s}},
        });
        QVERIFY2(filed.ok(), qPrintable(filed.trouble));
        QCOMPARE(filed.value->moved, QList<QString>({u"w-1"_s}));

        // `open` is defaulted in the contract: a server that leaves it out closed it.
        const Api::Read<Api::Installed> closed = Api::installed(QJsonObject{
            {u"root"_s, u"Bleach"_s}, {u"installed"_s, 1}, {u"orphans"_s, QJsonArray{}}});
        QVERIFY2(closed.ok(), qPrintable(closed.trouble));
        QVERIFY(!closed.value->open);
    }

    /// Where a chunk landed, and where the next one goes.
    void what_the_server_now_holds_of_one_file_is_a_number()
    {
        const Api::Read<Api::Received> got = Api::received(
            QJsonObject{{u"path"_s, u"Tome 1.cbz"_s}, {u"received"_s, 104857600}});
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->received, 104857600LL);

        const Api::Read<Api::BadOffset> refused = Api::badOffset(QJsonObject{
            {u"error"_s, u"impossible offset"_s}, {u"received"_s, 40000}});
        QVERIFY2(refused.ok(), qPrintable(refused.trouble));
        QCOMPARE(refused.value->received, 40000LL);

        // Without the count there is nothing to resume from, and guessing zero would send
        // the file again from its first byte.
        QVERIFY(!Api::badOffset(QJsonObject{{u"error"_s, u"x"_s}}).ok());
    }

    /// `sameVolume` and `identical` decide which answer a screen offers, so neither may be
    /// read as false for having been left out.
    void a_collision_missing_what_decides_it_is_refused()
    {
        const Api::Read<Api::Collision> got = Api::collision(QJsonObject{
            {u"path"_s, u"x.cbz"_s},
            {u"occupies"_s, QJsonObject{}},
            {u"arriving"_s, QJsonObject{}},
            {u"identical"_s, false},
            {u"wouldBecome"_s, u"x (2).cbz"_s},
        });
        QVERIFY(!got.ok());
    }

    void a_whole_series_arrives_intact()
    {
        const Api::Read<Api::Series> got = Api::series(aSeries());
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        const Api::Series &one = *got.value;

        QCOMPARE(one.id, u"ed-1"_s);
        QCOMPARE(one.name, u"Assassination Classroom"_s);
        QCOMPARE(one.counts.entries, 21);
        QCOMPARE(one.counts.chapters, 180);
        QCOMPARE(one.holding.ownedVolumes, 21);
        QCOMPARE(one.publication.declaredVolumes, std::optional<int>(21));
        QCOMPARE(one.credits.author, std::optional<QString>(u"Yūsei Matsui"_s));
        QCOMPARE(one.credits.authors, QList<QString>({u"Yūsei Matsui"_s}));
        QCOMPARE(one.credits.artists, QList<QString>({u"Yūsei Matsui"_s}));
        QCOMPARE(one.medium, std::optional<Api::Medium>(Api::Medium::Manga));
        QCOMPARE(one.readingDirection,
                 std::optional<Api::ReadingDirection>(Api::ReadingDirection::RightToLeft));
        QCOMPARE(one.run, std::optional<Api::Run>(Api::Run::Completed));
        QCOMPARE(one.holding.readStatus, std::optional<Api::ReadStatus>(Api::ReadStatus::InProgress));
        QCOMPARE(one.genres, QList<QString>({u"Action"_s, u"Comédie"_s}));
        QCOMPARE(one.tags, QList<QString>({u"École"_s}));
        QCOMPARE(one.ageRating, std::optional<QString>(u"12+"_s));
        QVERIFY(!one.publication.collection.has_value());
        QCOMPARE(one.colour, std::optional<bool>(true));
        QCOMPARE(one.holding.missingChapters, QList<double>({3.5}));
        QVERIFY(one.holding.missingVolumes.isEmpty());
        QCOMPARE(one.holding.lastAddedAt, std::optional<qint64>(1755000000));
    }

    /// Null is how the contract says "not recorded". An empty string would say "recorded as
    /// nothing", and the shelf would print an empty line where it should print nothing.
    void null_is_absent_and_not_an_empty_string()
    {
        const Api::Read<Api::Series> got = Api::series(aSeries());
        QVERIFY(got.ok());
        QVERIFY(!got.value->universe.has_value());
        QVERIFY(!got.value->edition.has_value());
    }

    /// A field the contract marks required is the server's promise. Breaking it is worth a
    /// sentence naming the field, not a series with a blank name.
    void a_missing_required_field_refuses_and_says_which()
    {
        QJsonObject without = aSeries();
        without.remove(u"name"_s);

        const Api::Read<Api::Series> got = Api::series(without);
        QVERIFY(!got.ok());
        QVERIFY2(got.trouble.contains(u"name"_s), qPrintable(got.trouble));
        QVERIFY2(got.trouble.contains(u"missing"_s), qPrintable(got.trouble));
    }

    void a_required_field_set_to_null_refuses_too()
    {
        QJsonObject nulled = aSeries();
        nulled[u"work"_s] = QJsonValue::Null;

        const Api::Read<Api::Series> got = Api::series(nulled);
        QVERIFY(!got.ok());
        QVERIFY2(got.trouble.contains(u"work"_s), qPrintable(got.trouble));
    }

    /// The other half of the rule: the server may learn a word before this client does, and a
    /// shelf that emptied itself over one would be worse than a shelf with one odd tile.
    void a_word_the_client_never_heard_keeps_the_row()
    {
        QJsonObject odd = aSeries();
        odd[u"medium"_s] = u"lianhuanhua"_s;

        const Api::Read<Api::Series> got = Api::series(odd);
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->medium, std::optional<Api::Medium>(Api::Medium::Other));
        QCOMPARE(got.value->name, u"Assassination Classroom"_s);
    }

    /// `medium` has a bucket for what this client has not been taught — `Other` — and these
    /// two have none. A word outside the three became Unread, and a word outside the two
    /// became Ongoing: a series that had stopped, shown as still running, and a collection
    /// read to the end, shown as untouched. Nothing is a fact this client can hold; a wrong
    /// value is not.
    void a_word_the_client_never_heard_is_absent_and_never_the_wrong_one()
    {
        QJsonObject odd = aSeries();
        odd[u"status"_s] = u"hiatus"_s;
        odd[u"readStatus"_s] = u"ARCHIVED"_s;

        const Api::Read<Api::Series> got = Api::series(odd);
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->run, std::optional<Api::Run>());
        QCOMPARE(got.value->holding.readStatus, std::optional<Api::ReadStatus>());
        QCOMPARE(got.value->name, u"Assassination Classroom"_s);
    }

    void a_page_says_where_the_broken_item_is()
    {
        QJsonObject broken = aSeries();
        broken.remove(u"arcCount"_s);

        const QJsonObject body{
            {u"total"_s, 3},
            {u"page"_s, 0},
            {u"size"_s, 100},
            {u"items"_s, QJsonArray{aSeries(), broken, aSeries()}},
        };

        const Api::Read<Api::Page> got = Api::page(body);
        QVERIFY(!got.ok());
        QVERIFY2(got.trouble.contains(u"items[1]"_s), qPrintable(got.trouble));
        QVERIFY2(got.trouble.contains(u"arcCount"_s), qPrintable(got.trouble));
    }

    void a_good_page_keeps_its_counts()
    {
        const QJsonObject body{
            {u"total"_s, 337},
            {u"page"_s, 2},
            {u"size"_s, 100},
            {u"items"_s, QJsonArray{aSeries(), aSeries()}},
        };

        const Api::Read<Api::Page> got = Api::page(body);
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->total, 337);
        QCOMPARE(got.value->page, 2);
        QCOMPARE(got.value->items.size(), 2);
    }

    /// "Tome 12 · Page 47/190 · Chapitre 98" — three facts from three places. The chapter's
    /// wording is the server's, so the band and the reader cannot disagree about it.
    void the_band_reads_where_you_stopped()
    {
        const QJsonObject body{
            {u"seriesId"_s, u"ed-1"_s},
            {u"seriesName"_s, u"Assassination Classroom"_s},
            {u"reason"_s, u"IN_PROGRESS"_s},
            {u"entry"_s, anEntry()},
            {u"progress"_s,
             QJsonObject{
                 {u"entryId"_s, u"en-12"_s},
                 {u"page"_s, 47},
                 {u"pageCount"_s, 190},
                 {u"finished"_s, false},
                 {u"updatedAt"_s, 1755000000},
                 {u"chapter"_s, QJsonObject{{u"id"_s, u"ch-98"_s},
                                            {u"raw"_s, u"098"_s},
                                            {u"label"_s, u"Chapitre 98"_s},
                                            {u"kind"_s, u"CHAPTER"_s},
                                            {u"position"_s, 2},
                                            {u"entryId"_s, u"en-12"_s}}},
             }},
        };

        const Api::Read<Api::UpNext> got = Api::upNext(body);
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->reason, Api::UpNext::Reason::InProgress);
        QCOMPARE(got.value->entryKind, Api::UpNext::Kind::Volume);
        QCOMPARE(got.value->entryNumber, std::optional<double>(12));
        QCOMPARE(got.value->page, std::optional<int>(47));
        QCOMPARE(got.value->pageCount, 190);
        QCOMPARE(got.value->chapterLabel, std::optional<QString>(u"Chapitre 98"_s));
    }

    /// Nothing started is not a fault, and the band words it differently. Absent progress must
    /// therefore parse, and must leave `page` empty rather than sitting it at zero.
    void nothing_started_parses_with_no_page()
    {
        const QJsonObject body{
            {u"seriesId"_s, u"ed-1"_s},
            {u"seriesName"_s, u"Berserk"_s},
            {u"reason"_s, u"NEXT_UP"_s},
            {u"entry"_s, anEntry()},
            {u"progress"_s, QJsonValue::Null},
        };

        const Api::Read<Api::UpNext> got = Api::upNext(body);
        QVERIFY2(got.ok(), qPrintable(got.trouble));
        QCOMPARE(got.value->reason, Api::UpNext::Reason::NextUp);
        QVERIFY(!got.value->page.has_value());
        QVERIFY(!got.value->chapterLabel.has_value());
        QCOMPARE(got.value->pageCount, 190);
    }

    void a_band_card_with_no_entry_refuses()
    {
        const QJsonObject body{
            {u"seriesId"_s, u"ed-1"_s},
            {u"seriesName"_s, u"Berserk"_s},
            {u"reason"_s, u"NEXT_UP"_s},
        };

        const Api::Read<Api::UpNext> got = Api::upNext(body);
        QVERIFY(!got.ok());
        QVERIFY2(got.trouble.contains(u"entry"_s), qPrintable(got.trouble));
    }

    /// The panel exists to show what you own, so a facet with no count is not a facet.
    void facets_keep_their_counts_and_drop_what_has_none()
    {
        const QJsonObject body{
            {u"media"_s, QJsonArray{QJsonObject{{u"value"_s, u"manga"_s}, {u"count"_s, 41}},
                                    QJsonObject{{u"value"_s, u"bd"_s}, {u"count"_s, 12}},
                                    QJsonObject{{u"value"_s, u"comics"_s}}}},
            {u"readStatuses"_s,
             QJsonArray{QJsonObject{{u"value"_s, u"UNREAD"_s}, {u"count"_s, 12}}}},
        };

        const Api::Read<Api::Facets> got = Api::facets(body);
        QVERIFY(got.ok());
        QCOMPARE(got.value->media.size(), 2);
        QCOMPARE(got.value->media.at(0).value, u"manga"_s);
        QCOMPARE(got.value->media.at(0).count, 41);
        QCOMPARE(got.value->readStatuses.size(), 1);
        QVERIFY(got.value->genres.isEmpty());
    }

    /// What the search found, and the three fields the contract insists on.
    void a_hit_carries_what_it_takes_to_open_it()
    {
        const Api::Read<Api::Hit> read = Api::hit(QJsonObject{
            {u"kind"_s, u"ENTRY"_s},
            {u"id"_s, u"volume-1"_s},
            {u"label"_s, u"Assassinat"_s},
            {u"seriesId"_s, u"ac"_s},
            {u"seriesName"_s, u"Assassination Classroom"_s},
            {u"entryId"_s, u"volume-1"_s},
        });
        QVERIFY2(read.ok(), qPrintable(read.trouble));
        QCOMPARE(read.value->kind, Api::Hit::Kind::Entry);
        QCOMPARE(read.value->label, u"Assassinat"_s);
        QCOMPARE(read.value->seriesName.value_or(QString()), u"Assassination Classroom"_s);
        // Absent is not approximate: an older server sending no such field would otherwise
        // have every one of its exact hits marked as a guess.
        QVERIFY(!read.value->approximate);
    }

    /// A chapter found inside a volume carries both levels: its own number and title, and
    /// the file it lives in. « Chapitre 98 » alone does not say what opening it opens.
    void a_chapter_hit_carries_the_file_it_lives_in()
    {
        const Api::Read<Api::Hit> read = Api::hit(QJsonObject{
            {u"kind"_s, u"CHAPTER"_s},
            {u"id"_s, u"ac-v12-c98"_s},
            {u"label"_s, u"Leçon 099"_s},
            {u"seriesId"_s, u"ac"_s},
            {u"entryId"_s, u"ac-v12"_s},
            {u"entryKind"_s, u"VOLUME"_s},
            {u"entryNumber"_s, 12.0},
            {u"entryTitle"_s, u"Shinigami"_s},
            {u"entryPageCount"_s, 189},
            {u"chapterNumber"_s, 99.0},
            {u"chapterTitle"_s, u"Cadeau (2e leçon)"_s},
        });
        QVERIFY2(read.ok(), qPrintable(read.trouble));
        QCOMPARE(read.value->entryKind, Api::UpNext::Kind::Volume);
        QCOMPARE(read.value->entryNumber.value_or(0), 12.0);
        QCOMPARE(read.value->entryTitle.value_or(QString()), u"Shinigami"_s);
        QCOMPARE(read.value->entryPageCount.value_or(0), 189);
        QCOMPARE(read.value->chapterNumber.value_or(0), 99.0);
        QCOMPARE(read.value->chapterTitle.value_or(QString()), u"Cadeau (2e leçon)"_s);
    }

    /// And an edition carries none of them: it is not inside anything.
    void an_edition_hit_lives_in_no_file()
    {
        const Api::Read<Api::Hit> read = Api::hit(QJsonObject{
            {u"kind"_s, u"EDITION"_s},
            {u"id"_s, u"ac"_s},
            {u"label"_s, u"Assassination Classroom"_s},
        });
        QVERIFY(read.ok());
        QVERIFY(!read.value->entryKind.has_value());
        QVERIFY(!read.value->entryPageCount.has_value());
        QVERIFY(!read.value->chapterTitle.has_value());
    }

    /// The envelope, when a page was asked for: the counts a heading needs come with it.
    void a_page_of_hits_carries_what_a_heading_must_say()
    {
        const Api::Read<Api::Hits> read = Api::hits(QJsonDocument(QJsonObject{
            {u"items"_s,
             QJsonArray{QJsonObject{{u"kind"_s, u"ENTRY"_s},
                                    {u"id"_s, u"v1"_s},
                                    {u"label"_s, u"Tome 1"_s}}}},
            {u"total"_s, 63},
            {u"fileTotal"_s, 60},
            {u"page"_s, 0},
            {u"size"_s, 40},
        }));
        QVERIFY2(read.ok(), qPrintable(read.trouble));
        QCOMPARE(read.value->items.size(), 1);
        QCOMPARE(read.value->total, 63);
        QCOMPARE(read.value->fileTotal, 60);
        QCOMPARE(read.value->size, 40);
    }

    /// And the bare list a server that predates the envelope answers: what is in hand is all
    /// there is known to be, counted here rather than invented larger.
    void a_bare_list_counts_itself()
    {
        const Api::Read<Api::Hits> read = Api::hits(QJsonDocument(QJsonArray{
            QJsonObject{{u"kind"_s, u"EDITION"_s}, {u"id"_s, u"ac"_s}, {u"label"_s, u"AC"_s}},
            QJsonObject{{u"kind"_s, u"ENTRY"_s}, {u"id"_s, u"v1"_s}, {u"label"_s, u"Tome 1"_s}},
            QJsonObject{{u"kind"_s, u"CHAPTER"_s}, {u"id"_s, u"c1"_s}, {u"label"_s, u"Ch. 1"_s}},
        }));
        QVERIFY2(read.ok(), qPrintable(read.trouble));
        QCOMPARE(read.value->total, 3);
        // Two of the three are files; an edition is a shelf tile and is counted apart.
        QCOMPARE(read.value->fileTotal, 2);
    }

    void a_broken_hit_refuses_the_whole_page_by_its_place()
    {
        const Api::Read<Api::Hits> read = Api::hits(QJsonDocument(QJsonArray{
            QJsonObject{{u"kind"_s, u"ENTRY"_s}, {u"id"_s, u"v1"_s}, {u"label"_s, u"Tome 1"_s}},
            QJsonObject{{u"kind"_s, u"ENTRY"_s}, {u"id"_s, u"v2"_s}},
        }));
        QVERIFY(!read.ok());
        QVERIFY2(read.trouble.contains(u"hits[1]"_s), qPrintable(read.trouble));
        QVERIFY2(read.trouble.contains(u"label"_s), qPrintable(read.trouble));
    }

    /// A kind this client has never heard of leaves the hit standing — the screen decides not
    /// to draw what it cannot place, which is not the same as refusing the whole answer.
    void a_kind_nobody_knows_leaves_the_hit_standing()
    {
        const Api::Read<Api::Hit> read = Api::hit(QJsonObject{
            {u"kind"_s, u"UNIVERSE"_s},
            {u"id"_s, u"parasite"_s},
            {u"label"_s, u"Parasite"_s},
        });
        QVERIFY(read.ok());
        QCOMPARE(read.value->kind, Api::Hit::Kind::Other);
    }

    /// And a hit without what it takes to be opened is refused, by the name of what is missing.
    void a_hit_without_a_label_is_refused_by_name()
    {
        const Api::Read<Api::Hit> read = Api::hit(QJsonObject{
            {u"kind"_s, u"EDITION"_s},
            {u"id"_s, u"ac"_s},
        });
        QVERIFY(!read.ok());
        QVERIFY2(read.trouble.contains(u"label"_s), qPrintable(read.trouble));
    }

    /// Walked rather than trusted: every spelling the contract lists must survive going in and
    /// coming back out. A switch that forgot a case would show up here and nowhere else.
    void the_contract_spellings_survive_the_round_trip()
    {
        for (const QString &word : {u"manga"_s, u"bd"_s, u"comics"_s, u"manhwa"_s, u"manhua"_s,
                                    u"webtoon"_s, u"artbook"_s, u"other"_s})
            QCOMPARE(Api::spell(Api::medium(word)), word);

        for (const QString &word : {u"name"_s, u"added"_s, u"volumes"_s, u"read"_s})
            QCOMPARE(Api::spell(Api::sort(word)), word);

        for (const QString &word : {u"EDITION"_s, u"ENTRY"_s, u"CHAPTER"_s})
            QCOMPARE(Api::spell(Api::hitKind(word)), word);

        // Asked before it is opened: a rename in `Api.cpp` that stopped one of these being
        // recognised would make this a dereference of an empty optional, and ctest would
        // report a segfault where it should report which word came back as nothing.
        for (const QString &word : {u"UNREAD"_s, u"IN_PROGRESS"_s, u"READ"_s}) {
            const std::optional<Api::ReadStatus> read = Api::readStatus(word);
            QVERIFY2(read.has_value(), qPrintable(word));
            QCOMPARE(Api::spell(*read), word);
        }
    }

    /// A report as the server sends one: counts, and one kind of finding with more behind
    /// it than it lists.
    static QJsonObject aReport()
    {
        return QJsonObject{
            {u"counts"_s,
             QJsonObject{{u"universes"_s, 1},
                         {u"works"_s, 5},
                         {u"editions"_s, 6},
                         {u"entries"_s, 59},
                         {u"chapters"_s, 546},
                         {u"pages"_s, 0},
                         {u"reanalysed"_s, 2}}},
            {u"chaptersWithoutStartPage"_s, 3},
            {u"findings"_s,
             QJsonArray{QJsonObject{
                 {u"kind"_s, u"DISREGARDED"_s},
                 {u"total"_s, 40},
                 {u"items"_s, QJsonArray{u"Akira/Tome 1.cbz — sans numéro"_s,
                                         u"Akira/Tome 2.cbz — sans numéro"_s}}}}},
        };
    }

    /// What the server is. Read rather than assumed: a client that guesses its server's
    /// version is one that will read a field the other end stopped sending.
    void what_the_server_says_it_is_is_read_in_full()
    {
        const QJsonObject said{{u"status"_s, u"ok"_s}, {u"api"_s, 1},
                               {u"format"_s, 1},       {u"library"_s, 6},
                               {u"localDrop"_s, true}};
        const Api::Read<Api::Health> read = Api::health(said);
        QVERIFY2(read.ok(), qPrintable(read.trouble));
        QCOMPARE(read.value->status, u"ok"_s);
        QCOMPARE(read.value->api, 1);
        QCOMPARE(read.value->format, 1);
        QCOMPARE(read.value->library, 6);
        QVERIFY(read.value->localDrop);
    }

    /// No shared folder is the ordinary case for a server on another machine, not a
    /// malformed answer.
    void a_server_with_no_shared_folder_is_not_a_broken_answer()
    {
        const QJsonObject said{
            {u"status"_s, u"ok"_s}, {u"api"_s, 1}, {u"format"_s, 1}, {u"library"_s, 0}};
        const Api::Read<Api::Health> read = Api::health(said);
        QVERIFY2(read.ok(), qPrintable(read.trouble));
        QVERIFY(!read.value->localDrop);
        // A library of zero is a fact, not an absence: a server answering perfectly over an
        // unmounted disk answers exactly this.
        QCOMPARE(read.value->library, 0);
    }

    void a_health_without_its_version_is_refused_by_name()
    {
        const Api::Read<Api::Health> read =
            Api::health(QJsonObject{{u"status"_s, u"ok"_s}, {u"format"_s, 1}});
        QVERIFY(!read.ok());
        QVERIFY2(read.trouble.contains(u"api"_s), qPrintable(read.trouble));
    }

    void the_scan_says_where_it_is_and_what_the_last_one_found()
    {
        using enum Api::ScanStatus::State;
        const QJsonObject said{{u"state"_s, u"DONE"_s},
                               {u"startedAt"_s, 1788463365455LL},
                               {u"finishedAt"_s, 1788463370000LL},
                               {u"report"_s, aReport()}};
        const Api::Read<Api::ScanStatus> read = Api::scanStatus(said);
        QVERIFY2(read.ok(), qPrintable(read.trouble));
        QCOMPARE(read.value->state, Done);
        // Milliseconds, and past what an int holds: read as a small number, a scan from
        // last week becomes one from 1970.
        QCOMPARE(read.value->startedAt.value_or(0), 1788463365455LL);

        QVERIFY(read.value->report.has_value());
        QCOMPARE(read.value->report->counts.editions, 6);
        QCOMPARE(read.value->report->counts.chapters, 546);
        QCOMPARE(read.value->report->chaptersWithoutStartPage, 3);
        QCOMPARE(read.value->report->findings.size(), 1);
        // How many there are is not how many are listed: a library with four hundred of one
        // mistake sends sixteen lines and the number.
        QCOMPARE(read.value->report->findings.constFirst().kind, u"DISREGARDED"_s);
        QCOMPARE(read.value->report->findings.constFirst().total, 40);
        QCOMPARE(read.value->report->findings.constFirst().items.size(), 2);
    }

    /// Two counts that are nought on every scan but one: the reading positions a scan
    /// carried onto a new identity, and those it could not.
    ///
    /// Read as absent rather than required, and both halves are worth a test. A server that
    /// sends them must be read; a server that does not must not have its whole answer
    /// refused over a number that is nought almost always — the settings screen would then
    /// say nothing at all about a library it can otherwise describe in full.
    void the_places_a_scan_carried_are_read_and_their_absence_is_not_a_fault()
    {
        QJsonObject report = aReport();
        QJsonObject counts = report.value(u"counts"_s).toObject();
        counts.insert(u"progressCarried"_s, 42);
        counts.insert(u"progressLost"_s, 1);
        report.insert(u"counts"_s, counts);

        auto read = Api::scanStatus({{u"state"_s, u"DONE"_s}, {u"report"_s, report}});
        QVERIFY2(read.ok(), qPrintable(read.trouble));
        QCOMPARE(read.value->report->counts.progressCarried, 42);
        QCOMPARE(read.value->report->counts.progressLost, 1);

        read = Api::scanStatus({{u"state"_s, u"DONE"_s}, {u"report"_s, aReport()}});
        QVERIFY2(read.ok(), qPrintable(read.trouble));
        QCOMPARE(read.value->report->counts.progressCarried, 0);
        QCOMPARE(read.value->report->counts.editions, 6);
    }

    /// A scan that has never run carries neither a date nor a report, and that is an answer
    /// rather than a gap.
    void a_scan_that_never_ran_says_so_without_dates()
    {
        using enum Api::ScanStatus::State;
        const Api::Read<Api::ScanStatus> read =
            Api::scanStatus(QJsonObject{{u"state"_s, u"IDLE"_s}});
        QVERIFY2(read.ok(), qPrintable(read.trouble));
        QCOMPARE(read.value->state, Idle);
        QVERIFY(!read.value->startedAt.has_value());
        QVERIFY(!read.value->report.has_value());
    }

    /// A state this client has not been taught leaves the screen able to say when the last
    /// scan ran, which is more use than refusing the whole answer.
    void a_state_nobody_knows_does_not_refuse_the_answer()
    {
        using enum Api::ScanStatus::State;
        const Api::Read<Api::ScanStatus> read = Api::scanStatus(
            QJsonObject{{u"state"_s, u"PAUSED"_s}, {u"finishedAt"_s, 1788463370000LL}});
        QVERIFY2(read.ok(), qPrintable(read.trouble));
        QCOMPARE(read.value->state, Other);
        QCOMPARE(read.value->finishedAt.value_or(0), 1788463370000LL);
    }

    void malformed_nested_scan_fields_are_refused_by_name()
    {
        QJsonObject report = aReport();
        report.insert(u"counts"_s, QJsonObject{});
        auto read = Api::scanStatus({{u"state"_s, u"DONE"_s}, {u"report"_s, report}});
        QVERIFY(!read.ok());
        QVERIFY(read.trouble.contains(u"universes"_s));
        report = aReport();
        report.insert(u"findings"_s, QJsonArray{QJsonObject{{u"kind"_s, u"ERRORS"_s}}});
        read = Api::scanStatus({{u"state"_s, u"DONE"_s}, {u"report"_s, report}});
        QVERIFY(!read.ok());
        QVERIFY(read.trouble.contains(u"total"_s));
    }

    void a_scan_without_its_state_is_refused_by_name()
    {
        const Api::Read<Api::ScanStatus> read = Api::scanStatus(QJsonObject{});
        QVERIFY(!read.ok());
        QVERIFY2(read.trouble.contains(u"state"_s), qPrintable(read.trouble));
    }

    /// Every answer of the import road, refused when a field it cannot do without is gone.
    ///
    /// `Api.h`'s rule is that structure is strict: a missing required field refuses the
    /// item and names it. The shelf's readers are held to that one at a time above; the
    /// eleven between a pre-flight and a commit were not held to it at all. Each has two
    /// ways to refuse — the field it checks by hand, and the text fields `Fields` gathers
    /// — and a reader that stopped doing either would hand back a default-constructed
    /// answer: a commit reading « 0 tome envoyé » over an import that did happen, or a
    /// session whose `received` is empty, which sends every volume of a folder again.
    void every_answer_of_the_import_road_refuses_what_it_cannot_read()
    {
        const auto refuses = []<typename T>(const Api::Read<T> &read, const char *what) {
            QVERIFY2(!read.ok(), what);
            QVERIFY2(!read.trouble.isEmpty(), what);
        };

        refuses(Api::proposal({}), "a proposal with no size");
        refuses(Api::proposal({{u"size"_s, 9}, {u"read"_s, QJsonObject{}}}),
                "a proposal with no name and no confidence");
        refuses(Api::proposal({{u"size"_s, 9},
                               {u"read"_s, QJsonObject{}},
                               {u"received"_s, u"rcv_1"_s},
                               {u"name"_s, u"Tome 1.cbz"_s},
                               {u"confidence"_s, u"CERTAIN"_s},
                               {u"reason"_s, u"une seule série"_s},
                               {u"candidates"_s, QJsonArray{QJsonObject{}}}}),
                "a proposal whose candidate names nothing");

        refuses(Api::reserved({}), "a reservation with no id");
        refuses(Api::staged({{u"size"_s, 1}, {u"received"_s, 0}}), "a staged file with no id");

        refuses(Api::waiting({}), "a waiting file with none of its numbers");
        refuses(Api::waiting({{u"size"_s, 1},
                              {u"lastTouchedAt"_s, 1},
                              {u"received"_s, 0},
                              {u"onlyCopy"_s, true}}),
                "a waiting file with no id and no origin");

        refuses(Api::filed({}), "a filing that does not say whether it replaced anything");
        refuses(Api::filed({{u"replacement"_s, false}}), "a filing with no entry and no path");

        refuses(Api::collision({{u"sameVolume"_s, true},
                                {u"identical"_s, false},
                                {u"occupies"_s, QJsonObject{}},
                                {u"arriving"_s, QJsonObject{}}}),
                "a collision with no path");

        refuses(Api::opened({}), "an opened import with no weight to send");
        refuses(Api::opened({{u"bytesToSend"_s, 0}}), "an opened import with no id");
        const QJsonObject open{{u"id"_s, u"imp_1"_s},
                               {u"root"_s, u"Koro"_s},
                               {u"bytesToSend"_s, 0}};
        QJsonObject broken = open;
        broken.insert(u"creates"_s, QJsonArray{QJsonObject{}});
        refuses(Api::opened(broken), "an opened import whose creation names nothing");
        broken = open;
        // A number where the volume it would land on is named: the one field of a
        // replacement that is read and then still refused for the path beside it.
        broken.insert(u"replaces"_s, QJsonArray{QJsonObject{{u"presentNumber"_s, 3.0}}});
        refuses(Api::opened(broken), "an opened import whose replacement has no path");
        broken = open;
        broken.insert(u"declarations"_s, QJsonArray{QJsonObject{}});
        refuses(Api::opened(broken), "an opened import whose declaration has no path");

        refuses(Api::received({}), "a chunk answer that does not say how much arrived");
        refuses(Api::received({{u"received"_s, 0}}), "a chunk answer with no path");

        refuses(Api::badOffset({{u"error"_s, u"x"_s}}),
                "a refused offset that does not say what the server holds");
        refuses(Api::badOffset({{u"received"_s, 0}}), "a refused offset with no sentence");

        refuses(Api::session({}), "a session that does not say what it received");
        refuses(Api::session({{u"received"_s, QJsonObject{}}}), "a session with no id");

        refuses(Api::installed({}), "a commit that does not list its orphans");
        refuses(Api::installed({{u"orphans"_s, QJsonArray{}}}), "a commit with no root");
    }
};

// Any script, from the start.
//
// A library is not French. A work is named in the language it was published in, and the
// client has no say in which: Japanese, Chinese, Arabic, Cyrillic, or a Latin name with a
// macron on it. This asserts that what the server sends is what the client holds — the
// reason every literal here is UTF-16 rather than judged ASCII one at a time.
void ReadsTheContract::anyScriptSurvives()
{
    const QList<QString> names = {
        u"ハイキュー!!"_s,          // Japanese
        u"进击的巨人"_s,             // Chinese
        u"هجوم العمالقة"_s,          // Arabic, right to left
        u"Атака титанов"_s,         // Cyrillic
        u"Haikyū — l'été"_s,        // Latin, macron and an apostrophe
    };

    for (const QString &name : names) {
        QJsonObject one = aSeries();
        one[u"name"_s] = name;
        one[u"work"_s] = name;

        const Api::Read<Api::Series> read = Api::series(one);
        QVERIFY2(read.ok(), qPrintable(read.trouble));
        QCOMPARE(read.value->name, name);
        QCOMPARE(read.value->work, name);
    }
}

QTEST_APPLESS_MAIN(ReadsTheContract)
#include "reads_the_contract.moc"
