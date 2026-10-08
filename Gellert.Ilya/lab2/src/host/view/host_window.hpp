#pragma once

#include "host/model/hub.hpp"
#include "ui/chat_window.hpp"

namespace chat::host {

/**
 * The host's window: connects the Hub (the chat logic) with the shared ChatWindow.
 *
 * The Hub reports events from its own threads; they are forwarded to the GUI thread with
 * queued calls, because Qt widgets may only be touched from the thread that runs the GUI.
 */
class HostWindow : private HubListener {
public:
    HostWindow();
    ~HostWindow() override;

    HostWindow(const HostWindow&) = delete;
    HostWindow& operator=(const HostWindow&) = delete;

    void show();

private:
    void onJoined(proto::ParticipantId id, const std::string& name, pid_t pid) override;
    void onLeft(proto::ParticipantId id, const std::string& name,
                const std::string& reason) override;
    void onMessage(const proto::Message& message) override;

    template <typename F> void inGuiThread(F&& action);

    ui::ChatWindow window_;
    Hub hub_; ///< declared after the window: stopped (and silent) before the window goes away
};

} // namespace chat::host
