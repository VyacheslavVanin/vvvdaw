#pragma once
#include <QDialog>
#include <QString>
#include <functional>

class Settings;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// Startup dialog shown before the main window. Offers the most recently
// opened projects, project templates (built-in and user-saved), a browse
// action and an explicit exit button.
class StartDialog : public QDialog {
    Q_OBJECT
public:
    friend class MainWindowTest;
    enum class Action { None, OpenRecent, OpenTemplate, Browse, Exit };

    struct Choice {
        Action action = Action::None;
        QString path;         // OpenRecent / Browse
        QString templateName; // OpenTemplate
    };

    explicit StartDialog(Settings& settings, QWidget* parent = nullptr);

    Choice choice() const { return m_choice; }

private:
    void setupUi();
    void populateRecentProjects();
    void populateTemplates();
    void openSelectedRecent();
    void openSelectedTemplate();
    void deleteSelectedRecent();
    void deleteSelectedTemplate();
    void updateTemplateDeleteEnabled();
    void browseForProject();
    void choose(Action action);

    // Returns true when deletion of `name` is confirmed. Tests inject a stub
    // via setConfirmCallback; the default shows a Yes/No question dialog.
    bool confirmDelete(const QString& name);

    Settings& m_settings;
    Choice m_choice;
    QListWidget* m_recentList = nullptr;
    QListWidget* m_templateList = nullptr;
    QPushButton* m_openRecentButton = nullptr;
    QPushButton* m_deleteRecentButton = nullptr;
    QPushButton* m_useTemplateButton = nullptr;
    QPushButton* m_deleteTemplateButton = nullptr;
    std::function<bool(const QString&)> m_confirm;
};
