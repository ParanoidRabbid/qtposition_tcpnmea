/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtPositioning module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qgeopositioninfosourcefactory_tcpnmea.h"
#include <QtPositioning/qnmeapositioninfosource.h>
#include <QtSerialPort/qserialport.h>
#include <QtSerialPort/qserialportinfo.h>
#include <QtCore/qloggingcategory.h>
#include <QSet>
#include "qiopipe_p.h"
#include <QSharedPointer>
#include "qnmeasatelliteinfosource_p.h"
#include <QTcpSocket>
#include <QUrl>

Q_LOGGING_CATEGORY(lcSerial, "qt.positioning.tcpnmea")

class IODeviceContainer
{
public:
    IODeviceContainer() {}
    IODeviceContainer(IODeviceContainer const&) = delete;
    void operator=(IODeviceContainer const&)  = delete;

    QSharedPointer<QIOPipe> serial(const QString &address)
    {
        if (m_sockets.contains(address)) {
            m_sockets[address].refs++;
            QIOPipe *endPipe = new QIOPipe(m_sockets[address].proxy);
            m_sockets[address].proxy->addChildPipe(endPipe);
            return QSharedPointer<QIOPipe>(endPipe);
        }
        QTcpSocket *socket = new QTcpSocket();
        QString ip = address.split(":").first();
        quint16 port = address.split(":").last().toInt();
        qInfo() << "tcpnmea:" << "trying to open:"
                << " ip:" << ip << " port:" << port;

        socket->connectToHost(ip, port);
        if(!socket->open(QIODevice::ReadWrite)) {
            qInfo() << "tcpnmea: Failed to open:" << address;
            delete socket;
            return {};
        }
        IODevice device;
        qInfo() <<"tcpnmea: opening url:" << address;

        qInfo() << "tcpnmea: opened successfully";
        device.device = socket;
        device.refs = 1;
        device.proxy = new QIOPipe(socket, QIOPipe::ProxyPipe);
        m_sockets[address] = device;
        QIOPipe *endPipe = new QIOPipe(device.proxy);
        device.proxy->addChildPipe(endPipe);
        return QSharedPointer<QIOPipe>(endPipe);
    }

    void releaseSerial(const QString &portName, QSharedPointer<QIOPipe> &pipe) {
        if (!m_sockets.contains(portName))
            return;

        pipe.clear(); // make sure to release the pipe returned by getSerial, or else, if there are still refs, data will be leaked through it
        IODevice &device = m_sockets[portName];
        if (device.refs > 1) {
            device.refs--;
            return;
        }

        IODevice taken = m_sockets.take(portName);
        taken.device->close();
        taken.device->deleteLater();
    }

private:

    struct IODevice {
        QIODevice *device = nullptr;
        QIOPipe *proxy = nullptr;  // adding client pipes as children of proxy allows to dynamically add clients to one device.
        unsigned int refs = 1;
    };

    QMap<QString, IODevice> m_sockets;
};

Q_GLOBAL_STATIC(IODeviceContainer, deviceContainer)


class NmeaSource : public QNmeaPositionInfoSource
{
public:
    explicit NmeaSource(QObject *parent, const QVariantMap &parameters);
    ~NmeaSource() override;
    bool isValid() const { return !m_port.isNull(); }

private:
    QSharedPointer<QIOPipe> m_port;
    QString m_url;
};

NmeaSource::NmeaSource(QObject *parent, const QVariantMap &parameters)
    : QNmeaPositionInfoSource(RealTimeMode, parent)
{
    QByteArray requestedUrl;
    if (parameters.contains(QStringLiteral("tcpnmea.url")))
        requestedUrl = parameters.value(QStringLiteral("tcpnmea.url")).toString().toLatin1();
    else
        requestedUrl = qgetenv("QT_NMEA_URL");

    if (requestedUrl.isEmpty()) {
        qWarning() << "tcpnmea: No known GNSS device found. Specify the url via QT_NMEA_URL. " << requestedUrl;
        return;
    }

    m_url = QString::fromUtf8(requestedUrl);

    m_port = deviceContainer->serial(m_url);

    if (!m_port)
        return;

    setDevice(m_port.data());
}

NmeaSource::~NmeaSource()
{
    deviceContainer->releaseSerial(m_url, m_port);
}



class NmeaSatelliteSource : public QNmeaSatelliteInfoSource
{
public:
    NmeaSatelliteSource(QObject *parent, const QVariantMap &parameters)
        : QNmeaSatelliteInfoSource(parent)
    {
        QByteArray requestedUrl;
        if (parameters.contains(QStringLiteral("tcpnmea.url")))
            requestedUrl = parameters.value(QStringLiteral("tcpnmea.url")).toString().toLatin1();
        else
            requestedUrl = qgetenv("QT_NMEA_URL");

        if (requestedUrl.isEmpty()) {
            qWarning("tcpnmea: url is empty");
            return;
        }

        if (requestedUrl.isEmpty()) {
            qWarning() << "tcpnmea: No known GNSS device found. Specify the url via QT_NMEA_URL. " << requestedUrl;
            return;
        }

        qWarning() << "tcpnmea: tcpnmea.url is :" << requestedUrl;
        m_port = deviceContainer->serial(requestedUrl);

        if (!m_port)
            return;

        setDevice(m_port.data());
    }

    ~NmeaSatelliteSource()
    {
        deviceContainer->releaseSerial(m_portName, m_port);
    }

    bool isValid() const { return !m_port.isNull(); }

private:
    QSharedPointer<QIOPipe> m_port;
    QString m_portName;
};

QGeoPositionInfoSource *QGeoPositionInfoSourceFactorytcpnmea::positionInfoSource(QObject *parent)
{
    return positionInfoSourceWithParameters(parent, QVariantMap());
}

QGeoSatelliteInfoSource *QGeoPositionInfoSourceFactorytcpnmea::satelliteInfoSource(QObject *parent)
{
    return satelliteInfoSourceWithParameters(parent, QVariantMap());
}

QGeoAreaMonitorSource *QGeoPositionInfoSourceFactorytcpnmea::areaMonitor(QObject *parent)
{
    return areaMonitorWithParameters(parent, QVariantMap());
}

QGeoPositionInfoSource *QGeoPositionInfoSourceFactorytcpnmea::positionInfoSourceWithParameters(QObject *parent, const QVariantMap &parameters)
{
    QScopedPointer<NmeaSource> src(new NmeaSource(parent, parameters));
    return src->isValid() ? src.take() : nullptr;
}

QGeoSatelliteInfoSource *QGeoPositionInfoSourceFactorytcpnmea::satelliteInfoSourceWithParameters(QObject *parent, const QVariantMap &parameters)
{
    QScopedPointer<NmeaSatelliteSource> src(new NmeaSatelliteSource(parent, parameters));
    return src->isValid() ? src.take() : nullptr;
}

QGeoAreaMonitorSource *QGeoPositionInfoSourceFactorytcpnmea::areaMonitorWithParameters(QObject *parent, const QVariantMap &parameters)
{
    Q_UNUSED(parent);
    Q_UNUSED(parameters)
    return nullptr;
}
