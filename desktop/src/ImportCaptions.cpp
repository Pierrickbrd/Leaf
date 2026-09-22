#include "ImportCaptions.h"

#include "Imports.h"
#include "Words.h"

#include <QDebug>
#include <QQmlEngine>

ImportCaptions *ImportCaptions::create(QQmlEngine *engine, QJSEngine *)
{
    auto *imports = engine->singletonInstance<Imports *>(qmlTypeId("Leaf", 1, 0, "Imports"));
    if (!imports) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Imports singleton — the import dialog will "
                              "have no words");
    }
    return new ImportCaptions(imports);
}

ImportCaptions::ImportCaptions(Imports *imports, QObject *parent)
    : QObject(parent)
    , m_imports(imports)
{
    // `waitingLabel` quotes `Imports::deciding()`, which moves as the queue is answered —
    // re-announcing on every `Imports::changed` is what keeps a binding on it from freezing
    // on whatever count happened to be true when the dialog first opened.
    if (m_imports)
        connect(m_imports, &Imports::changed, this, &ImportCaptions::changed);
}

QString ImportCaptions::title() const
{
    return Words::importing();
}

QString ImportCaptions::dropHereLabel() const
{
    return Words::dropFilesHere();
}

QString ImportCaptions::dropHowLabel() const
{
    return Words::dropHow();
}

QString ImportCaptions::cancelLabel() const
{
    return Words::cancel();
}

QString ImportCaptions::backLabel() const
{
    return Words::goBack();
}

QString ImportCaptions::startLabel() const
{
    return Words::startImport();
}

QString ImportCaptions::pauseLabel() const
{
    return Words::pauseIt();
}

QString ImportCaptions::resumeLabel() const
{
    return Words::resumeIt();
}

QString ImportCaptions::abandonLabel() const
{
    return Words::abandonIt();
}

QString ImportCaptions::noSeriesLabel() const
{
    return Words::noSeriesForThisFile();
}

QString ImportCaptions::acceptLabel() const
{
    return Words::acceptCreations();
}

QString ImportCaptions::verifyLabel() const
{
    return Words::verifyEachFile();
}

QString ImportCaptions::verifyAside() const
{
    return Words::verifyingMeans();
}

QString ImportCaptions::chooseLeadLabel() const
{
    return Words::chooseLead();
}

QString ImportCaptions::dropSomethingElseLabel() const
{
    return Words::dropSomethingElse();
}

QString ImportCaptions::chooseFilesLabel() const
{
    return Words::chooseFiles();
}

QString ImportCaptions::chooseFilesTitle() const
{
    return Words::chooseFilesTitle();
}

QString ImportCaptions::chooseFolderLabel() const
{
    return Words::chooseFolder();
}

QString ImportCaptions::chooseFolderTitle() const
{
    return Words::chooseFolderTitle();
}

QString ImportCaptions::waitingLabel() const
{
    return Words::decisionsWaiting(m_imports ? m_imports->deciding() : 0);
}

