#include "ComicInfo.h"

#include <QHash>
#include <QStringConverter>
#include <QXmlStreamReader>

using Qt::Literals::StringLiterals::operator""_s;

namespace {

// Where a tag's text goes once it closes. `path` holds the ancestors, not the tag itself.
// `tags.contains` and not an overwrite: the first spelling of a tag wins.
void record(const QStringList &path, const QString &tag, const QString &value,
            QHash<QString, QString> &tags, QStringList &chapters)
{
    if (value.isEmpty())
        return;
    if (path.contains(u"Chapter"_s)) {
        if (tag == u"Name"_s)
            chapters.append(value);
    } else if (path.size() <= 1 && !tags.contains(tag)) {
        tags.insert(tag, value);
    }
}

} // namespace

std::optional<ComicInfo::Fields> ComicInfo::read(const QByteArray &xml, bool *unreadable)
{
    bool ignored = false;
    bool &refused = unreadable ? *unreadable : ignored;
    refused = true;
    QStringDecoder decoder(QStringConverter::Utf8);
    QString text = decoder(xml);
    if (decoder.hasError())
        return std::nullopt;
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);

    // From a string, so that the parser has no encoding to guess and the declaration of the
    // file cannot make it decode the bytes a second time.
    QXmlStreamReader reader(text);
    QHash<QString, QString> tags;
    QStringList chapters;
    QStringList path;
    QString current;

    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartElement:
            path.append(reader.name().toString());
            current.clear();
            break;
        case QXmlStreamReader::Characters:
            // The server's reader drops CDATA events, so `<Title><![CDATA[x]]></Title>` is
            // empty there; reading it here would announce a title the server will not keep.
            if (!reader.isCDATA())
                current += reader.text();
            break;
        case QXmlStreamReader::EndElement: {
            const QString tag = path.takeLast();
            const QString value = current.trimmed();
            current.clear();
            record(path, tag, value, tags, chapters);
            break;
        }
        case QXmlStreamReader::DTD:
        // Defence only: Qt normally stops on an undeclared entity with an error before this
        // token is delivered, but an entity it hands back unresolved must not pass as text.
        case QXmlStreamReader::EntityReference:
            return std::nullopt;
        default:
            break;
        }
    }
    if (reader.hasError())
        return std::nullopt;
    refused = false;
    if (tags.isEmpty() && chapters.isEmpty())
        return std::nullopt;

    const auto get = [&tags](const QString &name) { return tags.value(name); };

    Fields out;
    out.chapters = chapters;
    bool ok = false;
    const double number = QString(get(u"Number"_s)).replace(u',', u'.').toDouble(&ok);
    if (ok)
        out.number = number;
    out.title = get(u"Title"_s);
    out.isbn = get(u"GTIN"_s);
    out.summary = get(u"Summary"_s);
    if (const QString year = get(u"Year"_s); !year.isEmpty()) {
        QStringList parts{year};
        for (const QString &name : {u"Month"_s, u"Day"_s})
            if (const QString part = get(name); !part.isEmpty())
                parts.append(part);
        out.publishedOn = parts.join(u'-');
    }
    out.series = get(u"Series"_s);
    out.publisher = get(u"Publisher"_s);
    out.language = get(u"LanguageISO"_s);
    out.author = get(u"Writer"_s);
    out.artist = get(u"Penciller"_s);
    out.arc = get(u"StoryArc"_s);
    const int count = get(u"Count"_s).toInt(&ok);
    if (ok)
        out.volumeCount = count;
    if (get(u"Manga"_s).contains(u"RightToLeft"_s))
        out.readingDirection = u"RIGHT_TO_LEFT"_s;
    for (const QString &piece : get(u"Genre"_s).split(u',')) {
        const QString genre = piece.trimmed();
        if (!genre.isEmpty())
            out.genres.append(genre);
    }
    return out;
}
