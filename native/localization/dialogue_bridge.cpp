#include "localization/dialogue_bridge.h"

namespace bs::native::localization {

void DialogueBridge::on_event(const audio::DialogueEvent& e) {
    using K = audio::DialogueKind;
    const int64_t t = to100ns(e.frame);
    switch (e.kind) {
    case K::Started: subs_.logical_time(t), subs_.dialogue_started(e.id, scene_, t); break;
    case K::PhysicalEnd: subs_.dialogue_ended(e.id); break;
    case K::Interrupted: subs_.dialogue_interrupted(e.id); break;
    case K::Paused: subs_.paused(true, t); break;
    case K::Resumed: subs_.paused(false, t); break;
    case K::LogicalEnd:
    case K::Missing:
    case K::Requested: break;
    }
}

} // namespace bs::native::localization
