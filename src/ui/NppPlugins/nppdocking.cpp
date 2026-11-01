#include "nppdocking.h"

#include "npp/Docking.h"
#include "npp/dockingResource.h"

#include <QDockWidget>
#include <QImage>
#include <QMainWindow>
#include <QPixmap>
#include <QWindow>

namespace {

Qt::DockWidgetArea areaFromMask(UINT mask)
{
    switch ((mask >> 28) & 0x3) {
    case CONT_LEFT:
        return Qt::LeftDockWidgetArea;
    case CONT_TOP:
        return Qt::TopDockWidgetArea;
    case CONT_BOTTOM:
        return Qt::BottomDockWidgetArea;
    case CONT_RIGHT:
    default:
        return Qt::RightDockWidgetArea;
    }
}

int containerFromArea(Qt::DockWidgetArea area)
{
    switch (area) {
    case Qt::LeftDockWidgetArea:
        return CONT_LEFT;
    case Qt::TopDockWidgetArea:
        return CONT_TOP;
    case Qt::BottomDockWidgetArea:
        return CONT_BOTTOM;
    default:
        return CONT_RIGHT;
    }
}

} // namespace

NppDockManager::NppDockManager(QMainWindow* window, HWND nppHandle, QObject* parent)
    : QObject(parent)
    , m_window(window)
    , m_nppHandle(nppHandle)
{
}

NppDockManager::~NppDockManager()
{
    for (Panel& panel : m_panels) {
        if (panel.dock && panel.client && IsWindow(panel.client)) {
            // Give the dialog back to Notepad++'s window so that the plugin can destroy it.
            SetParent(panel.client, m_nppHandle);
            ShowWindow(panel.client, SW_HIDE);
        }
    }
}

NppDockManager::Panel* NppDockManager::find(HWND client)
{
    for (Panel& p : m_panels) {
        if (p.client == client)
            return &p;
    }
    return nullptr;
}

const NppDockManager::Panel* NppDockManager::find(HWND client) const
{
    for (const Panel& p : m_panels) {
        if (p.client == client)
            return &p;
    }
    return nullptr;
}

bool NppDockManager::isRegistered(HWND client) const
{ return find(client) != nullptr; }

void NppDockManager::sendDockNotification(HWND client, UINT code, int container)
{
    NMHDR nmhdr;
    nmhdr.hwndFrom = m_nppHandle; // DockingDlgInterface checks hwndFrom against Notepad++'s handle
    nmhdr.idFrom = 0;
    nmhdr.code = MAKELONG(code, container);
    SendMessageW(client, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&nmhdr));
}

void NppDockManager::refreshTitle(Panel& panel)
{
    const auto* data = static_cast<const tTbData*>(panel.data);
    QString title = data && data->pszName ? QString::fromWCharArray(data->pszName) : panel.name;
    if (data && (data->uMask & DWS_ADDINFO) && data->pszAddInfo && *data->pszAddInfo)
        title += QStringLiteral(" - ") + QString::fromWCharArray(data->pszAddInfo);
    if (panel.dock) {
        panel.dock->setWindowTitle(title);
        if (data && (data->uMask & DWS_ICONTAB) && data->hIconTab) {
            const QImage image = QImage::fromHICON(data->hIconTab);
            if (!image.isNull())
                panel.dock->setWindowIcon(QIcon(QPixmap::fromImage(image)));
        }
    }
}

