#include "include/EditorNS/asyncrequesttracker.h"
#include "include/EditorNS/defer.h"
#include "include/Extensions/extension.h"
#include "include/Search/filesearcher.h"
#include "include/Search/searchhelpers.h"
#include "include/commandlineopenruntime.h"
#include "include/docengine.h"
#include "include/documentcontroller.h"
#include "include/editoruicontroller.h"
#include "include/encodingdetector.h"
#include "include/localsockethelpers.h"
#include "include/notepadqq.h"

#include <QElapsedTimer>
#include <QFile>
#include <QIODevice>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <optional>

using namespace std::chrono_literals;

// Supplies a deterministic invalid runtime so Extension can be tested without launching Node.js.
QString Notepadqq::nodejsPath()
{ return QStringLiteral("/definitely-not-a-node-runtime"); }

// Minimal file reader used by FileSearcher (DocEngine itself is not linked into this test).
DocEngine::DecodedText DocEngine::readToString(QFile* file)
{
    DecodedText decoded;
    if (!file->open(QIODevice::ReadOnly)) {
        decoded.error = true;
        return decoded;
    }
    decoded.text = QString::fromUtf8(file->readAll());
    return decoded;
}

class CppCorrectnessTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void asyncRequestTrackerResolvesMatchingReplyOnce();
    void asyncRequestTrackerRejectsMissingReplyAfterTimeout();
    void asyncRequestTrackerRejectsPendingPromisesOnDestruction();
    void asyncRequestTrackerSetsLegacyExceptionOnTimeout();
    void asyncRequestTrackerCallsLegacyCallbackOnlyOnSuccess();
    void deferredCallbackIsDiscardedWhenContextIsDestroyed();
    void cancellationRequestsInterruptionAndStopsFilesystemSearch();
    void searchPlainText_handlesContentEndingInLf();
    void localSocketProbeDoesNotLeakFailedConnection();
    void localSocketDeletesAcceptedPeerAfterDisconnect();
    void extensionOwnsRuntimeProcessForImmediateDestruction();
    void commandLineOpenContinuesOnlyAfterLoadingCompletes();
    void commandLineOpenDiscardsContinuationWhenOwnerIsDestroyed();
    void documentControllerHasExpectedConstructionContract();
    void editorUiControllerHasExpectedConstructionContract();
    void encodingDetectorRecognisesUtf8();
    void linePositions_data();
    void linePositions();
};

// Guards the extracted controller's QObject, collaborator-constructor, and non-copyable API shape.
void CppCorrectnessTest::documentControllerHasExpectedConstructionContract()
{
    QVERIFY((std::is_base_of_v<QObject, DocumentController>));
    QVERIFY((std::is_constructible_v<DocumentController, MainWindow&, DocEngine&, TopEditorContainer&, NqqSettings&>));
    QVERIFY((!std::is_copy_constructible_v<DocumentController>));
}

// Guards the editor UI controller's QObject, collaborator-constructor, and non-copyable API shape.
void CppCorrectnessTest::editorUiControllerHasExpectedConstructionContract()
{
    QVERIFY((std::is_base_of_v<QObject, EditorUiController>));
    QVERIFY((std::is_constructible_v<EditorUiController,
        MainWindow&,
        Ui::MainWindow&,
        DocEngine&,
        TopEditorContainer&,
        NqqSettings&>));
    QVERIFY((!std::is_copy_constructible_v<EditorUiController>));
}

// Guards the bridge dispatch against invoking an Editor callback after the Editor is destroyed.
void CppCorrectnessTest::deferredCallbackIsDiscardedWhenContextIsDestroyed()
{
    bool called = false;
    auto* context = new QObject;

    EditorNS::deferToObject(context, [&] { called = true; });
    delete context;

    QCoreApplication::processEvents();
    QVERIFY(!called);
}

// Ensures a failed single-instance probe does not leave a socket child behind.
void CppCorrectnessTest::localSocketProbeDoesNotLeakFailedConnection()
{
    QObject owner;
    const int before = owner.findChildren<QLocalSocket*>().size();
    QVERIFY(LocalSocketHelpers::probe(
                &owner, QStringLiteral("nqq-nonexistent-socket"), [](QLocalSocket*) { return false; }) == nullptr);
    QCOMPARE(owner.findChildren<QLocalSocket*>().size(), before);
}

// Ensures accepted server sockets are released automatically when their peer disconnects.
void CppCorrectnessTest::localSocketDeletesAcceptedPeerAfterDisconnect()
{
    auto* accepted = new QLocalSocket;
    QPointer<QLocalSocket> observed = accepted;
    LocalSocketHelpers::deleteOnDisconnect(accepted);
    QVERIFY(QMetaObject::invokeMethod(accepted, "disconnected", Qt::DirectConnection));
    QTRY_VERIFY(observed.isNull());
}

