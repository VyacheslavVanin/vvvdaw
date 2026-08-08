#include "TrackCommands.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
#include "model/MidiEvent.h"
#include "plugin/PluginManager.h"
#include "core/Constants.h"

// --- AddTrackCommand ---

AddTrackCommand::AddTrackCommand(Project& project, int index, int channels)
    : m_project(project), m_index(index), m_channels(channels), m_type(Track::Type::Audio) {}

AddTrackCommand::AddTrackCommand(Project& project, int index, Track::Type type)
    : m_project(project), m_index(index), m_channels(2), m_type(type) {}

void AddTrackCommand::execute() {
    if (m_type == Track::Type::Midi)
        m_project.addMidiTrack(QString());
    else
        m_project.addTrack(QString(), m_channels);
}

void AddTrackCommand::undo() {
    m_project.removeTrack(m_index);
}

// --- RemoveTrackCommand ---

RemoveTrackCommand::RemoveTrackCommand(Project& project, int index, PluginManager* manager)
    : m_project(project), m_index(index), m_manager(manager) {
    if (const Track* track = m_project.trackAt(index))
        m_savedTrack = track->toJson();
}

void RemoveTrackCommand::execute() {
    m_project.removeTrack(m_index);
}

void RemoveTrackCommand::undo() {
    Track track;
    track.fromJson(m_savedTrack, {}, m_manager);
    if (m_index >= 0 && m_index <= static_cast<int>(m_project.tracks().size())) {
        m_project.tracks().insert(m_project.tracks().begin() + m_index, std::move(track));
    } else {
        m_project.addTrack(track.name());
    }
}

// --- SetTrackMidiOutputCommand ---

SetTrackMidiOutputCommand::SetTrackMidiOutputCommand(Project& project, int trackIndex,
                                                     Routing oldRouting, Routing newRouting)
    : m_project(project), m_trackIndex(trackIndex),
      m_oldRouting(oldRouting), m_newRouting(newRouting) {}

void SetTrackMidiOutputCommand::apply(const Routing& routing) {
    Track* track = m_project.trackAt(m_trackIndex);
    if (!track)
        return;
    track->setMidiOutputDeviceId(routing.deviceId);
    track->setMidiOutputDeviceName(routing.deviceName);
    track->setInstrumentIndex(routing.instrumentIndex);
}

void SetTrackMidiOutputCommand::execute() { apply(m_newRouting); }
void SetTrackMidiOutputCommand::undo() { apply(m_oldRouting); }
