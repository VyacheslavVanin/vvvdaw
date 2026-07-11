#include "PluginWindow.h"
#include "plugin/PluginInstance.h"
#include <QCloseEvent>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QLabel>

PluginWindow::PluginWindow(PluginInstance* plugin, QWidget* parent)
    : QWidget(parent)
    , m_plugin(plugin) {
    setWindowTitle(m_plugin ? m_plugin->name() : "Plugin");
    setMinimumSize(300, 200);
    setAttribute(Qt::WA_DeleteOnClose);

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
    if (!m_plugin || !m_plugin->hasEditor()) return;

    m_editorHandle = m_plugin->createEditor(reinterpret_cast<void*>(winId()));
    if (!m_editorHandle) {
        auto* label = findChild<QLabel*>();
        if (label) label->setText("Plugin GUI not available");
    }
    show();
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