// Ensures an extension owns its runtime process even when shutdown stops the event loop.
void CppCorrectnessTest::extensionOwnsRuntimeProcessForImmediateDestruction()
{
    QTemporaryDir extensionDir;
    QVERIFY(extensionDir.isValid());
    QFile manifest(extensionDir.filePath(QStringLiteral("nqq-manifest.json")));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    manifest.write(R"({"name":"test extension","runtime":"nodejs","main":"index.js"})");
    manifest.close();

    auto* extension = new Extensions::Extension(extensionDir.path(), QStringLiteral("test-socket"));
    const QList<QProcess*> processes = extension->findChildren<QProcess*>();
    QCOMPARE(processes.size(), 1);
    QPointer<QProcess> process = processes.constFirst();

    delete extension;
    QVERIFY(process.isNull());
}

// Ensures command-line follow-up work is deferred until document loading has completed.
void CppCorrectnessTest::commandLineOpenContinuesOnlyAfterLoadingCompletes()
{
    std::optional<QtPromise::QPromiseResolve<void>> resolve;
    auto loading =
        QtPromise::QPromise<void>([&resolve](const QtPromise::QPromiseResolve<void>& resolver) { resolve = resolver; });
    QObject owner;
    bool continued = false;

    CommandLineOpenRuntime::continueAfterLoading(loading, &owner, [&] { continued = true; });
    QVERIFY(!continued);

    (*resolve)();
    QTRY_VERIFY(continued);
}

// Ensures a closing window discards command-line follow-up work before document loading completes.
void CppCorrectnessTest::commandLineOpenDiscardsContinuationWhenOwnerIsDestroyed()
{
    std::optional<QtPromise::QPromiseResolve<void>> resolve;
    auto loading =
        QtPromise::QPromise<void>([&resolve](const QtPromise::QPromiseResolve<void>& resolver) { resolve = resolver; });
    auto* owner = new QObject;
    bool continued = false;

    CommandLineOpenRuntime::continueAfterLoading(loading, owner, [&] { continued = true; });
    delete owner;
    (*resolve)();

    QCoreApplication::processEvents();
    QVERIFY(!continued);
}

// Guards single settlement and late-duplicate rejection.
void CppCorrectnessTest::asyncRequestTrackerResolvesMatchingReplyOnce()
{
    EditorNS::AsyncRequestTracker tracker(this, 100ms);
    int resolutionCount = 0;
    QVariant resolvedValue;

    auto promise = QtPromise::QPromise<QVariant>(
        [&](const QtPromise::QPromiseResolve<QVariant>& resolve, const QtPromise::QPromiseReject<QVariant>& reject) {
            tracker.trackPromise(17, QStringLiteral("C_FUN_TEST"), resolve, reject);
        });
    promise.then([&](const QVariant& value) {
        ++resolutionCount;
        resolvedValue = value;
    });

    QVERIFY(tracker.resolve(17, QStringLiteral("reply")));
    QTRY_COMPARE(resolutionCount, 1);
    QCOMPARE(resolvedValue, QVariant(QStringLiteral("reply")));
    QCOMPARE(tracker.pendingCount(), 0);
    QVERIFY(!tracker.resolve(17, QStringLiteral("duplicate")));
    QCoreApplication::processEvents();
    QCOMPARE(resolutionCount, 1);
}

// Guards bounded QtPromise rejection and record removal when no reply arrives.
void CppCorrectnessTest::asyncRequestTrackerRejectsMissingReplyAfterTimeout()
{
    EditorNS::AsyncRequestTracker tracker(this, 20ms);
    bool rejected = false;

    auto promise = QtPromise::QPromise<QVariant>(
        [&](const QtPromise::QPromiseResolve<QVariant>& resolve, const QtPromise::QPromiseReject<QVariant>& reject) {
            tracker.trackPromise(18, QStringLiteral("C_FUN_TIMEOUT"), resolve, reject);
        });
    promise.fail([&](const std::runtime_error&) {
        rejected = true;
        return QVariant();
    });

    QTRY_COMPARE_WITH_TIMEOUT(tracker.pendingCount(), 0, 250);
    QTRY_VERIFY_WITH_TIMEOUT(rejected, 250);
}

// Guards teardown rejection of pending QtPromise requests.
void CppCorrectnessTest::asyncRequestTrackerRejectsPendingPromisesOnDestruction()
{
    bool rejected = false;
    auto* tracker = new EditorNS::AsyncRequestTracker(this, 100ms);

    auto promise = QtPromise::QPromise<QVariant>(
        [&](const QtPromise::QPromiseResolve<QVariant>& resolve, const QtPromise::QPromiseReject<QVariant>& reject) {
            tracker->trackPromise(19, QStringLiteral("C_FUN_DESTROY"), resolve, reject);
        });
    promise.fail([&](const std::runtime_error&) {
        rejected = true;
        return QVariant();
    });

    QCOMPARE(tracker->pendingCount(), 1);
    delete tracker;
    QTRY_VERIFY_WITH_TIMEOUT(rejected, 250);
}

