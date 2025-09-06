#include "PreviewWidget.h"
#include <QPainter>
#include <QResizeEvent>
#include <QMouseEvent>

PreviewWidget::PreviewWidget(GameCapture* capture, QWidget* parent)
    : QWidget(parent)
    , m_capture(capture)
    , m_previewActive(false)
    , m_display(nullptr)
{
    setMinimumSize(320, 180);
    setStyleSheet("background-color: #1a1a1a; border: 1px solid #333;");
}

PreviewWidget::~PreviewWidget()
{
    stopPreview();
}

void PreviewWidget::startPreview()
{
    if (m_previewActive || !m_capture) {
        return;
    }
    
    // For now, just set the flag - actual OBS display integration can be added later
    m_previewActive = true;
    update();
}

void PreviewWidget::stopPreview()
{
    if (!m_previewActive) {
        return;
    }
    
    if (m_display) {
        m_display = nullptr;
    }
    
    m_previewActive = false;
    update();
}

void PreviewWidget::renderPreview(void* data, uint32_t cx, uint32_t cy)
{
    // This will be implemented later when OBS display integration is working
}

void PreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    
    // Future: resize OBS display here
}

void PreviewWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(26, 26, 26));
    
    painter.setPen(QColor(150, 150, 150));
    if (m_previewActive) {
        painter.drawText(rect(), Qt::AlignCenter, "Preview Active\n(OBS Display Integration Coming Soon)");
    } else {
        painter.drawText(rect(), Qt::AlignCenter, "Preview Stopped\nClick 'Start Preview' to begin");
    }
    
    QWidget::paintEvent(event);
}

QPaintEngine* PreviewWidget::paintEngine() const
{
    return QWidget::paintEngine(); // Use default Qt painting for now
}