bool NppDockManager::registerDialog(const void* rawData)
{
    const auto* data = static_cast<const tTbData*>(rawData);
    if (!data || !data->hClient || !IsWindow(data->hClient))
        return false;
    if (Panel* existing = find(data->hClient)) {
        existing->data = data;
        refreshTitle(*existing);
        return true;
    }

    Panel panel;
    panel.client = data->hClient;
    panel.data = data;
    panel.name = data->pszName ? QString::fromWCharArray(data->pszName) : QStringLiteral("Plugin");
    panel.module = data->pszModuleName ? QString::fromWCharArray(data->pszModuleName) : QString();
    panel.dlgId = data->dlgID;

    RECT rc = {};
    GetWindowRect(data->hClient, &rc);

    auto* dock = new QDockWidget(panel.name, m_window);
    dock->setObjectName(QStringLiteral("nppPlugin:%1:%2").arg(panel.module).arg(panel.dlgId));
    dock->setAttribute(Qt::WA_DeleteOnClose, false);

    QWindow* foreign = QWindow::fromWinId(reinterpret_cast<WId>(data->hClient));
    QWidget* container = QWidget::createWindowContainer(foreign, dock);
    container->setMinimumSize(80, 60);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w > 0 && h > 0) {
        const qreal dpr = m_window->devicePixelRatioF();
        container->resize(qRound(w / dpr), qRound(h / dpr));
    }
    dock->setWidget(container);
    panel.dock = dock;

    const HWND client = data->hClient;
    connect(dock, &QDockWidget::visibilityChanged, this, [this, client](bool visible) {
        Panel* p = find(client);
        if (!p || !p->dock)
            return;
        if (p->programmaticVisibilityChange)
            return;
        // A dock that is merely tabbed behind another one is not closed.
        if (!visible && !p->dock->isHidden())
            return;
        if (!visible)
            sendDockNotification(client, DMN_CLOSE);
    });
    connect(dock, &QDockWidget::topLevelChanged, this, [this, client](bool floating) {
        Panel* p = find(client);
        if (!p || !p->dock)
            return;
        if (floating)
            sendDockNotification(client, DMN_FLOAT);
        else
            sendDockNotification(client, DMN_DOCK, containerFromArea(m_window->dockWidgetArea(p->dock)));
    });

    m_panels.push_back(panel);
    Panel& stored = m_panels.back();
    refreshTitle(stored);

    stored.programmaticVisibilityChange = true;
    m_window->addDockWidget(areaFromMask(data->uMask), dock);
    if (data->uMask & DWS_DF_FLOATING) {
        dock->setFloating(true);
        if (data->rcFloat.right > data->rcFloat.left) {
            dock->setGeometry(data->rcFloat.left,
                data->rcFloat.top,
                data->rcFloat.right - data->rcFloat.left,
                data->rcFloat.bottom - data->rcFloat.top);
        }
    }
    dock->show();
    ShowWindow(client, SW_SHOW);
    stored.programmaticVisibilityChange = false;
    return true;
}

bool NppDockManager::show(HWND client)
{
    Panel* p = find(client);
    if (!p || !p->dock)
        return false;
    p->programmaticVisibilityChange = true;
    p->dock->show();
    p->dock->raise();
    ShowWindow(client, SW_SHOW);
    p->programmaticVisibilityChange = false;
    return true;
}

bool NppDockManager::hide(HWND client)
{
    Panel* p = find(client);
    if (!p || !p->dock)
        return false;
    p->programmaticVisibilityChange = true;
    p->dock->hide();
    p->programmaticVisibilityChange = false;
    return true;
}

bool NppDockManager::updateDisplayInfo(HWND client)
{
    Panel* p = find(client);
    if (!p)
        return false;
    refreshTitle(*p);
    return true;
}

HWND NppDockManager::findClient(const QString& windowName, const QString& moduleName) const
{
    for (const Panel& p : m_panels) {
        const bool nameMatches = windowName.isEmpty() || p.name.compare(windowName, Qt::CaseInsensitive) == 0;
        const bool moduleMatches = moduleName.isEmpty() || p.module.compare(moduleName, Qt::CaseInsensitive) == 0;
        if (nameMatches && moduleMatches)
            return p.client;
    }
    return nullptr;
}

bool NppDockManager::viewOtherTab(const QString& windowName)
{
    HWND client = findClient(windowName, QString());
    return client ? show(client) : false;
}
