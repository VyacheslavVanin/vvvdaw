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
                                                     std::vector<int> newHeights)
    : m_project(project), m_oldHeights(std::move(oldHeights))
    , m_newHeights(std::move(newHeights)) {}

void SetAllTracksHeightCommand::execute() {
    const size_t n = std::min(m_newHeights.size(), m_project.tracks().size());
    for (size_t i = 0; i < n; ++i)
        m_project.tracks()[i].setHeight(m_newHeights[i]);
}

void SetAllTracksHeightCommand::undo() {
    const size_t n = std::min(m_oldHeights.size(), m_project.tracks().size());
    for (size_t i = 0; i < n; ++i)
        m_project.tracks()[i].setHeight(m_oldHeights[i]);
}

// --- SetTracksHeightCommand ---

SetTracksHeightCommand::SetTracksHeightCommand(Project& project,
                                               std::vector<int> indices,
                                               std::vector<int> oldHeights,
                                               std::vector<int> newHeights)
    : m_project(project), m_indices(std::move(indices))
    , m_oldHeights(std::move(oldHeights)), m_newHeights(std::move(newHeights)) {}

void SetTracksHeightCommand::execute() {
    const size_t n = std::min({m_indices.size(), m_newHeights.size(),
                               m_project.tracks().size()});
    for (size_t i = 0; i < n; ++i) {
        const int idx = m_indices[i];
        if (idx < 0 || idx >= static_cast<int>(m_project.tracks().size())) continue;
        m_project.tracks()[idx].setHeight(m_newHeights[i]);
    }
}

void SetTracksHeightCommand::undo() {
    const size_t n = std::min({m_indices.size(), m_oldHeights.size(),
                               m_project.tracks().size()});
    for (size_t i = 0; i < n; ++i) {
        const int idx = m_indices[i];
        if (idx < 0 || idx >= static_cast<int>(m_project.tracks().size())) continue;
        m_project.tracks()[idx].setHeight(m_oldHeights[i]);
    }
}

// --- SetTracksColorCommand ---

SetTracksColorCommand::SetTracksColorCommand(Project& project,
                                             std::vector<int> indices,
                                             std::vector<QColor> oldColors,
                                             std::vector<bool> oldSets,
                                             QColor newColor, bool newSet)
    : m_project(project), m_indices(std::move(indices))
    , m_oldColors(std::move(oldColors)), m_oldSets(std::move(oldSets))
    , m_newColor(newColor), m_newSet(newSet) {}

void SetTracksColorCommand::execute() {
    const size_t n = std::min({m_indices.size(), m_oldColors.size(), m_oldSets.size(),
                               m_project.tracks().size()});
    for (size_t i = 0; i < n; ++i) {
        Track* track = m_project.trackAt(m_indices[i]);
        if (!track) continue;
        if (m_newSet) track->setColor(m_newColor);
        else track->clearColor();
    }
}

void SetTracksColorCommand::undo() {
    const size_t n = std::min({m_indices.size(), m_oldColors.size(), m_oldSets.size(),
                               m_project.tracks().size()});
    for (size_t i = 0; i < n; ++i) {
        Track* track = m_project.trackAt(m_indices[i]);
        if (!track) continue;
        if (m_oldSets[i]) track->setColor(m_oldColors[i]);
        else track->clearColor();
    }
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
