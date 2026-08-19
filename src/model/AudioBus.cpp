#include "AudioBus.h"
#include <QJsonArray>
#include <QJsonObject>

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
        int parent = bus.outputBusIndex();
        if (parent >= 0 && parent < busCount)
            stack.push_back(parent);
        for (const auto& send : bus.sends())
            if (send.busIndex >= 0 && send.busIndex < busCount)
                stack.push_back(send.busIndex);
    }
    return false;
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
            send.busIndex = sObj["bus"].toInt(0);
            send.level = static_cast<float>(sObj["level"].toDouble(1.0));
            send.preFader = sObj["pre"].toBool(false);
            sends.push_back(send);
        }
        bus.setSends(std::move(sends));
    }
    return bus;
}
