#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QThread>
#include<QImage>
#include"SDL_audio.h"

extern"C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include "SDL.h"
}
// packet缓冲队列
typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;


// 视频状态结构体，在视频音频同步中使用
class VideoPlayer ;
typedef struct VideoState {
    AVFormatContext *pFormatCtx;//相当于视频”文件指针”
    AVCodecContext *aCodecCtx; //音频解码器
    AVStream        *audio_st; //音频流
    AVFrame *audioFrame;// 解码音频过程中的使用缓存
    PacketQueue *audioq;//音频缓冲队列
    int videoStream, audioStream;
    double video_clock; ///<pts of last decoded frame 视频时钟
    AVCodecContext *pCodecCtx; //音频解码器
    AVStream        *video_st; //视频流
    AVFrame *videoFrame;// 解码音频过程中的使用缓存

    AVFrame out_frame; //设置参数，供解码后的swr_alloc_set_opts使用。
    PacketQueue *videoq;//视频队列

    SDL_Thread *video_tid;  //视频线程id
    SDL_AudioDeviceID audioID; //音频ID
    int64_t start_time ; //开始时间 , 用于计算主时钟
    double audio_clock; ///<pts of last decoded frame //音频时间


    // 播放控制
    bool isPause;// 暂停标志
    bool quit;  // 停止标志
    bool readFinished;  // 文件读取结束
    bool readThreadFinished;  // 读取线程结束标志
    bool videoThreadFinished;  // 视频播放线程结束标志

    // 音频与视频进行跳转的变量
    int seek_req;  // 跳转标志-- 读线程
    int64_t seek_pos;  // 跳转的位置-- 微秒
    int seek_flag_audio;  //跳转的位置-- 用于音频
    int seek_flag_video;  //跳转的位置-- 用于视频
    double seek_time;      //跳转的时间（秒）

    // 针对rtmp数据流的时钟控制标志
    bool beginFrame;

    SDL_mutex *mutexProcess;

    VideoState()
    {
        audio_clock = video_clock =start_time =  0;
    }
    VideoPlayer* m_player;//用于调用函数
} VideoState;

int video_thread(void *arg);

class VideoPlayer : public QThread
{
    Q_OBJECT
public:
    enum PlayState // 播放状态的枚举
    {
        Playing,
        Pause,
        Stop
    };
public:
    explicit VideoPlayer();
signals:
    void SIG_GetOneFrame(QImage );  // 将一帧的图片发送上去
    void SIG_SendTotalTime(qint64 );  // 用来获取视频总时长
    void SIG_SendCurrentState(VideoPlayer::PlayState );
public slots:
    // 音视频播放函数
    void VideoPlay();
    void run();
    void SetFileName(QString FileName)//-- 对外接口
    {
       // this->clear();
        this->mVideoState.beginFrame = false;
        // 确保run函数是唯一执行。若run函数没有执行结束，则不会另外创建一根线程，导致播放混乱
        if(this->m_State == Stop)
        {
            this->m_strFileName = FileName;
            this->start();
            this->m_State = Playing;
        }
        return ;
    }
    void SendGetOneFrame(QImage img); // 调用SIGNAL函数，将信号发射出去
    // 多媒体控制函数
    void play();//-- 对外接口
    void pause();//-- 对外接口
    void stop(bool isWait);  //-- 对外接口
    void clear();
    // 获取当前时间与整体时间---对外接口
    int64_t GetTotalTime();
    double GetCurrentTime();
    // 跳转使用的函数---对外接口
    void seek(qint64 pos);
    PlayState getPlayerState();
private:
    QString m_strFileName;
    // 用来传递给SDL音频回调函数的数据 userdata --> m_ViderState;
    VideoState mVideoState;
    PlayState m_State;
};

#endif // VIDEOPLAYER_H
