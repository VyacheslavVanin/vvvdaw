#pragma once
#include <QDialog>
#include <QVector>

class QLineEdit;
class QListWidget;
struct PluginInfo;

// Modal plugin picker with a fuzzy-search field and an arrow-key navigable
// result list. The focus stays in the search field: Up/Down are forwarded to
// the list, so the whole choice can be made from the keyboard, and Enter
// accepts the highlighted (not necessarily the first) entry.
class PluginPickerDialog : public QDialog {
    Q_OBJECT
public:
    // plugins — snapshot of the available plugins at open time;
    // instrumentsOnly — mirrors the old pickers: when true only instrument
    // plugins are listed, when false only effects.
    explicit PluginPickerDialog(const QVector<PluginInfo>& plugins,
                                bool instrumentsOnly,
                                QWidget* parent = nullptr);

    // Empty strings when nothing was chosen (cancelled / empty result list).
    QString selectedType() const;
    QString selectedPath() const;

protected:
    // Forwards Up/Down from the search line edit to the list so arrow keys
    // move the list selection while the caret stays in the search field.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void populateList(const QString& text);
    void acceptCurrent();

    QVector<PluginInfo> m_plugins;
    bool m_instrumentsOnly = false;
    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_listWidget = nullptr;
};
