#include "PluginPickerDialog.h"
#include "plugin/PluginManager.h"

#include <QHBoxLayout>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {

// Vector-ish fuzzy score of target against query; -1 when query does not
// match as a subsequence of target.
int fuzzyScore(const QString& query, const QString& target) {
    if (query.isEmpty()) return 0;
    int qi = 0, score = 0, run = 0;
    for (int ti = 0; ti < target.size(); ++ti) {
        if (target[ti].toLower() == query[qi].toLower()) {
            score += (run > 0) ? 8 + run : 12;
            if (ti == 0) score += 6;
            else {
                QChar prev = target[ti - 1];
                if (!prev.isLetterOrNumber() || prev.isUpper() != target[ti].isUpper())
                    score += 4;
            }
            score += 20 - ti / 2;
            if (++qi == query.size()) return score;
            ++run;
        } else {
            run = 0;
        }
    }
    return -1;
}

QString pluginLabel(const PluginInfo& pi) {
    return QString("[%1] %2").arg(pi.type.toUpper(), pi.name);
}

} // namespace

PluginPickerDialog::PluginPickerDialog(const QVector<PluginInfo>& plugins,
                                       bool instrumentsOnly,
                                       QWidget* parent)
    : QDialog(parent), m_plugins(plugins), m_instrumentsOnly(instrumentsOnly) {
    setWindowTitle(instrumentsOnly ? "Select Instrument" : "Add Plugin");
    setMinimumSize(400, 300);

    auto* layout = new QVBoxLayout(this);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(instrumentsOnly
                                         ? "Search instruments..."
                                         : "Search plugins...");
    m_searchEdit->setFocus();
    layout->addWidget(m_searchEdit);

    m_listWidget = new QListWidget(this);
    layout->addWidget(m_listWidget);

    auto* buttons = new QHBoxLayout();
    auto* okBtn = new QPushButton(instrumentsOnly ? "Select" : "Add", this);
    auto* cancelBtn = new QPushButton("Cancel", this);
    buttons->addWidget(okBtn);
    buttons->addWidget(cancelBtn);
    layout->addLayout(buttons);

    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_listWidget, &QListWidget::itemDoubleClicked,
            this, &QDialog::accept);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, [this](const QString& text) { populateList(text); });

    // Enter accepts the current selection; Up/Down reach the list through the
    // event filter installed below.
    m_searchEdit->installEventFilter(this);

    populateList(QString());
}

bool PluginPickerDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched != m_searchEdit || event->type() != QEvent::KeyPress) {
        return QDialog::eventFilter(watched, event);
    }
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    switch (keyEvent->key()) {
    case Qt::Key_Up:
    case Qt::Key_Down:
        QApplication::sendEvent(m_listWidget, event);
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        acceptCurrent();
        return true;
    default:
        return QDialog::eventFilter(watched, event);
    }
}

void PluginPickerDialog::populateList(const QString& text) {
    QVector<QPair<int, const PluginInfo*>> scored;
    for (const auto& pi : m_plugins) {
        if (pi.isInstrument != m_instrumentsOnly) continue;
        const QString display = pluginLabel(pi);
        const int score = fuzzyScore(text, display);
        if (score >= 0)
            scored.append({score, &pi});
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const QPair<int, const PluginInfo*>& a,
                        const QPair<int, const PluginInfo*>& b) {
                         return a.first > b.first;
                     });
    m_listWidget->clear();
    for (const auto& entry : scored) {
        const PluginInfo* pi = entry.second;
        auto* item = new QListWidgetItem(pluginLabel(*pi));
        item->setData(Qt::UserRole, pi->type);
        item->setData(Qt::UserRole + 1, pi->path);
        m_listWidget->addItem(item);
    }
    if (m_listWidget->count() > 0)
        m_listWidget->setCurrentItem(m_listWidget->item(0));
}

void PluginPickerDialog::acceptCurrent() {
    if (m_listWidget->count() == 0) return;
    accept();
}

QString PluginPickerDialog::selectedType() const {
    const auto* item = m_listWidget->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

QString PluginPickerDialog::selectedPath() const {
    const auto* item = m_listWidget->currentItem();
    return item ? item->data(Qt::UserRole + 1).toString() : QString();
}
