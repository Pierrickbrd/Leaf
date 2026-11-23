#include "ReadingExport.h"

#include "Words.h"

#include <QDebug>
#include <QSaveFile>

using namespace Qt::StringLiterals;

ReadingExport *ReadingExport::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — the reading cannot be "
                              "exported");
    }
    return new ReadingExport(server);
}

ReadingExport::ReadingExport(Server *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
}

QString ReadingExport::label() const
{
    return Words::exportReading();
}

QString ReadingExport::explained() const
{
    return Words::exportExplained();
}

QString ReadingExport::name() const
{
    return Words::readingExportName();
}

void ReadingExport::save(const QUrl &where)
{
    if (m_exporting || !m_server || !where.isLocalFile())
        return;
    m_exporting = true;
    m_failed = false;
    m_sentence.clear();
    emit changed();
    const QString path = where.toLocalFile();
    m_server->getFile(u"/profile/export"_s, this, [this, path](const Server::Answer &answer) {
        m_exporting = false;
        if (!answer.went()) {
            m_failed = true;
            m_sentence = answer.trouble;
        } else {
            // Whole or not at all: a half-written record of reading opens, and is wrong.
            QSaveFile out(path);
            if (!out.open(QIODevice::WriteOnly) || out.write(answer.file) != answer.file.size()
                || !out.commit()) {
                m_failed = true;
                m_sentence = Words::couldNotWrite(path);
            } else {
                m_sentence = Words::readingExported(path);
            }
        }
        emit changed();
    });
}
