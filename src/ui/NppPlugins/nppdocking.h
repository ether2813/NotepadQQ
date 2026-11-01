#ifndef NQQ_NPPDOCKING_H
#define NQQ_NPPDOCKING_H

#include <QIcon>
#include <QObject>
#include <QPointer>
#include <QString>

#include <vector>

#include <windows.h>

class QDockWidget;
class QMainWindow;

/**
 * @brief Hosts Notepad++ plugin docking panels (NPPM_DMM* messages) in Qt dock widgets.
 *
 * The plugin's dialog HWND is embedded with QWindow::fromWinId() + createWindowContainer(),
 * so it stays a genuine Win32 dialog driven by the plugin's own dialog procedure. Closing,
 * floating and docking the panel send the same WM_NOTIFY (DMN_CLOSE / DMN_FLOAT / DMN_DOCK)
 * notifications Notepad++ sends.
 */
class NppDockManager : public QObject {
    Q_OBJECT
public:
    NppDockManager(QMainWindow* window, HWND nppHandle, QObject* parent = nullptr);
    ~NppDockManager() override;

    /** NPPM_DMMREGASDCKDLG. @p data points to a Notepad++ tTbData structure. */
    bool registerDialog(const void* data);
    bool show(HWND client);
    bool hide(HWND client);
    bool updateDisplayInfo(HWND client);
    HWND findClient(const QString& windowName, const QString& moduleName) const;
    bool viewOtherTab(const QString& windowName);
    bool isRegistered(HWND client) const;

private:
    struct Panel {
        HWND client = nullptr;
        const void* data = nullptr; ///< tTbData owned by the plugin (it must stay alive, as in Notepad++)
        QString name;
        QString module;
        int dlgId = 0;
        QPointer<QDockWidget> dock;
        bool programmaticVisibilityChange = false;
    };

    Panel* find(HWND client);
    const Panel* find(HWND client) const;
    void refreshTitle(Panel& panel);
    void sendDockNotification(HWND client, UINT code, int container = 0);

    QMainWindow* m_window;
    HWND m_nppHandle;
    std::vector<Panel> m_panels;
};

#endif // NQQ_NPPDOCKING_H
