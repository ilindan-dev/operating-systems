#include "host_window.hpp"

#include <unistd.h>

#include <QMetaObject>

#include "transport/conn.hpp"

namespace chat::host {

namespace {

QString qs(const std::string& text)
{
    return QString::fromStdString(text);
}

QString transportName()
{
    return QString::fromLatin1(transport::Conn::typeName());
}

ui::ChatWindowConfig windowConfig()
{
    const auto pid = static_cast<qint64>(getpid());
    ui::ChatWindowConfig config;
    config.windowTitle = QStringLiteral("LocalChat — хост (%1)").arg(transportName());
    config.selfName = QStringLiteral("Хост");
    config.selfId = proto::kHostId;
    config.transport = transportName();
    config.info = QStringLiteral("PID %1\nКлиент: ./client_%2 %1").arg(pid).arg(transportName());
    config.hint = QStringLiteral("Правый клик по сообщению — переслать его в общий чат или лично.\n"
                                 "Кто молчит больше минуты, отключается (SIGKILL).");
    config.canForward = true;
    return config;
}

} // namespace

HostWindow::HostWindow() : window_(windowConfig()), hub_(*this)
{
    QObject::connect(
        &window_, &ui::ChatWindow::sendRequested, &window_,
        [this](quint32 to, const QString& text) { hub_.sendFromHost(to, text.toStdString()); });
    QObject::connect(&window_, &ui::ChatWindow::forwardRequested, &window_,
                     [this](quint32 author, const QString& text, quint32 to) {
                         hub_.forward(author, text.toStdString(), to);
                     });

    hub_.start();
    window_.setConnected(
        true, QStringLiteral("Чат запущен · канал %1 · ждём участников").arg(transportName()));
    window_.addStatus(QStringLiteral("Чат запущен. Запустите клиента: ./client_%1 %2")
                          .arg(transportName())
                          .arg(static_cast<qint64>(getpid())));
}

HostWindow::~HostWindow()
{
    hub_.stop();
}

void HostWindow::show()
{
    window_.show();
}

template <typename F> void HostWindow::inGuiThread(F&& action)
{
    // The window is the context: if it is already destroyed, the call is simply dropped.
    QMetaObject::invokeMethod(&window_, std::forward<F>(action), Qt::QueuedConnection);
}

void HostWindow::onJoined(proto::ParticipantId id, const std::string& name, pid_t pid)
{
    inGuiThread([this, id, name = qs(name), pid] {
        window_.addParticipant(id, name, QStringLiteral("id %1 · PID %2").arg(id).arg(pid));
        window_.addStatus(QStringLiteral("%1 теперь в чате").arg(name), id);
        window_.setConnected(true, QStringLiteral("Чат запущен · канал %1 · участников: %2")
                                       .arg(transportName())
                                       .arg(hub_.participants().size()));
    });
}

void HostWindow::onLeft(proto::ParticipantId id, const std::string& name, const std::string& reason)
{
    inGuiThread([this, id, name = qs(name), reason = qs(reason)] {
        window_.addStatus(QStringLiteral("%1 %2").arg(name, reason), id);
        window_.removeParticipant(id);
        window_.setConnected(true, QStringLiteral("Чат запущен · канал %1 · участников: %2")
                                       .arg(transportName())
                                       .arg(hub_.participants().size()));
    });
}

void HostWindow::onMessage(const proto::Message& message)
{
    inGuiThread([this, message] {
        window_.addChatMessage(message.from, message.to, message.sentAtMs, qs(message.text),
                               message.forwardedFrom);
    });
}

} // namespace chat::host
