#include "AudioVisualizer.h"
#include <QPainter>
#include <QStyleOption>
#include <algorithm>
#include <cmath>

AudioVisualizer::AudioVisualizer(QWidget *parent)
    : QWidget(parent),
      m_updateTimer(new QTimer(this)),
      m_currentLevel(0.0f),
      m_peakLevel(0.0f),
      m_displayLevel(0.0f),
      m_enabled(true),
      m_barLevels(m_barCount, 0.0f),
      m_barPeaks(m_barCount, 0.0f)
{
    setFixedHeight(30);
    setMinimumWidth(200);
    
    // Update at 60 FPS for smooth animation
    m_updateTimer->setInterval(16);
    connect(m_updateTimer, &QTimer::timeout, this, &AudioVisualizer::updateVisualizer);
    m_updateTimer->start();
    
    // Set background color to match OBS theme
    setAutoFillBackground(true);
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, QColor(42, 42, 43));
    setPalette(palette);
}

AudioVisualizer::~AudioVisualizer()
{
}

void AudioVisualizer::updateAudioLevel(float level)
{
    QMutexLocker locker(&m_mutex);
    
    // Clamp level to valid range
    level = std::max(0.0f, std::min(1.0f, level));
    
    m_currentLevel = level;
    
    // Update peak level
    if (level > m_peakLevel) {
        m_peakLevel = level;
    }
    
    // Simulate frequency bands by adding some variation
    for (int i = 0; i < m_barCount; ++i) {
        float bandLevel = level;
        
        // Add some frequency-based variation
        if (level > 0.01f) {
            float variation = 0.3f + 0.7f * std::sin(i * 0.5f + level * 10.0f);
            bandLevel *= variation;
        }
        
        // Update bar level
        if (bandLevel > m_barLevels[i]) {
            m_barLevels[i] = bandLevel;
        }
        
        // Update bar peak
        if (bandLevel > m_barPeaks[i]) {
            m_barPeaks[i] = bandLevel;
        }
    }
}

void AudioVisualizer::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (m_updateTimer) {
        if (enabled && !m_updateTimer->isActive()) m_updateTimer->start();
        if (!enabled && m_updateTimer->isActive()) m_updateTimer->stop();
    }
    if (!enabled) {
        QMutexLocker locker(&m_mutex);
        m_currentLevel = 0.0f;
        m_peakLevel = 0.0f;
        m_displayLevel = 0.0f;
        std::fill(m_barLevels.begin(), m_barLevels.end(), 0.0f);
        std::fill(m_barPeaks.begin(), m_barPeaks.end(), 0.0f);
    }
    update();
}

void AudioVisualizer::updateVisualizer()
{
    QMutexLocker locker(&m_mutex);
    
    // Smooth level transitions
    float targetLevel = m_enabled ? m_currentLevel : 0.0f;
    m_displayLevel = m_displayLevel * 0.8f + targetLevel * 0.2f;
    
    // Decay peak level
    m_peakLevel *= (1.0f - m_peakDecay);
    
    // Decay bar levels and peaks
    for (int i = 0; i < m_barCount; ++i) {
        m_barLevels[i] *= (1.0f - m_levelDecay);
        m_barPeaks[i] *= (1.0f - m_peakDecay);
    }
    
    update();
}

void AudioVisualizer::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    
    QRect rect = this->rect();
    
    // Draw background
    painter.fillRect(rect, QColor(42, 42, 43));
    
    // Draw border
    painter.setPen(QColor(70, 70, 71));
    painter.drawRect(rect.adjusted(0, 0, -1, -1));
    
    if (!m_enabled) {
        // Draw disabled state
        painter.setPen(QColor(128, 128, 128));
        painter.drawText(rect, Qt::AlignCenter, "Audio Disabled");
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // Calculate bar dimensions
    int margin = 2;
    int availableWidth = rect.width() - 2 * margin;
    int barWidth = std::max(1, (availableWidth - (m_barCount - 1)) / m_barCount);
    int spacing = 1;
    
    // Draw bars
    for (int i = 0; i < m_barCount; ++i) {
        int x = margin + i * (barWidth + spacing);
        int barHeight = rect.height() - 2 * margin;
        
        // Calculate bar fill height based on level
        int fillHeight = static_cast<int>(m_barLevels[i] * barHeight);
        int peakHeight = static_cast<int>(m_barPeaks[i] * barHeight);
        
        // Choose color based on level (green -> yellow -> red like OBS)
        QColor barColor;
        float level = m_barLevels[i];
        if (level < 0.7f) {
            // Green to yellow
            int green = 255;
            int red = static_cast<int>(255 * (level / 0.7f));
            barColor = QColor(red, green, 0);
        } else {
            // Yellow to red
            int red = 255;
            int green = static_cast<int>(255 * (1.0f - ((level - 0.7f) / 0.3f)));
            barColor = QColor(red, green, 0);
        }
        
        // Draw background bar (dark)
        QRect barRect(x, margin + barHeight - barHeight, barWidth, barHeight);
        painter.fillRect(barRect, QColor(60, 60, 61));
        
        // Draw active bar portion
        if (fillHeight > 0) {
            QRect fillRect(x, margin + barHeight - fillHeight, barWidth, fillHeight);
            painter.fillRect(fillRect, barColor);
        }
        
        // Draw peak indicator
        if (peakHeight > 0 && peakHeight != fillHeight) {
            QRect peakRect(x, margin + barHeight - peakHeight, barWidth, 2);
            painter.fillRect(peakRect, barColor.lighter(150));
        }
    }
    
    // Draw level text
    painter.setPen(QColor(255, 255, 255));
    QString levelText = QString("Level: %1%").arg(static_cast<int>(m_displayLevel * 100));
    painter.drawText(rect.adjusted(5, 0, -5, 0), Qt::AlignRight | Qt::AlignBottom, levelText);
}

QSize AudioVisualizer::sizeHint() const
{
    return QSize(200, 30);
}