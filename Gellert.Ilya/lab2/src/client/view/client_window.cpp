#include "client_window.hpp"

#include <unistd.h>

#include <QMetaObject>

#include "transport/conn.hpp"

namespace chat::client {

namespace {

QString qs(const std::string& text)
{
    return QString::fromStdString(text);
}

QString transportName()
{
    return QString::fromLatin1(transport::Conn::typeName());
}

ui::ChatWindowConfig windowConfig(const QString& name)
{
    ui::ChatWindowConfig config;
    config.windowTitle = QStringLiteral("LocalChat — %1 (%2)").arg(name, transportName());
    config.selfName = name;
    config.selfId = proto::kNobody; // not known before the handshake
    config.transport = transportName();
    config.info = QStringLiteral("Подключение…");
    config.hint = QStringLiteral("Если молчать больше минуты, хост отключит вас.");
    return config;
}

} // namespace

ClientWindow::ClientWindow(pid_t hostPid, const QString& name)
    : window_(windowConfig(name)), session_(*this, hostPid, name.toStdString())
{
    QObject::connect(&window_, &ui::ChatWindow::sendRequested, &window_,
                     [this](quint32 to, const QString& text) {
                         if (!session_.send(to, text.toStdString())) {
                             window_.addStatus(
                                 QStringLiteral("Сообщение не отправлено: нет связи с хостом"));
                         }
                     });
}

ClientWindow::~ClientWindow()
{
    session_.stop();
}

std::optional<QString> ClientWindow::connect()
{
    try {
        session_.connect();
    } catch (const std::exception& e) {
        return QString::fromStdString(e.what());
    }
    const auto pid = static_cast<qint64>(getpid());
    window_.setSelf(session_.id(), qs(session_.name()),
                    QStringLiteral("id %1 · PID %2\nХост: PID %3")
                        .arg(session_.id())
                        .arg(pid)
                        .arg(session_.hostPid()));
    window_.setConnected(true, QStringLiteral("Подключено к хосту %1 · канал %2")
                                   .arg(session_.hostPid())
                                   .arg(transportName()));
    window_.addStatus(QStringLiteral("Вы подключились к чату как «%1»").arg(qs(session_.name())));
    return std::nullopt;
}

void ClientWindow::show()
{
    window_.show();
}

template <typename F> void ClientWindow::inGuiThread(F&& action)
{
    QMetaObject::invokeMethod(&window_, std::forward<F>(action), Qt::QueuedConnection);
}

void ClientWindow::onJoined(proto::ParticipantId id, const std::string& name)
{
    inGuiThread([this, id, name = qs(name)] {
        window_.addParticipant(id, name,
                               id == proto::kHostId ? QStringLiteral("хост чата")
                                                    : QStringLiteral("id %1").arg(id));
        window_.addStatus(id == proto::kHostId ? QStringLiteral("%1 — хост этого чата").arg(name)
                                               : QStringLiteral("%1 в чате").arg(name),
                          id);
    });
}

void ClientWindow::onLeft(proto::ParticipantId id, const std::string& reason)
{
    inGuiThread([this, id, reason = qs(reason)] {
        window_.addStatus(QStringLiteral("%1 %2").arg(window_.nameOf(id), reason), id);
        window_.removeParticipant(id);
    });
}

void ClientWindow::onMessage(const proto::Message& message)
{
    inGuiThread([this, message] {
        window_.addChatMessage(message.from, message.to, message.sentAtMs, qs(message.text),
                               message.forwardedFrom);
    });
}

void ClientWindow::onDisconnected(const std::string& reason)
{
    inGuiThread([this, reason = qs(reason)] {
        window_.addStatus(QStringLiteral("Соединение закрыто: %1").arg(reason));
        window_.setConnected(false, QStringLiteral("Нет связи с хостом"));
    });
}

} // namespace chat::client
