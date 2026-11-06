#include "include/Sessions/backupservice.h"

#include "include/Sessions/persistentcache.h"
#include "include/Sessions/sessions.h"
#include "include/mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>

#include <set>

QTimer BackupService::s_autosaveTimer;
bool BackupService::s_autosaveEnabled = false;
std::set<BackupService::WindowData> BackupService::s_backupWindowData;
QStringList BackupService::s_restoredDirs;

namespace {

/**
 * The process that holds this lock owns the snapshots found at start-up. A second process (for
 * example one started while the first one did not answer) must not restore, and then duplicate,
 * the windows of an instance that is still running.
 */
QLockFile* ownershipLock()
{
    static QLockFile* lock = [] {
        const QString path = PersistentCache::backupDirPath() + ".lock";
        QDir().mkpath(QFileInfo(path).absolutePath());
        auto* l = new QLockFile(path);
        // Never stale while its process is alive (a dead owner is still detected).
        l->setStaleLockTime(0);
        l->tryLock(0);
        qAddPostRoutine([] {
            ownershipLock()->unlock();
        });
        return l;
    }();
    return lock;
}

/** Snapshot folder of a window. The process id keeps folders of different runs apart. */
QString backupDirFor(const MainWindow* wnd)
{
    return PersistentCache::backupDirPath() + QStringLiteral("/window_%1_%2")
                                                  .arg(QCoreApplication::applicationPid())
                                                  .arg(reinterpret_cast<quintptr>(wnd));
}

} // namespace

bool BackupService::ownsBackupData()
{ return ownershipLock()->isLocked(); }

void BackupService::executeBackup()
{
    // Never re-entered (a modal dialog or nested event loop could fire the timer again).
    static bool running = false;
    if (running)
        return;
    running = true;
    struct Reset {
        ~Reset() { running = false; }
    } reset;

    // Restored snapshots can be dropped only once every restored document is fully loaded and
    // saved again in a snapshot of its own.
    bool stillLoading = false;
    for (const auto& wnd : MainWindow::instances()) {
        wnd->topEditorContainer()->forEachEditor([&stillLoading](int, int, EditorTabWidget*, Editor* ed) {
            stillLoading = stillLoading || ed->isLoading;
            return !stillLoading;
        });
    }

    std::set<WindowData> newData, savedData, temp;

    // Fill newData with up-to-date window data
    for (const auto& wnd : MainWindow::instances()) {
        WindowData wd;
        wd.ptr = wnd;
        wnd->topEditorContainer()->forEachEditor([&wd](int, int, EditorTabWidget*, Editor* ed) {
            wd.editors.push_back(std::make_pair(ed, ed->historyGeneration()));
            return true;
        });
        newData.insert(std::move(wd));
    }

    // Find all closed windows and remove their backups
    std::set_difference(s_backupWindowData.begin(),
        s_backupWindowData.end(),
        newData.begin(),
        newData.end(),
        std::inserter(temp, temp.end()));

    for (const auto& item : temp)
        QDir(backupDirFor(item.ptr)).removeRecursively();

    // Find all newly created windows and create their backups
    temp.clear();
    std::set_difference(newData.begin(),
        newData.end(),
        s_backupWindowData.begin(),
        s_backupWindowData.end(),
        std::inserter(temp, temp.end()));

    for (const auto& item : temp) {
        // If writeBackup() fails we don't mark this window as saved. Another attempt at saving will be made
        // next time executeBackup() runs.
        if (writeBackup(item.ptr))
            savedData.insert(item);
    }

    // Find all persisting windows and re-check whether to save them
    temp.clear();
    std::set_intersection(s_backupWindowData.begin(),
        s_backupWindowData.end(),
        newData.begin(),
        newData.end(),
        std::inserter(temp, temp.end()));

    for (const auto& oldItem : temp) { // oldItem is always from the first set (s_backupWindowData)
        const auto& newItem = *newData.find(oldItem);

        // If oldItem and newItem are fully equal, their contents haven't changed and need not be backed up...
        if (oldItem.isFullyEqual(newItem)) {
            savedData.insert(newItem);
            continue;
        }

        // ...otherwise we attempt saving the backup
        if (writeBackup(newItem.ptr))
            savedData.insert(newItem);
    }

    s_backupWindowData = savedData;

    // The snapshots restored at start-up are now duplicated by the snapshots of the restored
    // windows: delete them, or every following crash would bring back one more window.
    if (!s_restoredDirs.isEmpty() && !stillLoading && savedData.size() == newData.size()) {
        QStringList liveDirs;
        for (const auto& item : savedData)
            liveDirs.append(QDir(backupDirFor(item.ptr)).absolutePath());
        for (const QString& dir : std::as_const(s_restoredDirs)) {
            if (!liveDirs.contains(QDir(dir).absolutePath()))
                QDir(dir).removeRecursively();
        }
        s_restoredDirs.clear();
    }
}

