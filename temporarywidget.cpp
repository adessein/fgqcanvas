#include "temporarywidget.h"
#include "ui_temporarywidget.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>

// ws://localhost:8080/PropertyTreeMirror/canvas/by-index/texture[3]

#include "canvastreemodel.h"
#include "localprop.h"

TemporaryWidget::TemporaryWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TemporaryWidget)
{
    ui->setupUi(this);
    connect(ui->connectButton, &QPushButton::clicked, this, &TemporaryWidget::onStartConnect);
    restoreSettings();

}

TemporaryWidget::~TemporaryWidget()
{
    delete ui;
}

void TemporaryWidget::onStartConnect()
{
    QString wsUrl = ui->socketURL->text();
    if (!wsUrl.startsWith("ws")) {
        qWarning() << "Not a web-socket URL" << wsUrl;
        return;
    }

// string clean up, to ensure our root path has a leading slash but
// no trailing slash
    QString propPath = ui->propertyPath->text();
    if (wsUrl.endsWith('/')) {
        wsUrl.truncate(wsUrl.size() - 1);
    }

    QString rootPath = propPath;
    if (!propPath.startsWith('/')) {
        rootPath = '/' + propPath;
    }

    if (propPath.endsWith('/')) {
        rootPath.chop(1);
    }

    QUrl url = QUrl(wsUrl + rootPath);
    rootPropertyPath = rootPath.toUtf8();

    connect(&m_webSocket, &QWebSocket::connected, this, &TemporaryWidget::onConnected);
    connect(&m_webSocket, &QWebSocket::disconnected, this, &TemporaryWidget::onSocketClosed);

    saveSettings();

    qDebug() << "starting connection to:" << url;
    m_webSocket.open(url);
}

void TemporaryWidget::onConnected()
{
    qDebug() << "connected";
    connect(&m_webSocket, &QWebSocket::textMessageReceived,
            this, &TemporaryWidget::onTextMessageReceived);
    m_webSocket.sendTextMessage(QStringLiteral("Hello, world!"));

    m_localPropertyRoot = new LocalProp(nullptr, NameIndexTuple(""));

    ui->canvas->setRootProperty(m_localPropertyRoot);
    ui->stack->setCurrentIndex(1);

    m_canvasModel = new CanvasTreeModel(ui->canvas->rootElement());
    ui->treeView->setModel(m_canvasModel);
}

void TemporaryWidget::onTextMessageReceived(QString message)
{
    QJsonDocument json = QJsonDocument::fromJson(message.toUtf8());
    if (json.isObject()) {
        // process new nodes
        QJsonArray created = json.object().value("created").toArray();
        Q_FOREACH (QJsonValue v, created) {
            QJsonObject newProp = v.toObject();

            QByteArray nodePath = newProp.value("path").toString().toUtf8();
            if (nodePath.indexOf(rootPropertyPath) != 0) {
                qWarning() << "not a property path we are mirroring:" << nodePath;
                continue;
            }

            QByteArray localPath = nodePath.mid(rootPropertyPath.size() + 1);
            LocalProp* newNode = propertyFromPath(localPath);

            // store in the global dict
            unsigned int propId = newProp.value("id").toInt();
            if (idPropertyDict.contains(propId)) {
                qWarning() << "duplicate add of:" << nodePath << "old is" << idPropertyDict.value(propId)->path();
            } else {
                idPropertyDict.insert(propId, newNode);
            }

            // set initial value
            newNode->processChange(newProp.value("value"));
        }

        // process removes
        QJsonArray removed = json.object().value("remvoed").toArray();
        Q_FOREACH (QJsonValue v, removed) {
            unsigned int propId = v.toInt();
            if (idPropertyDict.contains(propId)) {
                LocalProp* prop = idPropertyDict.value(propId);
                idPropertyDict.remove(propId);
                prop->parent()->removeChild(prop);
            }
        }

        // process changes
        QJsonArray changed = json.object().value("changed").toArray();

        Q_FOREACH (QJsonValue v, changed) {
            QJsonArray change = v.toArray();
            if (change.size() != 2) {
                qWarning() << "malformed change notification";
                continue;
            }

            unsigned int propId = change.at(0).toInt();
            if (!idPropertyDict.contains(propId)) {
                qWarning() << "ignoring unknown prop ID " << propId;
                continue;
            }

            LocalProp* lp = idPropertyDict.value(propId);
            lp->processChange(change.at(1));
        }
    }

    ui->canvas->update();
}

void TemporaryWidget::onSocketClosed()
{
    qDebug() << "saw web-socket closed";
    delete m_localPropertyRoot;
    m_localPropertyRoot = nullptr;
    idPropertyDict.clear();

    ui->stack->setCurrentIndex(0);
}

void TemporaryWidget::saveSettings()
{
    QSettings settings;
    settings.setValue("ws-host", ui->socketURL->text());
    settings.setValue("prop-path", ui->propertyPath->text());
}

void TemporaryWidget::restoreSettings()
{
    QSettings settings;
    ui->socketURL->setText(settings.value("ws-host").toString());
    ui->propertyPath->setText(settings.value("prop-path").toString());
}

LocalProp *TemporaryWidget::propertyFromPath(QByteArray path) const
{
    return m_localPropertyRoot->getOrCreateWithPath(path);
}
