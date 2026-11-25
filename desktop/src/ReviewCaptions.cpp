#include "ReviewCaptions.h"

#include "Words.h"

ReviewCaptions::ReviewCaptions(QObject *parent)
    : QObject(parent)
{
}

QString ReviewCaptions::missingLabel() const
{
    return Words::missing();
}

QString ReviewCaptions::requiredLegend() const
{
    return Words::requiredLegend();
}

QString ReviewCaptions::editLabel() const
{
    return Words::editInformation();
}

QString ReviewCaptions::searchLabel() const
{
    return Words::searchTheDrop();
}

QString ReviewCaptions::reviewNextLabel() const
{
    return Words::reviewNext();
}

QString ReviewCaptions::tickAllLabel() const
{
    return Words::tickAll();
}

QString ReviewCaptions::moveVolumeLabel() const
{
    return Words::moveVolume();
}

QString ReviewCaptions::writesLabel() const
{
    return Words::whatWillBeWritten();
}

QString ReviewCaptions::greyedRolesLabel() const
{
    return Words::greyedRolesHint();
}

QString ReviewCaptions::clueForMark() const
{
    return Words::clueForMark();
}

QString ReviewCaptions::clueAgainstMark() const
{
    return Words::clueAgainstMark();
}

QString ReviewCaptions::keepLabel() const
{
    return Words::aimChip(Words::Aim::Keep);
}

QString ReviewCaptions::newValueLabel() const
{
    return Words::newValue();
}

QString ReviewCaptions::addWordLabel(const QString &key) const
{
    return Words::addWord(key);
}

QString ReviewCaptions::removeWordLabel(const QString &field, const QString &word) const
{
    return Words::removeWordFrom(field, word);
}

QString ReviewCaptions::spliceCurrentSide() const
{
    return Words::spliceCurrentSide();
}

QString ReviewCaptions::spliceSourceSide() const
{
    return Words::spliceSourceSide();
}

QString ReviewCaptions::spliceFromLabel() const
{
    return Words::spliceFromLabel();
}

QString ReviewCaptions::spliceToLabel() const
{
    return Words::spliceToLabel();
}

QString ReviewCaptions::pagesRange(int from, int to) const
{
    return Words::pagesRange(from, to);
}
