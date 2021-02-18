#include "videoslider.h"
#include<QStyle>
VideoSlider::VideoSlider()
{

}
// 鼠标点击
void VideoSlider::mousePressEvent(QMouseEvent *ev)
{
    int PosX = ev->pos().x();

    int w = this->width();
    // 获取当前风格下的，点击位置的值
    int val = QStyle::sliderValueFromPosition(minimum(),maximum(),ev->pos().x(),width());

    setValue(val);

    emit SIG_sliderPress(val);
}
