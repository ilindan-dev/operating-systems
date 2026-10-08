#include "participant.hpp"

#include <utility>

namespace chat::host {

Participant::Participant(proto::ParticipantId participantId, pid_t processId,
                         transport::Conn channel)
    : id(participantId), pid(processId), conn(std::move(channel))
{}

bool Participant::send(const proto::Message& message)
{
    const auto bytes = proto::encode(message);
    const std::lock_guard lock(writeMutex);
    return conn.write(bytes);
}

} // namespace chat::host
