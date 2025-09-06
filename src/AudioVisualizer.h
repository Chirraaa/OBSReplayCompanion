#pragma once

#include <QWidget>
#include <QPainter>
#include <QTimer>
#include <QMutex>
#include <vector>

class AudioVisualizer : public QWidget
{
    Q_OBJECT

public:
    explicit AudioVisualizer(QWidget *parent = nullptr);
    ~AudioVisualizer();

    void updateAudioLevel(float level); // level should be 0.0 to 1.0
    void setEnabled(bool enabled);
    
protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;
    
private slots:
    void updateVisualizer();

private:
    QTimer *m_updateTimer;
    QMutex m_mutex;
    float m_currentLevel;
    float m_peakLevel;
    float m_displayLevel;
    bool m_enabled;
    
    // Visual properties
    static constexpr int m_barCount = 20;
    static constexpr float m_peakDecay = 0.05f;
    static constexpr float m_levelDecay = 0.1f;
    
    std::vector<float> m_barLevels;
    std::vector<float> m_barPeaks;
};