// Guards timeout propagation through the compatibility std::future API.
void CppCorrectnessTest::asyncRequestTrackerSetsLegacyExceptionOnTimeout()
{
    EditorNS::AsyncRequestTracker tracker(this, 20ms);
    auto result = std::make_shared<std::promise<QVariant>>();
    auto future = result->get_future();

    tracker.trackLegacy(20, QStringLiteral("C_FUN_LEGACY_TIMEOUT"), result, nullptr);

    QTRY_COMPARE_WITH_TIMEOUT(tracker.pendingCount(), 0, 250);
    QVERIFY(future.wait_for(0ms) == std::future_status::ready);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, future.get());
}

// Guards the legacy callback against timeout invocation while preserving successful delivery.
void CppCorrectnessTest::asyncRequestTrackerCallsLegacyCallbackOnlyOnSuccess()
{
    EditorNS::AsyncRequestTracker tracker(this, 20ms);
    int callbackCount = 0;
    QVariant callbackValue;

    auto timedOutResult = std::make_shared<std::promise<QVariant>>();
    tracker.trackLegacy(
        21, QStringLiteral("C_FUN_CALLBACK_TIMEOUT"), timedOutResult, [&](const QVariant&) { ++callbackCount; });
    QTRY_COMPARE_WITH_TIMEOUT(tracker.pendingCount(), 0, 250);
    QCOMPARE(callbackCount, 0);

    auto successfulResult = std::make_shared<std::promise<QVariant>>();
    tracker.trackLegacy(22, QStringLiteral("C_FUN_CALLBACK_SUCCESS"), successfulResult, [&](const QVariant& value) {
        ++callbackCount;
        callbackValue = value;
    });
    QVERIFY(tracker.resolve(22, 42));

    QTRY_COMPARE(callbackCount, 1);
    QCOMPARE(callbackValue, QVariant(42));
    QCOMPARE(tracker.pendingCount(), 0);
}

void CppCorrectnessTest::cancellationRequestsInterruptionAndStopsFilesystemSearch()
{
    SearchConfig config;
    config.searchScope = SearchConfig::ScopeFileSystem;

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    const QByteArray fileContents = QByteArrayLiteral("searchable text\n") + QByteArray(64 * 1024, 'x');
    for (int fileIndex = 0; fileIndex < 200; ++fileIndex) {
        QFile file(temporaryDirectory.filePath(QStringLiteral("file-%1.txt").arg(fileIndex)));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(fileContents), qint64(fileContents.size()));
    }

    config.directory = temporaryDirectory.path();
    config.filePattern = QStringLiteral("*.txt");
    config.searchString = QStringLiteral("searchable");

    FileSearcher* searcher = FileSearcher::prepareAsyncSearch(config);
    searcher->start();
    QTRY_VERIFY_WITH_TIMEOUT(searcher->isRunning(), 1000);
    searcher->cancel();
    QVERIFY(searcher->isInterruptionRequested());
    QVERIFY(searcher->wait(2000));
    QVERIFY(searcher->getResult().results.size() < 200);
    delete searcher;
}

void CppCorrectnessTest::searchPlainText_handlesContentEndingInLf()
{
    SearchConfig config;
    config.searchString = "needle";

    const DocResult result = FileSearcher::searchPlainText(config, "needle\n");

    QCOMPARE(result.results.size(), 1);
    QCOMPARE(result.results.constFirst().lineNumber, 1);
}

// Guards the BOM-less charset detection used when opening files.
void CppCorrectnessTest::encodingDetectorRecognisesUtf8()
{
    const QByteArray ascii("plain ascii text\n");
    QVERIFY(EncodingDetector::isValidUtf8(ascii.constData(), ascii.size()));
    const QByteArray utf8 = QStringLiteral("gr\u00FC\u00DFe \u65E5\u672C").toUtf8();
    QVERIFY(EncodingDetector::isValidUtf8(utf8.constData(), utf8.size()));
    const QByteArray latin1("gr\xFC\xDF", 4);
    QVERIFY(!EncodingDetector::isValidUtf8(latin1.constData(), latin1.size()));
    const QByteArray detected = EncodingDetector::detect(utf8.constData(), utf8.size());
    QVERIFY(detected.isEmpty() || detected.toUpper().contains("UTF-8"));
}

void CppCorrectnessTest::linePositions_data()
{
    QTest::addColumn<QString>("data");
    QTest::addColumn<std::vector<int>>("expected");

    QTest::newRow("empty") << "" << std::vector<int>({0, 0});
    QTest::newRow("no-line-break") << "abc" << std::vector<int>({0, 3});
    QTest::newRow("terminal-lf") << "abc\n" << std::vector<int>({0, 4, 4});
    QTest::newRow("terminal-cr") << "abc\r" << std::vector<int>({0, 4, 4});
    QTest::newRow("crlf") << "a\r\nb" << std::vector<int>({0, 3, 4});
}

void CppCorrectnessTest::linePositions()
{
    QFETCH(QString, data);
    QFETCH(std::vector<int>, expected);

    QCOMPARE(SearchHelpers::linePositions(data), expected);
}

QTEST_GUILESS_MAIN(CppCorrectnessTest)

#include "tst_cppcorrectness.moc"
