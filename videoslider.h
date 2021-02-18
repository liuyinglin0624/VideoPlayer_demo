#ifndef VIDEOSLIDER_H
#define VIDEOSLIDER_H
#include<QSlider>
#include<QMouseEvent>

class VideoSlider : public QSlider
{
    Q_OBJECT
signals:
    void SIG_sliderPress(int Pos);
public:
    explicit VideoSlider();
public:
    void mousePressEvent(QMouseEvent *ev); // 重写鼠标点击事件
signals:

};

#endif // VIDEOSLIDER_H
