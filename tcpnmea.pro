TARGET = qtposition_tcpnmea

QT = core-private positioning-private serialport network

HEADERS += \
    qgeopositioninfosourcefactory_tcpnmea.h qnmeasatelliteinfosource_p.h qiopipe_p.h

SOURCES += \
    qgeopositioninfosourcefactory_tcpnmea.cpp qnmeasatelliteinfosource.cpp qiopipe.cpp

OTHER_FILES += \
    plugin.json

PLUGIN_TYPE = position
PLUGIN_CLASS_NAME = QGeoPositionInfoSourceFactoryTcpNmea
load(qt_plugin)
