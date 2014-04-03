// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2013-2079 Dr. Kimoto Chan
// Copyright (c) 2014 The Eurocoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QApplication>

#include "paymentserver.h"

#include "guiconstants.h"
#include "ui_interface.h"
#include "util.h"

#include <QByteArray>
#include <QDataStream>
#include <QDebug>
#include <QFileOpenEvent>
#include <QHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStringList>
#if QT_VERSION < 0x050000
#include <QUrl>
#endif

using namespace boost;

const int EUROCOIN_IPC_CONNECT_TIMEOUT = 1000; // milliseconds
const QString EUROCOIN_IPC_PREFIX("eurocoin:");

//
// Create a name that is unique for:
//  testnet / non-testnet
//  data directory
//
static QString ipcServerName()
{
    QString name("EurocoinQt");

    // Append a simple hash of the datadir
    // Note that GetDataDir(true) returns a different path
    // for -testnet versus main net
    QString ddir(GetDataDir(true).string().c_str());
    name.append(QString::number(qHash(ddir)));

    return name;
}

//
// This stores payment requests received before
// the main GUI window is up and ready to ask the user
// to send payment.
//
static QStringList savedPaymentRequests;

//
// Sending to the server is done synchronously, at startup.
// If the server isn't already running, startup continues,
// and the items in savedPaymentRequest will be handled
// when uiReady() is called.
//
bool PaymentServer::ipcSendCommandLine()
{
    bool fResult = false;

    const QStringList& args = qApp->arguments();
    for (int i = 1; i < args.size(); i++)
    {
        if (!args[i].startsWith(EUROCOIN_IPC_PREFIX, Qt::CaseInsensitive))
            continue;
        savedPaymentRequests.append(args[i]);
    }

    foreach (const QString& arg, savedPaymentRequests)
    {
        QLocalSocket* socket = new QLocalSocket();
        socket->connectToServer(ipcServerName(), QIODevice::WriteOnly);
        if (!socket->waitForConnected(EUROCOIN_IPC_CONNECT_TIMEOUT))
            return false;

        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);
        out << arg;
        out.device()->seek(0);
        socket->write(block);
        socket->flush();

        socket->waitForBytesWritten(EUROCOIN_IPC_CONNECT_TIMEOUT);
        socket->disconnectFromServer();
        delete socket;
        fResult = true;
    }
    return fResult;
}

PaymentServer::PaymentServer(QApplication* parent) : QObject(parent), saveURIs(true)
{
    // Install global event filter to catch QFileOpenEvents on the mac (sent when you click eurocoin: links)
    parent->installEventFilter(this);

    QString name = ipcServerName();

    // Clean up old socket leftover from a crash:
    QLocalServer::removeServer(name);

    uriServer = new QLocalServer(this);

    if (!uriServer->listen(name))
        qDebug() << tr("Cannot start eurocoin: click-to-pay handler");
    else
        connect(uriServer, SIGNAL(newConnection()), this, SLOT(handleURIConnection()));
}

bool PaymentServer::eventFilter(QObject *object, QEvent *event)
{
    // clicking on eurocoin: URLs creates FileOpen events on the Mac:
    if (event->type() == QEvent::FileOpen)
    {
        QFileOpenEvent* fileEvent = static_cast<QFileOpenEvent*>(event);
        if (!fileEvent->url().isEmpty())
        {
            if (saveURIs) // Before main window is ready:
                savedPaymentRequests.append(fileEvent->url().toString());
            else
                emit receivedURI(fileEvent->url().toString());
            return true;
        }
    }
    return false;
}

void PaymentServer::uiReady()
{
    saveURIs = false;
    foreach (const QString& s, savedPaymentRequests)
        emit receivedURI(s);
    savedPaymentRequests.clear();
}

void PaymentServer::handleURIConnection()
{
    QLocalSocket *clientConnection = uriServer->nextPendingConnection();

    while (clientConnection->bytesAvailable() < (int)sizeof(quint32))
        clientConnection->waitForReadyRead();

    connect(clientConnection, SIGNAL(disconnected()),
            clientConnection, SLOT(deleteLater()));

    QDataStream in(clientConnection);
    in.setVersion(QDataStream::Qt_4_0);
    if (clientConnection->bytesAvailable() < (int)sizeof(quint16)) {
        return;
    }
    QString message;
    in >> message;

    if (saveURIs)
        savedPaymentRequests.append(message);
    else
        emit receivedURI(message);
}