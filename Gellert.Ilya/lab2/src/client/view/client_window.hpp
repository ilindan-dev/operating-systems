#pragma once

#include <sys/types.h>

#include <QString>
#include <optional>
#include <string>

#include "client/model/client_session.hpp"
#include "ui/chat_window.hpp"

namespace chat::client {

/// The client's window: connects ClientSession with the shared ChatWindow.
class ClientWindow : private ClientListener {
public:
    ClientWindow(pid_t hostPid, const QString& name);
    ~ClientWindow() override;

    ClientWindow(const ClientWindow&) = delete;
    ClientWindow& operator=(const ClientWindow&) = delete;

    /// Handshake and channel; returns an error message for the user on failure.
    [[nodiscard]] std::optional<QString> connect();
    void show();

private:
    void onJoined(proto::ParticipantId id, const std::string& name) override;
    void onLeft(proto::ParticipantId id, const std::string& reason) override;
    void onMessage(const proto::Message& message) override;
    void onDisconnected(const std::string& reason) override;

    template <typename F> void inGuiThread(F&& action);

    ui::ChatWindow window_;
    ClientSession session_; ///< declared after the window: stopped before the window goes away
};

} // namespace chat::client
