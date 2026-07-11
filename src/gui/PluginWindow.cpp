#include "PluginWindow.h"
#include "PluginParameterWidget.h"
#include "plugin/PluginInstance.h"
#include <QCloseEvent>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>

PluginWindow::PluginWindow(PluginInstance* plugin, QWidget* parent)
    : QWidget(parent)
    , m_plugin(plugin) {
    setWindowTitle(m_plugin ? m_plugin->name() : "Plugin");
    setMinimumSize(100, 100);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() | Qt::Window);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* label = new QLabel(m_plugin ? m_plugin->name() : "", this);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("color: #aaa; font-size: 12px; padding: 20px;");
    layout->addWidget(label);
}

PluginWindow::~PluginWindow() {
    if (m_plugin) m_plugin->destroyEditor();
}

void PluginWindow::open() {
    if (!m_plugin || !m_plugin->hasEditor()) {
        deleteLater();
        return;
    }

    auto* label = findChild<QLabel*>();
    if (label) label->hide();
    show();

    QTimer::singleShot(0, this, [this]() {
        m_editorHandle = m_plugin->createEditor(static_cast<void*>(this));
        if (m_editorHandle) {
            int w = 0, h = 0;
            if (m_plugin->getEditorSize(w, h))
                resize(w, h);
            return;
        }

        auto* label = findChild<QLabel*>();
        if (label) label->hide();

        auto* paramWidget = new PluginParameterWidget(m_plugin, this);
        layout()->addWidget(paramWidget);
        adjustSize();
    });
}

void PluginWindow::close() {
    if (m_plugin) m_plugin->destroyEditor();
    m_editorHandle = nullptr;
    QWidget::close();
}

void PluginWindow::closeEvent(QCloseEvent* event) {
    if (m_plugin) m_plugin->destroyEditor();
    m_editorHandle = nullptr;
    emit windowClosed();
    event->accept();
}

void PluginWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_plugin && m_editorHandle)
        m_plugin->resizeEditor(event->size().width(), event->size().height());
}
