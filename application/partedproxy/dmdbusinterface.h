/*
 * This file was generated by qdbusxml2cpp version 0.8
 * Command line was: qdbusxml2cpp -c DMDBusInterface -p dmdbusinterface.h:dmdbusinterface.cpp com.deepin.diskmanager.xml
 *
 * qdbusxml2cpp is Copyright (C) 2017 The Qt Company Ltd.
 *
 * This is an auto-generated file.
 * Do not edit! All changes made to it will be lost.
 */

#ifndef DMDBUSINTERFACE_H
#define DMDBUSINTERFACE_H

#include <QtCore/QObject>
#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtDBus/QtDBus>
#include "deviceinfo.h"
/*
 * Proxy class for interface com.deepin.diskmanager
 */
class DMDBusInterface: public QDBusAbstractInterface
{
    Q_OBJECT
public:
    static inline const char *staticInterfaceName()
    { return "com.deepin.diskmanager"; }

public:
    DMDBusInterface(const QString &service, const QString &path, const QDBusConnection &connection, QObject *parent = nullptr);

    ~DMDBusInterface();

public Q_SLOTS: // METHODS
    inline QDBusPendingReply<> Quit()
    {
        QList<QVariant> argumentList;
        return asyncCallWithArgumentList(QStringLiteral("Quit"), argumentList);
    }

    inline QDBusPendingReply<> Start()
    {
        QList<QVariant> argumentList;
        return asyncCallWithArgumentList(QStringLiteral("Start"), argumentList);
    }
    inline QDBusPendingReply<DeviceInfo>getDeviceinfo()
    {
        QList<QVariant> argumentList;
        return asyncCallWithArgumentList(QStringLiteral("getDeviceinfo"), argumentList);
    }
    inline QDBusPendingReply<>getalldevice()
    {
        QList<QVariant> argumentList;
        return asyncCallWithArgumentList(QStringLiteral("getalldevice"), argumentList);
    }


Q_SIGNALS: // SIGNALS
    Q_SCRIPTABLE void MessageReport(const QString &msg);
    Q_SCRIPTABLE void AccessDeviceInfoOver(const DeviceInfo &info);
    Q_SCRIPTABLE void sigUpdateDeviceInfo(const DeviceInfoMap &infomap);
};

namespace com {
namespace deepin {
typedef ::DMDBusInterface diskmanager;
}
}
#endif