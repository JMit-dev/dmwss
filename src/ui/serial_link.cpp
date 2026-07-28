#include "serial_link.hpp"
#include "../core/serial/serial.hpp"
#include <QTcpServer>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <spdlog/spdlog.h>

// How long the master waits for the slave's reply before treating the
// line as disconnected. Generous because the peer only services the
// socket between emulated frames.
static constexpr int EXCHANGE_TIMEOUT_MS = 500;

static constexpr char MSG_MASTER = 'M';
static constexpr char MSG_SLAVE = 'S';

SerialLink::SerialLink(Serial& serial, QObject* parent)
    : QObject(parent)
    , m_serial(serial) {
    m_serial.SetLinkCallback([this](u8 byte) { return ExchangeAsMaster(byte); });
}

bool SerialLink::Host(u16 port) {
    Disconnect();

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &SerialLink::OnNewConnection);
    if (!m_server->listen(QHostAddress::Any, port)) {
        emit StatusChanged("Link: failed to listen on port " + QString::number(port));
        delete m_server;
        m_server = nullptr;
        return false;
    }

    emit StatusChanged("Link: waiting for peer on port " + QString::number(port));
    return true;
}

bool SerialLink::ConnectTo(const QString& host, u16 port) {
    Disconnect();

    QTcpSocket* socket = new QTcpSocket(this);
    socket->connectToHost(host, port);
    if (!socket->waitForConnected(3000)) {
        emit StatusChanged("Link: connection to " + host + " failed");
        socket->deleteLater();
        return false;
    }

    AdoptSocket(socket);
    emit StatusChanged("Link: connected to " + host);
    return true;
}

void SerialLink::Disconnect() {
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    m_buffer.clear();
}

bool SerialLink::IsConnected() const {
    return m_socket != nullptr;
}

bool SerialLink::IsHosting() const {
    return m_server != nullptr;
}

void SerialLink::OnNewConnection() {
    QTcpSocket* socket = m_server->nextPendingConnection();
    if (!socket) return;

    if (m_socket) {
        // Only one peer per cable
        socket->close();
        socket->deleteLater();
        return;
    }

    AdoptSocket(socket);
    emit StatusChanged("Link: peer connected");
}

void SerialLink::AdoptSocket(QTcpSocket* socket) {
    m_socket = socket;
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(m_socket, &QTcpSocket::readyRead, this, &SerialLink::OnReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &SerialLink::OnDisconnected);
    m_buffer.clear();
}

void SerialLink::OnDisconnected() {
    if (m_socket) {
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_buffer.clear();
    emit StatusChanged("Link: peer disconnected");
}

u8 SerialLink::ExchangeAsMaster(u8 byte) {
    if (!m_socket) {
        return 0xFF;  // No cable: the line reads high
    }

    const char msg[2] = {MSG_MASTER, static_cast<char>(byte)};
    m_socket->write(msg, 2);
    m_socket->flush();

    // Wait for the slave reply, answering any crossing master bytes so
    // two simultaneous masters cannot deadlock
    m_in_exchange = true;
    QElapsedTimer timer;
    timer.start();
    u8 received = 0xFF;
    while (m_socket && timer.elapsed() < EXCHANGE_TIMEOUT_MS) {
        if (m_socket->bytesAvailable() < 2 && !m_socket->waitForReadyRead(10)) {
            continue;
        }
        m_buffer += m_socket->readAll();
        bool got_reply = false;
        while (m_buffer.size() >= 2) {
            char type = m_buffer[0];
            u8 data = static_cast<u8>(m_buffer[1]);
            m_buffer.remove(0, 2);
            if (type == MSG_SLAVE) {
                received = data;
                got_reply = true;
                break;
            } else if (type == MSG_MASTER) {
                const char reply[2] = {MSG_SLAVE,
                    static_cast<char>(m_serial.ExchangeAsSlave(data))};
                m_socket->write(reply, 2);
                m_socket->flush();
            }
        }
        if (got_reply) break;
    }
    m_in_exchange = false;

    return received;
}

void SerialLink::OnReadyRead() {
    // The blocking master exchange drains the socket itself
    if (m_in_exchange) return;
    if (!m_socket) return;

    m_buffer += m_socket->readAll();
    ProcessBuffer();
}

void SerialLink::ProcessBuffer() {
    while (m_buffer.size() >= 2) {
        char type = m_buffer[0];
        u8 data = static_cast<u8>(m_buffer[1]);
        m_buffer.remove(0, 2);
        if (type == MSG_MASTER) {
            const char reply[2] = {MSG_SLAVE,
                static_cast<char>(m_serial.ExchangeAsSlave(data))};
            m_socket->write(reply, 2);
            m_socket->flush();
        }
        // Stray slave replies (after a timed-out exchange) are dropped
    }
}
