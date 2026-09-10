#include "AudioBus.h"
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>

namespace {

// Append the buses `bus` feeds: its main output plus every send target. A
// sidechain send only counts while its target plugin still exists, so a stale
// assignment left behind after a plugin is removed does not keep forming a
// routing edge.
void appendBusTargets(const std::vector<AudioBus>& buses, const AudioBus& bus,
                      int busCount, std::vector<int>& out) {
    int parent = bus.outputBusIndex();
    if (parent >= 0 && parent < busCount)
        out.push_back(parent);
    for (const auto& send : bus.sends()) {
        int dest = send.busIndex;
        if (dest < 0 || dest >= busCount)
            continue;
        if (send.isSidechain() &&
            !buses[static_cast<size_t>(dest)].pluginChain().pluginById(send.pluginId))
            continue;
        out.push_back(dest);
    }
}

} // namespace

bool wouldCreateBusCycle(const std::vector<AudioBus>& buses,
                         int fromIndex, int toIndex) {
    if (toIndex < 0) return false;
    if (toIndex == fromIndex) return true;

    int busCount = static_cast<int>(buses.size());
    std::vector<bool> visited(static_cast<size_t>(busCount), false);

    // Reachability: does `toIndex` reach `fromIndex` following existing
    // main-output and send edges? The candidate edge fromIndex->toIndex then
    // closes a cycle.
    std::vector<int> stack = { toIndex };
    while (!stack.empty()) {
        int cur = stack.back();
        stack.pop_back();
        if (cur == fromIndex) return true;
        if (cur < 0 || cur >= busCount || visited[static_cast<size_t>(cur)])
            continue;
        visited[static_cast<size_t>(cur)] = true;

        const AudioBus& bus = buses[static_cast<size_t>(cur)];
        std::vector<int> targets;
        appendBusTargets(buses, bus, busCount, targets);
        for (int t : targets)
            stack.push_back(t);
    }
    return false;
}

SidechainSendLocation findSidechainSend(const std::vector<AudioBus>& buses,
                                        int targetBus, const QString& pluginId) {
    if (targetBus < 0 || targetBus >= static_cast<int>(buses.size()))
        return {};
    for (int i = 0; i < static_cast<int>(buses.size()); ++i) {
        const auto& sends = buses[static_cast<size_t>(i)].sends();
        for (int s = 0; s < static_cast<int>(sends.size()); ++s) {
            const auto& send = sends[static_cast<size_t>(s)];
            if (send.isSidechain() && send.busIndex == targetBus &&
                send.pluginId == pluginId) {
                return { i, s };
            }
        }
    }
    return {};
}

void removeSidechainSendsForPlugin(std::vector<AudioBus>& buses,
                                   int targetBus, const QString& pluginId) {
    for (auto& bus : buses) {
        auto& sends = bus.sends();
        sends.erase(std::remove_if(sends.begin(), sends.end(),
                                   [&](const AudioBus::Send& send) {
                                       return send.isSidechain() &&
                                              send.busIndex == targetBus &&
                                              send.pluginId == pluginId;
                                   }),
                    sends.end());
    }
}

QJsonObject AudioBus::toJson() const {
    QJsonObject obj;
    obj["name"] = m_name;
    obj["pan"] = m_pan;
    obj["volume"] = m_volume;
    obj["outputBusIndex"] = m_outputBusIndex;
    obj["solo"] = m_solo;
    obj["muted"] = m_muted;
    obj["removable"] = m_removable;
    obj["folderCollapsed"] = m_folderCollapsed;
    if (m_colorSet)
        obj["color"] = m_color.name(QColor::HexRgb);
    if (m_pluginChain.count() > 0)
        obj["plugins"] = m_pluginChain.toJson();
    if (!m_sends.empty()) {
        QJsonArray sendsArr;
        for (const auto& send : m_sends) {
            QJsonObject sObj;
            if (send.kind == Send::Kind::Sidechain) {
                sObj["kind"] = "sidechain";
                if (!send.pluginId.isEmpty())
                    sObj["pluginId"] = send.pluginId;
            }
            sObj["bus"] = send.busIndex;
            sObj["level"] = send.level;
            sObj["pre"] = send.preFader;
            sendsArr.append(sObj);
        }
        obj["sends"] = sendsArr;
    }
    return obj;
}

AudioBus AudioBus::fromJson(const QJsonObject& obj, PluginManager* manager) {
    AudioBus bus;
    bus.setName(obj["name"].toString("Bus"));
    bus.setPan(static_cast<float>(obj["pan"].toDouble(0.0)));
    bus.setVolume(static_cast<float>(obj["volume"].toDouble(1.0)));
    bus.setOutputBusIndex(obj["outputBusIndex"].toInt(0));
    bus.setSolo(obj["solo"].toBool(false));
    bus.setMuted(obj["muted"].toBool(false));
    bus.setRemovable(obj["removable"].toBool(true));
    bus.setFolderCollapsed(obj["folderCollapsed"].toBool(false));
    if (obj.contains("color")) {
        QColor color(obj["color"].toString());
        if (color.isValid())
            bus.setColor(color);
    }
    if (obj.contains("plugins"))
        bus.pluginChain().fromJson(obj["plugins"].toObject(), manager);
    if (obj.contains("sends")) {
        std::vector<Send> sends;
        const QJsonArray sendsArr = obj["sends"].toArray();
        sends.reserve(static_cast<size_t>(sendsArr.size()));
        for (const auto& sVal : sendsArr) {
            const QJsonObject sObj = sVal.toObject();
            Send send;
            if (sObj["kind"].toString() == "sidechain")
                send.kind = Send::Kind::Sidechain;
            send.pluginId = sObj["pluginId"].toString();
            send.busIndex = sObj["bus"].toInt(0);
            send.level = static_cast<float>(sObj["level"].toDouble(1.0));
            send.preFader = sObj["pre"].toBool(false);
            sends.push_back(send);
        }
        bus.setSends(std::move(sends));
    }
    return bus;
}
