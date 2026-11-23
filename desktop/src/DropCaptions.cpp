#include "DropCaptions.h"

#include "Card.h"
#include "Words.h"

using namespace Qt::StringLiterals;

DropCaptions::DropCaptions(QObject *parent)
    : QObject(parent)
{
}

QString DropCaptions::dropHereLabel() const
{
    return Words::dropFilesHere();
}

QString DropCaptions::dropHowLabel() const
{
    return Words::dropHow();
}

QString DropCaptions::dropWhatLabel() const
{
    return Words::dropWhat();
}

QString DropCaptions::reassuranceLabel() const
{
    return Words::chooseReassurance();
}

QString DropCaptions::chooseLeadLabel() const
{
    return Words::chooseLead();
}

QString DropCaptions::chooseFilesLabel() const
{
    return Words::chooseFiles();
}

QString DropCaptions::chooseFilesTitle() const
{
    return Words::chooseFilesTitle();
}

QString DropCaptions::chooseFolderLabel() const
{
    return Words::chooseFolder();
}

QString DropCaptions::chooseFolderTitle() const
{
    return Words::chooseFolderTitle();
}

QString DropCaptions::verifyLabel() const
{
    return Words::verifyEachFile();
}

QString DropCaptions::verifyAside() const
{
    return Words::verifyingMeans();
}

QString DropCaptions::everythingIsCheckedLabel() const
{
    return Words::everythingIsChecked();
}

QString DropCaptions::startWaitsLabel() const
{
    return Words::startWaits();
}

QString DropCaptions::startWaitsHint() const
{
    return Words::startWaitsHint();
}

QString DropCaptions::noSeriesLabel() const
{
    return Words::noSeriesForThisFile();
}

QString DropCaptions::acceptLabel() const
{
    return Words::acceptCreations();
}

QString DropCaptions::aimedDropLabel(const QString &kind) const
{
    return Words::aimedDrop(kind);
}

QString DropCaptions::toSendTotal(int files, qint64 bytes) const
{
    return Words::toSendTotal(files, bytes);
}

QString DropCaptions::readSoFar(int cards) const
{
    return Words::readSoFar(cards);
}

QString DropCaptions::heldNotResent(int files) const
{
    return Words::heldNotResent(files);
}

QString DropCaptions::readHowLabel(int origin) const
{
    return Words::readHow(origin);
}

QString DropCaptions::lineCountLabel(const QVariantMap &progress, bool brief) const
{
    const qint64 whole = progress.value(u"whole"_s).toLongLong();
    using enum Card::Counting;
    switch (Card::Counting(progress.value(u"state"_s).toInt())) {
    case Waiting:
        return Words::waitingToBeChecked();
    case Walking:
        return Words::lineCounting(progress.value(u"hashing"_s).toBool(),
                                   progress.value(u"done"_s).toLongLong(), whole, brief);
    case Over:
        if (!progress.value(u"uncovered"_s).toString().isEmpty())
            return Words::lineCheckedFileOnly(brief);
        return Words::lineChecked(progress.value(u"hashing"_s).toBool(), whole, brief);
    case None:
        break;
    }
    return {};
}

QString DropCaptions::dropProgressLabel(const QVariantMap &counts) const
{
    return Words::dropProgress({counts.value(u"foldersRead"_s).toInt(),
                                counts.value(u"foldersToRead"_s).toInt(),
                                counts.value(u"reading"_s).toInt(),
                                counts.value(u"checked"_s).toLongLong(),
                                counts.value(u"toCheck"_s).toLongLong(),
                                counts.value(u"hashing"_s).toBool(),
                                counts.value(u"deciding"_s).toInt(),
                                counts.value(u"listed"_s).toInt()});
}
