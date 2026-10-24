// The server sends every resized page and every cover as WebP, and Qt reads WebP only through
// a plugin Ubuntu ships apart from Qt itself (`qt6-image-formats-plugins`). Without it nothing
// fails to build and nothing fails to link: on the desktop, 22/10/2026, every cover
// and every page said « Format d'image non supporté », and the plugins folder held gif, ico,
// jpeg and svg. These two tests are what turns that into a red line instead of a blank shelf.

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QTest>

using Qt::Literals::StringLiterals::operator""_s;

class ReadsWebp : public QObject
{
    Q_OBJECT

private slots:
    void qt_lists_webp_among_the_formats_it_can_read()
    {
        QVERIFY2(QImageReader::supportedImageFormats().contains("webp"),
                 "no WebP image plugin: install qt6-image-formats-plugins");
    }

    /// Listed is not decoded: a plugin that loads and reads nothing would pass the test above.
    /// `page.webp` is a lossy VP8 picture, the kind the server writes.
    void a_lossy_webp_page_decodes_to_a_picture()
    {
        QFile file(u":/fixtures/page.webp"_s);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QBuffer buffer;
        buffer.setData(bytes);
        QImageReader reader(&buffer, "webp");
        const QImage picture = reader.read();
        QVERIFY2(!picture.isNull(), qPrintable(reader.errorString()));
        QCOMPARE(picture.size(), QSize(32, 48));
    }
};

QTEST_GUILESS_MAIN(ReadsWebp)
#include "reads_webp.moc"
