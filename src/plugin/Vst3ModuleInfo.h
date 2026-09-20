#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <cctype>

// Parsing of a VST3 bundle's moduleinfo.json. Kept separate from
// PluginManager.cpp so the (quirky) format can be exercised directly.
//
// The VST3/Steam writer emits trailing commas before '}' / ']', which Qt's JSON
// parser rejects, so the text is normalised first.
namespace Vst3ModuleInfo {

struct Meta {
    QString name;
    QString vendor;
    bool isInstrument = false;
};

// True when the comma at `commaIndex` is followed only by whitespace and then
// a closing '}' or ']'.
inline bool isTrailingCommaAt(const QByteArray& in, int commaIndex) {
    for (int j = commaIndex + 1; j < in.size(); ++j) {
        const char c = in.at(j);
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        return c == '}' || c == ']';
    }
    return false;
}

// Removes commas that sit just before a closing '}' / ']'. Commas inside string
// literals are left alone (quote/escape aware).
inline QByteArray stripTrailingCommas(const QByteArray& in) {
    QByteArray out;
    out.reserve(in.size());
    bool inString = false;
    bool escaped = false;
    for (int i = 0; i < in.size(); ++i) {
        const char c = in.at(i);
        if (inString) {
            out.append(c);
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"') {
            inString = true;
            out.append(c);
            continue;
        }
        if (c == ',') {
            if (isTrailingCommaAt(in, i)) continue; // drop the stray comma
        }
        out.append(c);
    }
    return out;
}

// Parses a moduleinfo.json document. Returns false when the payload is not a
// JSON object or declares no class with `audioEffectClass` as its category
// (normally Steinberg::Vst::kVstAudioEffectClass). On success `meta` carries
// the bundle name, factory vendor and whether the component declares itself an
// instrument via its subcategories.
inline bool parse(const QByteArray& json, const char* audioEffectClass, Meta& meta) {
    QJsonDocument doc = QJsonDocument::fromJson(stripTrailingCommas(json));
    if (!doc.isObject()) return false;

    const QJsonObject root = doc.object();
    meta.name = root["Name"].toString();
    meta.vendor = root["Factory Info"].toObject()["Vendor"].toString();

    const QString category = QString::fromUtf8(audioEffectClass);
    for (auto v : root["Classes"].toArray()) {
        const QJsonObject c = v.toObject();
        if (c["Category"].toString() != category) continue;
        for (auto s : c["Sub Categories"].toArray())
            if (s.toString() == "Instrument") meta.isInstrument = true;
        return true;
    }
    return false;
}

} // namespace Vst3ModuleInfo
