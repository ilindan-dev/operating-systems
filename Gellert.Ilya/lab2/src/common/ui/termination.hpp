#pragma once

class QCoreApplication;

namespace chat::ui {

/**
 * Makes SIGINT, SIGTERM and SIGHUP end the Qt event loop normally, so that destructors run:
 * the host closes the channels and removes its FIFOs, sockets and semaphores.
 *
 * The signals are read through signalfd, watched by the event loop like any descriptor.
 * They must already be blocked in all threads (SignalBlocker at the start of main).
 */
void quitOnTerminationSignals(QCoreApplication& app);

} // namespace chat::ui
