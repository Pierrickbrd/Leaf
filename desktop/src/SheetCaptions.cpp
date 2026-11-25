#include "SheetCaptions.h"

#include "Words.h"

#include <QVariantList>

using namespace Qt::StringLiterals;

SheetCaptions::SheetCaptions(QObject *parent)
    : QObject(parent)
{
}

QVariantMap SheetCaptions::sheetWords() const
{
    return {{u"seriesCredits"_s, Words::seriesCredits()},
            {u"showAll"_s, Words::showEveryCredit()},
            {u"fold"_s, Words::foldTheCredits()},
            {u"readingOrders"_s, Words::readingOrders()},
            {u"arcsSection"_s, Words::arcsSection()},
            {u"arcsWindowTitle"_s, Words::arcsWindowTitle()},
            {u"arcsAdd"_s, Words::arcsAdd()},
            {u"arcsKeep"_s, Words::arcsKeep()},
            {u"chaptersSection"_s, Words::chaptersSection()},
            {u"chaptersAdd"_s, Words::chaptersAdd()},
            {u"chaptersKeep"_s, Words::chaptersKeep()},
            {u"chaptersNumber"_s, Words::chaptersColumn(u"number"_s)},
            {u"chaptersTitle"_s, Words::chaptersColumn(u"title"_s)},
            {u"chaptersPage"_s, Words::chaptersColumn(u"page"_s)},
            {u"chaptersRefused"_s, Words::chaptersRefused()},
            {u"chaptersArc"_s, Words::chaptersArcLabel()},
            {u"chaptersArcsNote"_s, Words::chaptersArcsNote()},
            {u"chaptersArcsEdit"_s, Words::chaptersArcsEdit()},
            {u"chaptersNumberFrom"_s, Words::chaptersNumberFrom()},
            {u"arcsAddSaga"_s, Words::arcsAddSaga()},
            {u"arcsArc"_s, Words::arcsKind(u"arc"_s)},
            {u"arcsAddNone"_s, Words::arcsAddNone()},
            {u"arcsNoSaga"_s, Words::arcsNoSaga()},
            {u"arcsStartAt"_s, Words::arcsStartAt()},
            {u"arcsEditionPick"_s, Words::arcsEditionPick()},
            {u"arcsInEdition"_s, Words::arcsInEdition()},
            {u"arcsMoveHint"_s, Words::arcsMoveHint()},
            {u"arcsRemove"_s, Words::arcsRemove()},
            {u"arcsName"_s, Words::arcsColumn(u"name"_s)},
            {u"arcsParent"_s, Words::arcsColumn(u"parent"_s)},
            {u"arcsUnit"_s, Words::arcsColumn(u"unit"_s)},
            {u"arcsFrom"_s, Words::arcsColumn(u"from"_s)},
            {u"arcsTo"_s, Words::arcsColumn(u"to"_s)},
            {u"arcsChapters"_s, Words::arcsUnit(u"CHAPTER"_s)},
            {u"arcsVolumes"_s, Words::arcsUnit(u"VOLUME"_s)},
            {u"arcsNote"_s, Words::arcsNote()},
            {u"arcsRefused"_s, Words::arcsRefused()},
            {u"defaultOrder"_s, Words::defaultOrder()},
            {u"subset"_s, Words::subsetOrder()}};
}

QVariantMap SheetCaptions::orderWords() const
{
    return {{u"dragHint"_s, Words::orderDragHint()},
            {u"stepColumn"_s, Words::orderStepColumn()},
            {u"unitColumn"_s, Words::orderUnitColumn()},
            {u"fromColumn"_s, Words::orderFromColumn()},
            {u"toColumn"_s, Words::orderToColumn()},
            {u"proposed"_s, Words::proposedStep()},
            {u"addWork"_s, Words::addWorkStep()},
            {u"addInclude"_s, Words::addIncludeStep()},
            {u"universe"_s, Words::universeHeading()},
            {u"orders"_s, Words::ordersHeading()},
            {u"series"_s, Words::seriesHeading()},
            {u"includeAnother"_s, Words::includeAnother()},
            {u"includeInstead"_s, Words::includeInstead()},
            {u"includeExplained"_s, Words::includeExplained()},
            {u"includedOrder"_s, Words::includedOrder()},
            {u"inheritedSteps"_s, Words::inheritedSteps()},
            {u"flatOrder"_s, Words::flatOrder()},
            {u"nothingCopied"_s, Words::nothingCopied()},
            {u"loopRefused"_s, Words::loopRefused()},
            {u"loopExplained"_s, Words::loopExplained()},
            {u"keepInclusion"_s, Words::keepInclusion()},
            {u"work"_s, Words::stepWork()},
            {u"edition"_s, Words::stepEdition()},
            {u"absent"_s, Words::absentFromDrop()},
            {u"stepKeys"_s, Words::stepKeys()},
            {u"noEdition"_s, Words::noEdition()},
            {u"chooseIncludedOrder"_s, Words::chooseIncludedOrder()},
            // By number, as a select holds its values: `code` is what the step writes.
            {u"units"_s, QVariantList{
                QVariantMap{{u"value"_s, 0}, {u"code"_s, QString()},
                            {u"label"_s, Words::orderUnit({})}},
                QVariantMap{{u"value"_s, 1}, {u"code"_s, u"VOLUME"_s},
                            {u"label"_s, Words::orderUnit(u"VOLUME"_s)}},
                QVariantMap{{u"value"_s, 2}, {u"code"_s, u"CHAPTER"_s},
                            {u"label"_s, Words::orderUnit(u"CHAPTER"_s)}}}}};
}

QString SheetCaptions::arcsEditLabel(int count) const
{
    return Words::arcsEdit(count);
}

QString SheetCaptions::arcsCountLabel(int sagas, int arcs) const
{
    return Words::arcsCount(sagas, arcs);
}

QString SheetCaptions::chaptersEditLabel(int count) const
{
    return Words::chaptersEdit(count);
}

QString SheetCaptions::chaptersWindowTitle(const QString &volume) const
{
    return Words::chaptersWindowTitle(volume);
}

QString SheetCaptions::chaptersCountLabel(int chapters, int pages) const
{
    return Words::chaptersCount(chapters, pages);
}

QString SheetCaptions::stepNumbered(int step) const
{
    return Words::stepNumbered(step);
}

QString SheetCaptions::flatSteps(int steps) const
{
    return Words::flatSteps(steps);
}

QString SheetCaptions::followsIt(const QString &included, const QString &order) const
{
    return Words::followsIt(included, order);
}

QString SheetCaptions::removeStep(const QString &step) const
{
    return Words::removeStep(step);
}