bool BackupService::writeBackup(MainWindow* wnd)
{
    if (wnd == nullptr) {
        return false;
    }

    // Save this MainWindow as a session inside the autosave path.
    const QString cachePath = backupDirFor(wnd);
    const QString sessPath = cachePath + "/window.xml";

    TopEditorContainer* topEdCon = wnd->topEditorContainer();
    if (topEdCon == nullptr) {
        return false;
    }

    return Sessions::saveSession(wnd->getDocEngine(), wnd->topEditorContainer(), sessPath, cachePath);
}

bool BackupService::restoreFromBackup()
{
    if (!ownsBackupData())
        return false;

    const auto& backupPath = PersistentCache::backupDirPath();

    // Each window is saved as a separate session inside a subdirectory.
    // Grab all subdirs and load the session files inside.
    QDir autosaveDir(backupPath);
    autosaveDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    autosaveDir.setSorting(QDir::Time | QDir::Reversed);
    const auto& dirs = autosaveDir.entryInfoList();

    // Like Notepad++, restore silently: after a shutdown, restart or crash the user simply finds
    // all of their tabs again, including unsaved ones.
    bool restored = false;
    for (const auto& dirInfo : dirs) {
        const QString dirPath = dirInfo.absoluteFilePath();
        const QString sessPath = dirPath + "/window.xml";

        // A window that only had empty "new" tabs has nothing to bring back: opening it again
        // would just add one more empty window at every start.
        if (!Sessions::hasRestorableTabs(sessPath)) {
            QDir(dirPath).removeRecursively();
            continue;
        }

        MainWindow* wnd = new MainWindow(QStringList(), nullptr);
        Sessions::loadSession(wnd->getDocEngine(), wnd->topEditorContainer(), sessPath);
        wnd->show();
        s_restoredDirs.append(dirPath);
        restored = true;
    }

    return restored;
}

bool BackupService::detectImproperShutdown()
{
    if (!ownsBackupData())
        return false;
    QDir dir(PersistentCache::backupDirPath());
    if (!dir.exists())
        return false;
    if (dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
        dir.removeRecursively();
        return false;
    }
    return true;
}

void BackupService::enableAutosave(int intervalInSeconds)
{
    if (s_autosaveEnabled)
        return;

    static bool initializer = false;
    if (!initializer) {
        // Only create this connection once. Since we're connecting to a plain old function we can't use
        // Qt::UniqueConnection or QObject::disconnect() to make things easier.
        initializer = true;
        QObject::connect(&BackupService::s_autosaveTimer, &QTimer::timeout, &BackupService::executeBackup);

        // Disable the autosave timer when the application goes out of focus.
        QObject::connect(qApp, &QGuiApplication::applicationStateChanged, [](Qt::ApplicationState state) {
            if (!s_autosaveEnabled)
                return;

            switch (state) {
            case Qt::ApplicationInactive:
                // Take a snapshot right away: the user may now shut the computer down.
                executeBackup();
                s_autosaveTimer.stop();
                break;
            case Qt::ApplicationActive:
                s_autosaveTimer.start();
                break;
            default:
                break;
            }
        });
    }

    s_autosaveEnabled = true;
    s_autosaveTimer.setTimerType(Qt::VeryCoarseTimer);
    s_autosaveTimer.setInterval(intervalInSeconds * 1000);
    s_autosaveTimer.start();
}

void BackupService::disableAutosave()
{
    if (!s_autosaveEnabled)
        return;

    s_autosaveEnabled = false;
    s_autosaveTimer.stop();
    clearBackupData();
}

void BackupService::clearBackupData()
{
    if (ownsBackupData()) {
        QDir backupDir(PersistentCache::backupDirPath());
        if (backupDir.exists())
            backupDir.removeRecursively();
    } else {
        // Another instance owns the folder: only remove what this process wrote.
        for (const auto& item : s_backupWindowData)
            QDir(backupDirFor(item.ptr)).removeRecursively();
    }

    s_backupWindowData.clear();
    s_restoredDirs.clear();
}

void BackupService::pause()
{ s_autosaveTimer.stop(); }

void BackupService::resume()
{
    if (s_autosaveEnabled) {
        // The previous interval is persisted in s_autosaveTimer.interval()
        s_autosaveTimer.start();
    }
}
