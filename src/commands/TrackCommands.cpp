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

// --- SetAllTracksHeightCommand ---

SetAllTracksHeightCommand::SetAllTracksHeightCommand(Project& project,
                                                     std::vector<int> oldHeights,
                                                     int newHeight)
    : m_project(project), m_oldHeights(std::move(oldHeights)), m_newHeight(newHeight) {}

void SetAllTracksHeightCommand::execute() {
    for (auto& track : m_project.tracks())
        track.setHeight(m_newHeight);
}

void SetAllTracksHeightCommand::undo() {
    const size_t n = std::min(m_oldHeights.size(), m_project.tracks().size());
    for (size_t i = 0; i < n; ++i)
        m_project.tracks()[i].setHeight(m_oldHeights[i]);
}

// --- ReorderTracksCommand ---

ReorderTracksCommand::ReorderTracksCommand(Project& project, std::vector<int> newOrder)
    : m_project(project), m_newOrder(std::move(newOrder)) {
    // `newOrder` maps a new position to the original index; `oldOrder` is its
    // inverse, mapping a position back to the track that was there before.
    m_oldOrder.resize(m_newOrder.size());
    for (int i = 0; i < static_cast<int>(m_newOrder.size()); ++i) {
        int idx = m_newOrder[static_cast<size_t>(i)];
        if (idx >= 0 && idx < static_cast<int>(m_oldOrder.size()))
            m_oldOrder[static_cast<size_t>(idx)] = i;
    }
}

void ReorderTracksCommand::applyOrder(const std::vector<int>& order) {
    std::vector<Track> reordered;
    reordered.reserve(order.size());
    for (int idx : order) {
        if (idx < 0 || idx >= static_cast<int>(m_project.tracks().size()))
            continue;
        reordered.push_back(std::move(m_project.tracks()[static_cast<size_t>(idx)]));
    }
    m_project.tracks() = std::move(reordered);
}

void ReorderTracksCommand::execute() { applyOrder(m_newOrder); }
void ReorderTracksCommand::undo() { applyOrder(m_oldOrder); }
