#ifndef AUTOSAVE_H
#define AUTOSAVE_H

#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <set>
#include <tuple>

namespace EditorNS {
class Editor;
}
class MainWindow;

/**
 * @brief The BackupService class handles automatic saving of currently open windows, tabs, and documents.
 *        When this system is used, clearBackupData() should be called at program shutdown.
 *        If, on program start, there is backup data to be found, the system will assume an improper shutdown.
 */
class BackupService {
public:
    /**
     * @brief restoreFromAutosave Reads the autosave sessions and recreates
     *        all windows and their tabs.
     */
    static bool restoreFromBackup();

    /**
     * @brief detectImproperShutdown
     * @return True if the system suspects that Nqq has crashed. This means backup data was found.
     */
    static bool detectImproperShutdown();

    /**
     * @brief enableAutosave Starts periodic autosaving.
     */
    static void enableAutosave(int intervalInSeconds);

    /**
     * @brief disableAutosave Stops periodic autosaving.
     */
    static void disableAutosave();

    static void clearBackupData();

    /**
     * @brief Pause the timer, if it is running.
     */
    static void pause();
    /**
     * @brief Resume the timer, if it was paused.
     *        You can safely call this method even if the
     *        backup service is disabled by the user, as
     *        the timer is resumed only if it was previously
     *        running.
     */
    static void resume();

    /**
     * @brief Writes a snapshot of every window whose documents changed since the last one.
     *        Called periodically, when the application loses focus and when Windows ends the
     *        user session (shutdown, restart, log off).
     */
    static void executeBackup();

    /**
     * @brief Whether this process owns the snapshot folder (only one running instance does).
     *        Only the owner restores snapshots at start-up.
     */
    static bool ownsBackupData();

private:
    static QTimer s_autosaveTimer;
    static bool s_autosaveEnabled;

    /**
     * @brief The WindowData struct contains a list Editor*'s and the history generation
     *        at the time of the last autosave. With every new autosave this data will be
     *        compared to the MainWindow's current list of Editor*'s and their history gens
     *        to determine whether the MainWindow has changed since the last save.
     */
    struct WindowData {
        MainWindow* ptr;
        std::vector<std::pair<EditorNS::Editor*, int>> editors;

        // Note this is only a shallow comparison. Do deep compares using isFullyEqual().
        bool operator==(const WindowData& other) const { return ptr == other.ptr; }
        bool operator<(const WindowData& other) const { return ptr < other.ptr; }

        bool isFullyEqual(const WindowData& other) const { return ptr == other.ptr && editors == other.editors; }
    };

    /**
     * @brief s_backupWindowData contains the WindowData at the time of the last backup.
     */
    static std::set<WindowData> s_backupWindowData;

    /**
     * @brief Snapshot folders restored at start-up. They are deleted once the restored windows
     *        have snapshots of their own.
     */
    static QStringList s_restoredDirs;

    /**
     * @brief writeBackup Writes a backup of the given MainWindow into a unique location inside the backupCache
     * @return True if the backup was created successfully
     */
    static bool writeBackup(MainWindow* wnd);
};

/**
 * @brief Utility class used to pause the BackupService within a block of code.
 *        The service is automatically resumed as soon as the program exits
 *        the block.
 */
class BackupServicePauser {
public:
    inline explicit BackupServicePauser() {}

    // We can't pause it from the constructor because then we get a "unused var" warning
    // whenever this object is instantiated.
    inline void pause() { BackupService::pause(); }

    inline ~BackupServicePauser() { BackupService::resume(); }
};

#endif // AUTOSAVE_H
