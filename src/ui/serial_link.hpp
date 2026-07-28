#pragma once
#include <QObject>
#include <QByteArray>
#include "../core/types.hpp"

class Serial;
class QTcpServer;
class QTcpSocket;

// Link cable over TCP: one instance hosts, the other connects. Byte-level
// exchange: when the local game drives a transfer (internal clock) we send
// our byte and block briefly for the peer's reply; when the peer drives,
// we answer immediately with whatever is in our serial register.
//
// Protocol: two-byte messages, [type, data]
//   'M' = master byte (sender's clock drives the exchange), expects 'S' back
//   'S' = slave reply
class SerialLink : public QObject {
    Q_OBJECT

public:
    explicit SerialLink(Serial& serial, QObject* parent = nullptr);

    bool Host(u16 port);
    bool ConnectTo(const QString& host, u16 port);
    void Disconnect();
    bool IsConnected() const;
    bool IsHosting() const;

signals:
    void StatusChanged(const QString& status);

private slots:
    void OnNewConnection();
    void OnReadyRead();
    void OnDisconnected();

private:
    Serial& m_serial;
    QTcpServer* m_server = nullptr;
    QTcpSocket* m_socket = nullptr;
    QByteArray m_buffer;
    bool m_in_exchange = false;

    // Master-side exchange: send our byte, wait for the peer's reply
    u8 ExchangeAsMaster(u8 byte);

    void AdoptSocket(QTcpSocket* socket);
    void ProcessBuffer();
};
