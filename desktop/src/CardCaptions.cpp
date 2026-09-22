#include "CardCaptions.h"

#include "Manifest.h"
#include "Words.h"

#include <QDir>
#include <QFileInfo>

CardCaptions::CardCaptions(QObject *parent)
    : QObject(parent)
{
}

QString CardCaptions::stageLabel(int stage) const
{
    return Words::importStage(Words::Importing(stage));
}

QString CardCaptions::stageAnd(int stage, qint64 sent, qint64 whole) const
{
    return Words::stageAnd(stageLabel(stage), whole > 0 ? Words::howFar(sent, whole) : QString());
}

QString CardCaptions::nodeLine(int level, const QString &state, const QString &holds) const
{
    return Words::nodeLine(Manifest::Level(level), state, holds);
}

QString CardCaptions::concern(const QString &said) const
{
    return Words::concern(said);
}

QString CardCaptions::tryingAgainIn(int seconds) const
{
    return Words::tryingAgainIn(seconds);
}

QString CardCaptions::levelLabel(int level) const
{
    return Words::level(Manifest::Level(level));
}

QString CardCaptions::levelIcon(int level) const
{
    return Words::levelIcon(Manifest::Level(level));
}

QString CardCaptions::willCreateLabel(const QString &kind, const QString &name) const
{
    return Words::willCreate(kind, name);
}

QString CardCaptions::alreadyElsewhereLabel(const QString &name, const QString &from) const
{
    // The folder it sits in, not the path it sits at. A reader recognises « Mangas » and
    // reads past « /srv/leaf/library/Mangas », and the dialog is already a wall of words.
    const QFileInfo about(from);
    const QString folder = about.dir().dirName();
    return Words::alreadyElsewhere(name, folder);
}
