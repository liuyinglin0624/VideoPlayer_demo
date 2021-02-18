#include "widget.h"
#include "ui_widget.h"
#include<QPainter>
#include<QFileDialog>
#include<qDebug>
#include<QDesktopWidget>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);
    this->m_pPlayer = new VideoPlayer;

// 对于ffmpeg与SDL的初始化，不需要一播放就初始化一次。在最开始初始化就好
    //初始化FFMPEG  调用了这个才能正常使用编码器和解码器
    av_register_all();
    // ffmpeg支持接收rtmp信号流，需要我们主动打开ffmpeg网络模块
    avformat_network_init();
    // SDL初始化
    if (SDL_Init(SDL_INIT_AUDIO))
    {
        fprintf(stderr,"Could not initialize SDL - %s. \n", SDL_GetError());
        exit(1);
    }
    this->m_bMaxFlag = false;
    m_bStopState = false;

    setFocusPolicy(Qt::ClickFocus); // 注意对于空格与方向键，qt无法直接接收。需要设置界面的默认焦点

    connect(m_pPlayer,SIGNAL(SIG_GetOneFrame(QImage )),this,SLOT(slot_GetOneFrame(QImage)) );
    connect(m_pPlayer,SIGNAL(SIG_SendTotalTime(qint64)),this,SLOT(slot_GetTotalTime(qint64)) );
    connect(m_pPlayer,SIGNAL(SIG_SendCurrentState(VideoPlayer::PlayState )),this,SLOT(getCurrentState(VideoPlayer::PlayState)));

    connect(this->ui->pb_open,SIGNAL(clicked()),this,SLOT(slot_ButtonClick()) );
    connect(this->ui->pb_play,SIGNAL(clicked()),this,SLOT(slot_ButtonClick()) );
    connect(this->ui->pb_close,SIGNAL(clicked()),this,SLOT(slot_ButtonClick()) );
    connect(this->ui->pb_pause,SIGNAL(clicked()),this,SLOT(slot_ButtonClick()) );

    m_timer = new QTimer;
    m_timer->setInterval(500);
    connect(this->m_timer,SIGNAL(timeout() ) , this,SLOT(slot_TimeOut()) );

    slider=this->ui->slider_process;
    connect(slider,SIGNAL(SIG_sliderPress(int)), this,SLOT(slot_sliderPress(int)) );

    m_QuitTimer = new QTimer;
    m_QuitTimer->setInterval(1000);
    connect(this->m_QuitTimer,SIGNAL(timeout() ), this,SLOT(slot_TimeOut()) );
    //m_QuitTimer->start();
    // 设置快捷键
}

Widget::~Widget()
{
    delete ui;
}

void Widget::keyPressEvent(QKeyEvent *event)
{
    if(event->key() == Qt::Key_Space)
    {
        if(this->m_pPlayer->getPlayerState() == VideoPlayer::Playing)
        {
            this->m_pPlayer->pause();
        }
        else if(this->m_pPlayer->getPlayerState() == VideoPlayer::Pause)
        {
            this->m_pPlayer->play();
        }
    }
}

// 获得一帧图片
void Widget::slot_GetOneFrame(QImage image)
{
    this->m_Image = image;
    update(); // 调用paintEvent
    this->m_LastTime = QTime::currentTime(); // 更新时间
}

void Widget::slot_sliderPress(int pos)
{
    qint64 val = pos * 1000000;
    this->m_pPlayer->seek(val);
}

// 重绘
void Widget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this); // 获取界面的画布
    painter.setBrush(Qt::black); // 设置画刷
    painter.drawRect(0,0,this->width(),this->height()); // 将界面设置为全黑
    if(this->m_bStopState) return ;
    if(this->m_Image.size().width() <= 0) return ;
    // KeepAspectRatio 保持缩放比
    QImage image = this->m_Image.scaled(this->size(),Qt::KeepAspectRatio);

    int x = this->width() - image.width();
    int y = this->height() - image.height() - 22;
    x = x/2;
    y = y/2;

    painter.drawImage(x,y,image);
}

