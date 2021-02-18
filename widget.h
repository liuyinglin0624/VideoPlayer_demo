#ifndef WIDGET_H
#define WIDGET_H
#include <QWidget>
#include"videoplayer.h"
#include<QImage>
#include<QTimer>
#include<QTime>
#include"videoslider.h"
#include<QEvent>
#include<QKeyEvent>
QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

public slots:
    void slot_GetOneFrame(QImage image);
    void paintEvent(QPaintEvent *event);

    void slot_ButtonClick();

    void slot_GetTotalTime(qint64 uSec);

    void slot_TimeOut();

    void slot_sliderPress(int pos);

    void getCurrentState(VideoPlayer::PlayState state);
public:
    void keyPressEvent(QKeyEvent *event);

private slots:
    void on_slider_process_sliderMoved(int position);

    void on_pb_max_clicked();

private:
    Ui::Widget *ui;
    VideoPlayer* m_pPlayer;
    QImage m_Image;
    QTimer* m_timer;
    bool m_bStopState;
    VideoSlider* slider;
    QTimer* m_QuitTimer;
    QTime m_LastTime;
    bool m_bMaxFlag;
    QRect m_location;
};
#endif // WIDGET_H
