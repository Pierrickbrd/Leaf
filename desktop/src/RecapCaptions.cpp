#include "RecapCaptions.h"

#include "Words.h"

RecapCaptions::RecapCaptions(QObject *parent)
    : QObject(parent)
{
}

QString RecapCaptions::recapSeries() const { return Words::recapSeries(); }
QString RecapCaptions::recapVolumes() const { return Words::recapVolumes(); }
QString RecapCaptions::recapSize() const { return Words::recapSize(); }
QString RecapCaptions::recapState() const { return Words::recapState(); }
QString RecapCaptions::recapVerifies() const { return Words::recapVerifies(); }
QString RecapCaptions::recapFoot() const { return Words::recapFoot(); }
QString RecapCaptions::sendingFoot() const { return Words::sendingFoot(); }
QString RecapCaptions::lineStart() const { return Words::lineStart(); }
QString RecapCaptions::lineStop() const { return Words::lineStop(); }
QString RecapCaptions::lineRetry() const { return Words::lineRetry(); }
QString RecapCaptions::lineCancel() const { return Words::lineCancel(); }

QString RecapCaptions::recapTitle(int files) const
{
    return Words::recapTitle(files);
}

QString RecapCaptions::recapAbout(qint64 bytes, int cards) const
{
    return Words::recapAbout(bytes, cards);
}

QString RecapCaptions::sendingTitle(int done, int whole) const
{
    return Words::sendingTitle(done, whole);
}

QString RecapCaptions::sendingAbout(qint64 sent, qint64 whole) const
{
    return Words::sendingAbout(sent, whole);
}

QString RecapCaptions::sentTitle(int done, int whole) const
{
    return Words::sentTitle(done, whole);
}

QString RecapCaptions::bannerExplains(bool going, bool finished) const
{
    return Words::bannerExplains(going, finished);
}

QString RecapCaptions::sentAbout(qint64 bytes, int failed) const
{
    return Words::sentAbout(bytes, failed);
}

QString RecapCaptions::percentOf(double fraction) const
{
    return Words::percentSent(fraction);
}

QString RecapCaptions::sizeOf(qint64 bytes) const
{
    return Words::size(bytes);
}