// 控件点击
void Widget::slot_ButtonClick()
{
    if(sender() == this->ui->pb_play)
    {
        this->m_pPlayer->play();
    }
    else if(sender() == this->ui->pb_pause)
    {
        this->m_pPlayer->pause();
    }
    else if(sender() == this->ui->pb_close)
    {
        this->m_pPlayer->stop(true);
    }
    else if(sender() == this->ui->pb_open)
    {

        QString strPath  = QFileDialog::getOpenFileName(this,"选择播放文件","D:/",
                       "视频文件 (*.flv *.rmvb *.avi *.mp4 *.mkv) ;; 所有文件(*.*);;");

        //QString strPath = "rtmp://192.168.153.129/vod/3.flv";
        if(!strPath.isEmpty())
        {
            qDebug() << strPath;
            this->m_pPlayer->stop(true);  // 阻塞，将所有的线程全部停止
            this->m_pPlayer->SetFileName(strPath);
            //开启定时器

            this->m_timer->start(); // 定时器每个半秒，获取当前时间，更新再UI上
        }

    }
}

// UI界面收到音视频编码解码所发送的总时间
void Widget::slot_GetTotalTime(qint64 uSec)
{
    int64_t Sec = uSec / 1000000;  // 获取秒级别的时间

    this->ui->slider_process->setRange(0,Sec);

    QString hStr = QString("00%1").arg(Sec/3600);
    QString mStr = QString("00%1").arg(Sec%3600/60);
    QString sStr = QString("00%1").arg(Sec%60);

    QString strTotalTime = QString("00:00:00/%1:%2:%3").arg(hStr.right(2)).arg(mStr.right(2)).arg(sStr.right(2));
    this->ui->lb_time->setText(strTotalTime);
    this->m_bStopState = false;
}

// 定时器时间到
void Widget::slot_TimeOut()
{
    if(sender() == this->m_timer)
    {
        qint64 sec = this->m_pPlayer->GetCurrentTime() / 1000000; // 获取当前时间
        this->ui->slider_process->setValue(sec);  // 滑块向后移动

        QString hStr = QString("00%1").arg(sec/3600);
        QString mStr = QString("00%1").arg(sec%3600/60);
        QString sStr = QString("00%1").arg(sec%60);

        QString strTime = this->ui->lb_time->text();
        QString strTotalTime = QString("%1:%2:%3").arg(hStr.right(2)).arg(mStr.right(2)).arg(sStr.right(2));
        this->ui->lb_time->setText(strTotalTime + strTime.right(9));
    }
    else if(sender() == this->m_QuitTimer)
    {
        QTime currentTime = QTime::currentTime();
        if(this->m_LastTime.secsTo(currentTime) > 3)
        {
            this->m_Image.fill(Qt::black);
            this->m_LastTime = QTime::currentTime();
            update();
        }
    }
}


//slider_process滑动的信号
void Widget::on_slider_process_sliderMoved(int position)
{
    if(this->ui->slider_process == sender())
    {
        qint64 pos = position * 1000000;
        this->m_pPlayer->seek(pos);
    }
}

void Widget::getCurrentState(VideoPlayer::PlayState state)
{
    if(state == VideoPlayer::Stop)
    {
        qDebug() << "clear ";
        this->m_bStopState = true;
        update();
    }
}
// 点击最大化
void Widget::on_pb_max_clicked()
{
    if(this->m_bMaxFlag)
    {
        this->setGeometry(this->m_location);
        this->ui->pb_max->setText("最大化");
    }
    else
    {
        m_location = this->geometry();
        this->setGeometry(qApp->desktop()->availableGeometry());// 最大化
        this->ui->pb_max->setText("还原");
    }

    this->m_bMaxFlag = !this->m_bMaxFlag;
}

