#pragma once

#include <QWidget>
#include <QPaintEvent>
#include <QPaintEngine>
#include "GameCapture.h"

// Forward declare OBS types
struct obs_display;
typedef struct obs_display obs_display_t;

class PreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewWidget(GameCapture* capture, QWidget* parent = nullptr);
    ~PreviewWidget();

    void startPreview();
    void stopPreview();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    QPaintEngine* paintEngine() const override;

private:
    static void renderPreview(void* data, uint32_t cx, uint32_t cy);
    
    GameCapture* m_capture;
    obs_display_t* m_display;
    bool m_previewActive;
};