#include "StartDialog.h"
#include "core/Settings.h"
#include "model/TemplateStore.h"
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

StartDialog::StartDialog(Settings& settings, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
{
    TemplateStore::ensureBuiltInTemplates();
    setupUi();
    populateRecentProjects();
    populateTemplates();

    setWindowTitle("vvvdaw");
    resize(720, 420);
}

void StartDialog::setupUi() {
    auto* layout = new QVBoxLayout(this);

    auto* title = new QLabel("<h2>Welcome to vvvdaw</h2>", this);
    layout->addWidget(title);

    auto* columns = new QHBoxLayout;

    auto* recentColumn = new QVBoxLayout;
    recentColumn->addWidget(new QLabel("<b>Recent Projects</b>", this));
    m_recentList = new QListWidget(this);
    m_recentList->setSelectionMode(QAbstractItemView::SingleSelection);
    recentColumn->addWidget(m_recentList, 1);

    auto* recentButtons = new QHBoxLayout;
    m_openRecentButton = new QPushButton("Open", this);
    auto* browseButton = new QPushButton("Browse...", this);
    recentButtons->addWidget(m_openRecentButton);
    recentButtons->addWidget(browseButton);
    recentColumn->addLayout(recentButtons);
    columns->addLayout(recentColumn, 1);

    auto* templateColumn = new QVBoxLayout;
    templateColumn->addWidget(new QLabel("<b>Templates</b>", this));
    m_templateList = new QListWidget(this);
    m_templateList->setSelectionMode(QAbstractItemView::SingleSelection);
    templateColumn->addWidget(m_templateList, 1);

    auto* templateButtons = new QHBoxLayout;
    m_useTemplateButton = new QPushButton("Use Template", this);
    templateButtons->addWidget(m_useTemplateButton);
    templateButtons->addStretch();
    templateColumn->addLayout(templateButtons);
    columns->addLayout(templateColumn, 1);

    layout->addLayout(columns);

    auto* bottomRow = new QHBoxLayout;
    bottomRow->addStretch();
    auto* exitButton = new QPushButton("Exit", this);
    bottomRow->addWidget(exitButton);
    layout->addLayout(bottomRow);

    connect(m_recentList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { openSelectedRecent(); });
    connect(m_openRecentButton, &QPushButton::clicked, this,
            &StartDialog::openSelectedRecent);
    connect(browseButton, &QPushButton::clicked, this,
            &StartDialog::browseForProject);
    connect(m_templateList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { openSelectedTemplate(); });
    connect(m_useTemplateButton, &QPushButton::clicked, this,
            &StartDialog::openSelectedTemplate);
    connect(exitButton, &QPushButton::clicked, this,
            [this] { choose(Action::Exit); });
}

void StartDialog::populateRecentProjects() {
    m_recentList->clear();
    for (const auto& recent : m_settings.recentProjects()) {
        if (!QFile::exists(recent.path))
            continue;
        auto* item = new QListWidgetItem(QFileInfo(recent.path).dir().dirName(),
                                         m_recentList);
        item->setToolTip(recent.path);
        item->setData(Qt::UserRole, recent.path);
    }
}

void StartDialog::populateTemplates() {
    m_templateList->clear();
    for (const QString& name : TemplateStore::listTemplates()) {
        QString label = name;
        if (TemplateStore::isBuiltIn(name))
            label += " (built-in)";
        auto* item = new QListWidgetItem(label, m_templateList);
        QString description = TemplateStore::templateDescription(name);
        if (!description.isEmpty())
            item->setToolTip(description);
        item->setData(Qt::UserRole, name);
    }
}

void StartDialog::openSelectedRecent() {
    auto* item = m_recentList->currentItem();
    if (!item)
        return;
    m_choice.path = item->data(Qt::UserRole).toString();
    choose(Action::OpenRecent);
}

void StartDialog::openSelectedTemplate() {
    auto* item = m_templateList->currentItem();
    if (!item)
        return;
    m_choice.templateName = item->data(Qt::UserRole).toString();
    choose(Action::OpenTemplate);
}

void StartDialog::browseForProject() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open Project", QString(), "Project Files (project.json)");
    if (path.isEmpty())
        return;
    m_choice.path = path;
    choose(Action::Browse);
}

void StartDialog::choose(Action action) {
    m_choice.action = action;
    accept();
}
