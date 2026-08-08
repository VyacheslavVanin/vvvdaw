#pragma once
#include <QString>
#include <QStringList>

class Project;

// Project templates: built-in blueprints ("empty", "rock-band") plus
// user-saved templates, stored as folders next to the app settings under
// AppConfigLocation/templates/<name>/project.json.
class TemplateStore {
public:
    static QString templatesDir();
    static QString templateFilePath(const QString& name);
    static bool exists(const QString& name);
    static bool isBuiltIn(const QString& name);

    // All available template names: built-ins plus user folders, sorted.
    static QStringList listTemplates();

    // Write the built-in templates to disk if they are missing.
    static void ensureBuiltInTemplates();

    // Build a fresh project from a built-in template.
    static Project createBuiltIn(const QString& name);

    // Load a template into `project`; the project is left unsaved
    // (empty file path, name "Untitled") so a normal save prompts Save As.
    static bool loadTemplate(Project& project, const QString& name);

    // Save a copy of `project` as a template. The project's own file path is
    // left untouched. Refuses to overwrite unless `overwrite` is true.
    static bool saveTemplate(Project& project, const QString& name,
                             bool overwrite = false);

    // Make `raw` safe to use as a template name/folder; empty when invalid.
    static QString sanitizeName(const QString& raw);

    static QString templateDescription(const QString& name);

    // Test seam: redirect the templates directory (otherwise QStandardPaths).
    static void setTemplatesDirOverride(const QString& dir);

private:
    static bool writeBuiltIn(const QString& name);
    static QString s_templatesDirOverride;
};
