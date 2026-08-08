#pragma once
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <cstdint>

// Read an int64 field from JSON (QJsonValue has no native int64 overload).
inline int64_t jsonInt64(const QJsonObject& obj, const char* key) {
    return static_cast<int64_t>(obj[key].toVariant().toLongLong());
}

inline int64_t jsonInt64(const QJsonObject& obj, const char* key, int64_t fallback) {
    if (!obj.contains(key))
        return fallback;
    return jsonInt64(obj, key);
}

// Store a file path relative to the project directory so projects are
// portable; returns the path unchanged when there is no project directory.
inline QString relativeToProject(const QString& filePath, const QString& projectDir) {
    if (projectDir.isEmpty())
        return filePath;
    QString absPath = QFileInfo(filePath).absoluteFilePath();
    QString absProj = QFileInfo(projectDir).absoluteFilePath();
    if (absPath.startsWith(absProj + "/"))
        return absPath.mid(absProj.length() + 1);
    return absPath;
}

// Resolve a possibly-relative file path against the project directory.
inline QString resolveProjectPath(const QString& path, const QString& projectDir) {
    if (path.isEmpty() || projectDir.isEmpty())
        return path;
    return QDir::isAbsolutePath(path) ? path : QDir(projectDir).absoluteFilePath(path);
}
