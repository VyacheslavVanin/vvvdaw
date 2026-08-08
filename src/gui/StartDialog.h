#pragma once
#include <QDialog>
#include <QString>

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
    void browseForProject();
    void choose(Action action);

    Settings& m_settings;
    Choice m_choice;
    QListWidget* m_recentList = nullptr;
    QListWidget* m_templateList = nullptr;
    QPushButton* m_openRecentButton = nullptr;
    QPushButton* m_useTemplateButton = nullptr;
};
