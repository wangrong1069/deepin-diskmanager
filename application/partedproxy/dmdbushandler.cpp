#include "dmdbushandler.h"
#include <QObject>
#include <QDBusError>
#include <QDBusPendingCallWatcher>

DMDbusHandler *DMDbusHandler::m_statichandeler = nullptr;

DMDbusHandler *DMDbusHandler::instance(QObject *parent)
{
    if (parent != nullptr && m_statichandeler == nullptr)
        m_statichandeler = new DMDbusHandler(parent);
    return m_statichandeler;
}

DMDbusHandler::DMDbusHandler(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<DeviceInfo>("DeviceInfo");
    qRegisterMetaType<DeviceInfoMap>("DeviceInfoMap");

    qDBusRegisterMetaType<DeviceInfo>();
    qDBusRegisterMetaType<DeviceInfoMap>();
    qDBusRegisterMetaType<PartitionInfo>();
    qDBusRegisterMetaType<stCustest>();
    m_dbus = new DMDBusInterface("com.deepin.diskmanager",
                                 "/com/deepin/diskmanager",
                                 QDBusConnection::systemBus(),
                                 this);
//Note: when dealing with remote objects, it is not always possible to determine if it exists when creating a QDBusInterface.
    if (!m_dbus->isValid() && !m_dbus->lastError().message().isEmpty()) {
        qDebug() << "m_dbus isValid false error:" << m_dbus->lastError() << m_dbus->lastError().message();
    }
    qDebug() << "m_dbus isValid true";
    initConnection();
    m_dbus->Start();
}

void DMDbusHandler::initConnection()
{
    connect(m_dbus, &DMDBusInterface::MessageReport, this, &DMDbusHandler::MessageReport);
    connect(m_dbus, &DMDBusInterface::sigUpdateDeviceInfo, this, &DMDbusHandler::sigUpdateDeviceInfo);
}

void DMDbusHandler::Quit()
{
    m_dbus->Quit();
}

void DMDbusHandler::getDeviceinfo()
{
    // emit sigShowSpinerWindow(true);
    m_dbus->getalldevice();
    qDebug() << __FUNCTION__ << "-------";
}

void DMDbusHandler::MessageReport(const QString &msg)
{
    qDebug() << "MessageReport:" << msg;
}

