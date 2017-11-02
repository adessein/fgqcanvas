//
// Copyright (C) 2017 James Turner  zakalawe@mac.com
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include "applicationcontroller.h"

#include <QNetworkDiskCache>
#include <QStandardPaths>
#include <QNetworkRequest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include "canvasconnection.h"

ApplicationController::ApplicationController(QObject *parent)
    : QObject(parent)
    , m_status(Idle)
{
    m_netAccess = new QNetworkAccessManager;
    m_host = "localhost";
    m_port = 8080;

    QNetworkDiskCache* cache = new QNetworkDiskCache;
    cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    m_netAccess->setCache(cache); // takes ownership

    setStatus(Idle);
    rebuildConfigData();
}

ApplicationController::~ApplicationController()
{
    delete m_netAccess;
}

void ApplicationController::save(QString configName)
{
    QDir d(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    if (!d.exists()) {
        qWarning() << "creating" << d;
        d.mkpath(".");
    }

    // convert spaces to underscores
    QString filesystemCleanName = configName.replace(QRegularExpression("[\\s-\\\"/]"), "_");
    qDebug() << Q_FUNC_INFO << "FS clean name is " << filesystemCleanName;

    QFile f(d.filePath(configName + ".json"));
    if (f.exists()) {
        qWarning() << "not over-writing" << f.fileName();
        return;
    }

    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(saveState(configName));

    QVariantMap m;
    m["path"] = f.fileName();
    m["name"] = configName;
    m_configs.append(m);
    emit configListChanged(m_configs);
}

void ApplicationController::rebuildConfigData()
{
    m_configs.clear();
    QDir d(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    if (!d.exists()) {
        emit configListChanged(m_configs);
        return;
    }

    // this requires parsing each config in its entirety just to extract
    // the name, which is horrible.
    Q_FOREACH (auto entry, d.entryList(QStringList() << "*.json")) {
        QString path = d.filePath(entry);
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());

        QVariantMap m;
        m["path"] = path;
        m["name"] = doc.object().value("configName").toString();
        m_configs.append(m);
    }

    emit configListChanged(m_configs);
}

void ApplicationController::query()
{
    qWarning() << Q_FUNC_INFO << m_host << m_port;

    if (m_host.isEmpty() || (m_port == 0))
        return;

    QUrl queryUrl;
    queryUrl.setScheme("http");
    queryUrl.setHost(m_host);
    queryUrl.setPort(m_port);
    queryUrl.setPath("/json/canvas/by-index");
    queryUrl.setQuery("d=2");

    QNetworkReply* reply = m_netAccess->get(QNetworkRequest(queryUrl));
    connect(reply, &QNetworkReply::finished,
            this, &ApplicationController::onFinishedGetCanvasList);

    setStatus(Querying);
}

void ApplicationController::restoreConfig(int index)
{
    QString path = m_configs.at(index).toMap().value("path").toString();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << Q_FUNC_INFO << "failed to open the file";
        return;
    }

    restoreState(f.readAll());
}

void ApplicationController::openCanvas(QString path)
{
    CanvasConnection* cc = new CanvasConnection(this);

    cc->setNetworkAccess(m_netAccess);
    m_activeCanvases.append(cc);

    cc->setRootPropertyPath(path.toUtf8());
    cc->connectWebSocket(m_host.toUtf8(), m_port);

    emit activeCanvasesChanged();
}

QString ApplicationController::host() const
{
    return m_host;
}

unsigned int ApplicationController::port() const
{
    return m_port;
}

QVariantList ApplicationController::canvases() const
{
    return m_canvases;
}

QQmlListProperty<CanvasConnection> ApplicationController::activeCanvases()
{
    return QQmlListProperty<CanvasConnection>(this, m_activeCanvases);
}

QNetworkAccessManager *ApplicationController::netAccess() const
{
    return m_netAccess;
}

void ApplicationController::setHost(QString host)
{
    if (m_host == host)
        return;

    m_host = host;
    emit hostChanged(m_host);
    setStatus(Idle);
}

void ApplicationController::setPort(unsigned int port)
{
    if (m_port == port)
        return;

    m_port = port;
    emit portChanged(m_port);
    setStatus(Idle);
}

QJsonObject jsonPropNodeFindChild(QJsonObject obj, QByteArray name)
{
    Q_FOREACH (QJsonValue v, obj.value("children").toArray()) {
        QJsonObject vo = v.toObject();
        if (vo.value("name").toString() == name) {
            return vo;
        }
    }

    return QJsonObject();
}

void ApplicationController::onFinishedGetCanvasList()
{
    m_canvases.clear();
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        setStatus(QueryFailed);
        emit canvasListChanged();
        return;
    }

    QJsonDocument json = QJsonDocument::fromJson(reply->readAll());

    QJsonArray canvasArray = json.object().value("children").toArray();
    Q_FOREACH (QJsonValue canvasValue, canvasArray) {
        QJsonObject canvas = canvasValue.toObject();
        QString canvasName = jsonPropNodeFindChild(canvas, "name").value("value").toString();
        QString propPath = canvas.value("path").toString();

        QVariantMap info;
        info["name"] = canvasName;
        info["path"] = propPath;
        m_canvases.append(info);
    }

    emit canvasListChanged();
    setStatus(SuccessfulQuery);
}

void ApplicationController::setStatus(ApplicationController::Status newStatus)
{
    if (newStatus == m_status)
        return;

    m_status = newStatus;
    emit statusChanged(m_status);
}

QByteArray ApplicationController::saveState(QString name) const
{
    QJsonObject json;
    json["configName"] = name;

    QJsonArray canvases;
    Q_FOREACH (auto canvas, m_activeCanvases) {
        canvases.append(canvas->saveState());
    }

    json["canvases"] = canvases;
    // background color?
    // window geometry and state?

    QJsonDocument doc;
    doc.setObject(json);
    return doc.toJson();
}

void ApplicationController::restoreState(QByteArray bytes)
{
    qDeleteAll(m_activeCanvases);
    m_activeCanvases.clear();

    QJsonDocument jsonDoc = QJsonDocument::fromJson(bytes);
    QJsonObject json = jsonDoc.object();

    // window size
    // background color

    for (auto c : json.value("canvases").toArray()) {
        auto cc = new CanvasConnection(this);
        cc->setNetworkAccess(m_netAccess);
        m_activeCanvases.append(cc);
        cc->restoreState(c.toObject());
        cc->reconnect();
    }

    emit activeCanvasesChanged();
}
