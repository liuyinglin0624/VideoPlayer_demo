#include "videoplayer.h"
#include<QMessageBox>
#include<QDebug>
// 对于此处使用的c文件，我要在cpp文件中进行导入
extern"C"
{
#include"libavcodec/avcodec.h"
#include"libavdevice/avdevice.h"
#include"libavformat/avformat.h"
#include"libswscale/swscale.h"
#include"libswresample/swresample.h"
#include"libavutil/time.h"
}

#define SDL_AUDIO_BUFFER_SIZE (1024)
#define AVCODEC_MAX_AUDIO_FRAME_SIZE (192000)

#define MAX_AUDIO_SIZE 1024*16*25
#define MAX_VIDEO_SIZE 1024*255*25

#define FLUSH_DATA "FLUSH"  // 向Packet中写入FLUSH

// 音频缓冲队列的初始化
void packet_queue_init(PacketQueue *q) {
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}
// 音频缓冲队列的放入packet,尾添加
int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

    AVPacketList *pkt1;
    if (av_dup_packet(pkt) < 0) {
        return -1;
    }
    pkt1 = (AVPacketList*)av_malloc(sizeof(AVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    SDL_LockMutex(q->mutex);

    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;
    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
    return 0;
}
// 音频缓冲队列取出packet，头删除
int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
    AVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;) {

        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            *pkt = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

// 队列清空
void packet_queue_flush(PacketQueue *q)
{
    AVPacketList *pkt, *pkt1;

    SDL_LockMutex(q->mutex);
    for(pkt = q->first_pkt; pkt != NULL; pkt = pkt1)
    {
        pkt1 = pkt->next;
        av_free_packet(&pkt->pkt);
        av_freep(&pkt);
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    SDL_UnlockMutex(q->mutex);
}

void VideoPlayer::SendGetOneFrame(QImage img)
{
    emit SIG_GetOneFrame(img);
}
// 播放
void VideoPlayer::play()
{
    this->mVideoState.isPause = false;
    if(this->m_State != Pause) return ; // 若当前状态不是暂停，那么播放也没有意义

    this->m_State = Playing;
    emit SIG_SendCurrentState(Playing);
}
// 暂停
void VideoPlayer::pause()
{
    // 首先将播放状态结构体中的 isPause设置为true
    this->mVideoState.isPause = true;
    if(this->m_State != Playing) return ;  //若当前状态不为播放状态，则pause是没有意义的
    this->m_State = Pause;  // 将播放状态设置为暂停
    emit SIG_SendCurrentState(Pause);
}
// 停止
void VideoPlayer::stop(bool isWait)
{
    // 若当前状态已经时停止状态
    if(this->m_State == Stop) return;
    this->mVideoState.quit = 1; // 设置退出标志·

    // 退出标志
    if(isWait)
    {
        while(!this->mVideoState.readThreadFinished || !this->mVideoState.videoThreadFinished)
        {
            SDL_Delay(10);
        }
    }

    // 关闭SDL设备,音频播放线程再SDL_PauseAudio中结束
    if(this->mVideoState.audioID != 0)
    {
        SDL_LockAudio();
        SDL_PauseAudioDevice(this->mVideoState.audioID,1);
        SDL_UnlockAudio();


    }

    this->m_State = Stop; // 设置状态位。
    emit SIG_SendCurrentState(Stop);
}

// 每次播放之前，对VideoPlayer对象中的某种状态进行设置清空
void VideoPlayer::clear()
{
    this->mVideoState.isPause = false;
    this->m_State = Stop;  // 当前播放状态设置为停止状态

    // 将音频与视频的时间都设置为0.
    this->mVideoState.audio_clock = mVideoState.video_clock = mVideoState.start_time = 0;
}
// 获取总时间
int64_t VideoPlayer::GetTotalTime()
{
     return this->mVideoState.pFormatCtx->duration; // 返回文件流的持续时间，即为播放时间
}
// 获取当前时间
double VideoPlayer::GetCurrentTime()
{
    if(this->mVideoState.quit)
        return 0;
    return this->mVideoState.audio_clock; // 返回当前的时钟，作为当前的播放时间
}

//实现跳转
void VideoPlayer::seek(qint64 pos)
{
    // 跳转时，首先设置跳转标志，将缓存队列中的内容进行清空（否则将队列中的内容全部播放完毕，再播放我们指定的视频）
    // 利用av_seek_frame()函数，解码器直接跳转到我们指定的位置。
    if(!this->mVideoState.seek_req)  // 若当前的跳转标志 = 0
    {
        this->mVideoState.seek_pos = pos;
        this->mVideoState.seek_req = 1;
    }
}

// 获取当前的播放状态
VideoPlayer::PlayState VideoPlayer::getPlayerState()
{
    return this->m_State;
}

int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size);
void audio_callback(void *userdata, Uint8 *stream, int len);
double synchronize_video(VideoState *is, AVFrame *src_frame, double pts);


VideoPlayer::VideoPlayer()
{
    this->clear();
    this->mVideoState.mutexProcess = SDL_CreateMutex(); // 对三个线程获取VideoState
}
// 开始播放
void VideoPlayer::VideoPlay()
{
    this->start(); // 开启多线程
}

// 用于视频解码的SDL多线程函数
int video_thread(void *arg)
{
    VideoState *is = (VideoState *) arg;
    AVPacket pkt1, *packet = &pkt1;

    int ret, got_picture, numBytes; // 存储结果

    double video_pts = 0; //当前视频的pts
    double audio_pts = 0; //音频pts
    double begin_pts = 0;  // 初始的视频起始时间

    ///解码视频相关
    AVFrame *pFrame, *pFrameRGB;  // 存储解码后的数据信息
    uint8_t *out_buffer_rgb; //解码后的rgb数据
    struct SwsContext *img_convert_ctx;  //用于解码后的视频格式转换

    AVCodecContext *pCodecCtx = is->video_st->codec; //视频解码器的相关信息

    // 为AVFrame申请空间
    pFrame = av_frame_alloc();
    pFrameRGB = av_frame_alloc();

    ///这里我们改成了 将解码后的YUV数据转换成RGB32
   img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
           pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height,
           AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);

   numBytes = avpicture_get_size(AV_PIX_FMT_RGB32, pCodecCtx->width,pCodecCtx->height);

   out_buffer_rgb = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));

   // 挂载缓冲区
   avpicture_fill((AVPicture *) pFrameRGB, out_buffer_rgb, AV_PIX_FMT_RGB32,
           pCodecCtx->width, pCodecCtx->height);

   while(1)
   {
       if(is->quit) break; // 若当前退出状态

        if(is->isPause == true)  // 若当前的状态是暂停，则video_thread一直等待
        {
            SDL_Delay(10);
            continue;
        }

        //队列里面没有数据了  读取完毕了
        if (packet_queue_get(is->videoq, packet, 0) <= 0)
        {
            //  读取完毕了
            if(is->readFinished)
            {
                break;
            }
            else
            {// 暂时队列中没有数据
                SDL_Delay(1);
                continue;
            }

        }

        if(strcmp((char*)packet->data,FLUSH_DATA) == 0) // 若清空缓存
        {
            avcodec_flush_buffers(is->video_st->codec);// 清空解码器中的相关数据
            av_free_packet(packet);  // 初始化packet
            is->video_clock = 0;
            continue;
        }

        while(1)
        {
            if(is->quit) break;  // 退出
            if(is->audioq->size == 0 && is->audio_clock - audio_pts < 1)
            {
                break;
            }
            // 获取音频时间戳，若当前的视频时间戳
            SDL_LockMutex(is->mutexProcess);
            audio_pts = is->audio_clock;
            video_pts = is->video_clock; // 记录当前的时间戳
            SDL_UnlockMutex(is->mutexProcess);

            if (video_pts <= audio_pts || audio_pts == 0) break;
            // 音视频同步
            SDL_Delay(5);// 若视频的时间戳大于音频时间戳，说明视频快了，进行延时。
        }

        // 进行解码
        ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture,packet);
        // 获取时间戳
        if (packet->dts == AV_NOPTS_VALUE && pFrame->opaque&& *(uint64_t*) pFrame->opaque != AV_NOPTS_VALUE)
        {
            video_pts = *(uint64_t *) pFrame->opaque;
        }
        else if (packet->dts != AV_NOPTS_VALUE)
        {
            video_pts = packet->dts;
        }
        else
        {
            video_pts = 0;
        }
        // 对于读取rtmp数据流的情况下，
        // 我们以音频时钟作为主时钟，音频时钟的获取是通过计算一个packet大小来不断前进的。
        // 但是视频时钟我们是通过每个packet中的pts显示时间戳来计算的。
        // 对于rtmp数据流，提取到的视频时间戳不为0。但我们的音频时间戳一定是从0开始计算
        // 就会出现持续等待的情况。
        // 解决办法：第一次获取packet时，记录一下时间。然后每次获得的视频时间戳-起始视频时间戳。即为0起始的视频时间戳
        if(!is->beginFrame)
        {
            begin_pts = video_pts;
            is->beginFrame =  true;
        }
        video_pts -= begin_pts;
        video_pts *= 1000000 *av_q2d(is->video_st->time_base);
        video_pts = synchronize_video(is, pFrame, video_pts);

        // 对于跳转到前一个关键帧的情况
        if(is->seek_flag_video) // 若关键帧进行跳转
        {
            // 对于之后跳转的问题，若跳转的时间晚于视频的时间，说明关键帧落在指定位置之前
            // 将这几帧的数据舍弃
            SDL_LockMutex(is->mutexProcess);
            if(is->video_clock < is->seek_pos)
            {
                av_free_packet(packet);
                continue;
            }
            else
            {
                is->seek_flag_video = 0;
            }
            SDL_UnlockMutex(is->mutexProcess);
        }

        // 若出现严重的延时，视频帧丢弃
        if(is->audio_clock - video_pts >= 500000)
        {
            av_free_packet(packet);
            continue;
        }

        if(got_picture)
        {
            // 对解码数据进行格式转换
            sws_scale(img_convert_ctx,
                     (uint8_t const * const *) pFrame->data,
                     pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data,
                      pFrameRGB->linesize);
            //
            QImage tmpImg((uchar *)out_buffer_rgb,pCodecCtx->width,pCodecCtx->height,QImage::Format_RGB32);
            QImage image = tmpImg.copy(); //把图像复制一份 传递给界面显示
            is->m_player->SendGetOneFrame(image); //调用激发信号的函数
        }
        av_free_packet(packet);
   }
   // 若视频播放完成，但并没有设置文件退出结束。强制退出
   if(!is->quit)
   {
        is->quit = true; //
   }
   av_free(pFrame);
   av_free(pFrameRGB);
   av_free(out_buffer_rgb);

   is->videoThreadFinished = true;
   qDebug() << "videoThreadFinished true";
   return 0;
}



// 播放视频
void VideoPlayer::run()
{
    // 获取文件地址
    std::string str = this->m_strFileName.toStdString();
    char *file_path = (char*)str.c_str();

    AVFormatContext *pFormatCtx;
    //视频使用的编码器 pCodecCtx pCodec
    AVCodecContext *pCodecCtx;
    AVCodec *pCodec;

    //视频使用的  YUV420p对应的 AVFrame    RGB32 对应的 pFrameRGB
    AVFrame *pFrame, *pFrameRGB;

    //音视频共用的packet 视频读取的缓冲区out_buffer
    AVPacket *packet;
    uint8_t *out_buffer;

    //音频所使用的编码器 aCodecCtx aCodec
    AVCodecContext *aCodecCtx;
    AVCodec *aCodec;

    // 对于rtmp地址的数据流，若由于网络问题出现延时，需要等待
    int DelayCount = 0;

    static struct SwsContext *img_convert_ctx; // 图像格式转换结构体

    // 音频流索引，视频流索引
    int audioStream ,videoStream, i, numBytes;
    // 解码结果
    int ret, got_picture;


    memset(&this->mVideoState,0,sizeof(mVideoState)); // 将mVideoState清空
//    this->clear();  //每次开始播放时，清空
    //申请 AVFormatContext. 用于打开视频文件
    pFormatCtx = avformat_alloc_context();
    // 打开视频数据流
    if (avformat_open_input(&pFormatCtx, file_path, NULL, NULL) != 0) {
        printf("can't open the file. \n");
        return;
    }

    if(avformat_find_stream_info(pFormatCtx,NULL) < 0)
    {
        qDebug() << "find_stream_info error";
        return ;
    }

    this->mVideoState.pFormatCtx = pFormatCtx;
    videoStream = -1; // 音频数据流索引
    audioStream = -1; // 视频数据流索引
    //循环查找视频中包含的流信息，videoStream audioStream
    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStream = i;
        }
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO  && audioStream < 0)
        {
            audioStream = i;
        }
    }

    PacketQueue *audioq;  // 音频队列
    PacketQueue *videoq;  // 视频队列
    AVFrame* audioFrame;
    SDL_AudioSpec spec;
    SDL_AudioSpec wanted_spec;  // 音频设备信息设置


    ///如果videoStream为-1 说明没有找到视频流
    if (videoStream != -1) {
        pCodecCtx = pFormatCtx->streams[videoStream]->codec;
        pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
        if (pCodec == NULL)
        {
                printf("PCodec not found.\n");
                return;
        }
        ///打开视频解码器
        if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
            printf("Could not open video codec.\n");
            return;
        }
        // 设置结构体中流索引对应的数据流，在计算时间戳使用数据流中的时基信息
        mVideoState.video_st = pFormatCtx->streams[videoStream];
        mVideoState.audio_st = pFormatCtx->streams[audioStream];
        videoq = new PacketQueue;  // 定义视频队列
        packet_queue_init(videoq);  // 初始化视频队列

        mVideoState.videoq = videoq;  // 设置mVideoState中的视频队列

        mVideoState.m_player = this;   // 设置对象
        // 创建线程解码播放函数
        mVideoState.video_tid = SDL_CreateThread(video_thread, "video_thread", &mVideoState);
        mVideoState.videoStream = videoStream;
    }

    if (audioStream != -1) {
        //初始化音频队列
        audioq = new PacketQueue;
        packet_queue_init(audioq);
        // 首先打开音频解码器，
            //获取音频解码器的相关信息，查找音频解码器
        aCodecCtx = pFormatCtx->streams[audioStream]->codec;
        aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
        if (aCodec == NULL)
        {
            printf("ACodec not found.\n");
            return;
        }
        if (avcodec_open2(aCodecCtx, aCodec, NULL) < 0) {
            printf("Could not open audio codec.\n");
            return;
        }
        // 分配解码过程中的使用缓存
        audioFrame = avcodec_alloc_frame();

        mVideoState.aCodecCtx = aCodecCtx; // VideoState结构体设置编码信息
        mVideoState.audioq = audioq;  // VideoState结构体设置Packet队列
        mVideoState.audioFrame = audioFrame;
        mVideoState.audio_st = pFormatCtx->streams[audioStream];
        mVideoState.m_player = this;
        ///  打开SDL播放设备 - 开始
        SDL_LockAudio();
        wanted_spec.freq = aCodecCtx->sample_rate; // 设置音频输出频率
        wanted_spec.format = AUDIO_S16SYS;  // 双声道
        wanted_spec.channels = aCodecCtx->channels; // 设置通道数
        wanted_spec.silence = 0;  // 设置静音值
        wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;  // 设置播放缓冲区大小
        wanted_spec.callback = audio_callback; // 设置回调函数
        wanted_spec.userdata = &mVideoState;  // 传入回调函数的参数
        // SDL开启另一根线程，并且SDL设置播放设备
        this->mVideoState.audioID = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(0,0),0,&wanted_spec,&spec,0);
        if(mVideoState.audioID<0)
        {
            qDebug() << "SDL_OpenAudioDevice error\n";
        }

//        if(SDL_OpenAudio(&wanted_spec, &spec) <= 0)
//        {
//        }
        // 设置以下参数，供解码后的swr_alloc_set_opts使用。
        mVideoState.out_frame.format         = AV_SAMPLE_FMT_S16;
        mVideoState.out_frame.sample_rate    = aCodecCtx->sample_rate;
        mVideoState.out_frame.channels       = aCodecCtx->channels ;
        mVideoState.out_frame.channel_layout = av_get_default_channel_layout(aCodecCtx->channels);

        SDL_UnlockAudio();

        SDL_PauseAudioDevice(this->mVideoState.audioID,0);  // 代开音频设备
        //SDL_PauseAudio(0);//  打开SDL播放设备，回调函数开始执行，但由于队列中没有Pakcet,多线程被挂起。
    }

    packet = (AVPacket *) malloc(sizeof(AVPacket)); //分配一个packet

    mVideoState.start_time = av_gettime(); // 设置mVideoState中的其实时钟时间

    int64_t time = this->GetTotalTime();
    SIG_SendTotalTime(time);

    while(1)
    {
        // 对于结束的情况
        if(this->mVideoState.quit)
        {
            break; //退出
        }
        // 若跳转
        if(this->mVideoState.seek_req)
        {// 1. 清除队列与解码器中的缓存
            int stream_index = -1;
            int64_t seek_target = mVideoState.seek_pos; // 设置跳转位置
            // 找到流索引
            if (mVideoState.videoStream >= 0)
                stream_index = mVideoState.videoStream;
            else if (mVideoState.audioStream >= 0)
                stream_index = mVideoState.audioStream;

            AVRational aVRational = {1, AV_TIME_BASE};
            if (stream_index >= 0) {
                seek_target = av_rescale_q(seek_target, aVRational,
                        pFormatCtx->streams[stream_index]->time_base); //跳转到的位置
            }

            // 进行跳转，跳转到seek_target位置,AVSEEK_FLAG_BACKWARD表示跳转到对应位置的前一个关键帧
            if (av_seek_frame(mVideoState.pFormatCtx, stream_index, seek_target, AVSEEK_FLAG_BACKWARD) < 0) {
                fprintf(stderr, "%s: error while seeking\n",mVideoState.pFormatCtx->filename);
            } else {
                // 若文件为音频文件，首先清空队列。定义packet，向其中写入"FLUSH"。解码器后续进行判断
                SDL_LockMutex(mVideoState.mutexProcess);
                if (mVideoState.audioStream >= 0) {
                    AVPacket *packet = (AVPacket *) malloc(sizeof(AVPacket)); //分配一个packet
                    av_new_packet(packet, 10);
                    strcpy((char*)packet->data,FLUSH_DATA);
                    packet_queue_flush(mVideoState.audioq); //清除队列
                    packet_queue_put(mVideoState.audioq, packet); //往队列中存入用来清除的包
                }
                if (mVideoState.videoStream >= 0) {
                    AVPacket *packet = (AVPacket *) malloc(sizeof(AVPacket)); //分配一个packet
                    av_new_packet(packet, 10);
                    strcpy((char*)packet->data,FLUSH_DATA);
                    packet_queue_flush(mVideoState.videoq); //清除队列
                    packet_queue_put(mVideoState.videoq, packet); //往队列中存入用来清除的包
                    mVideoState.video_clock = 0; //考虑到向左快退，否则视频会卡在跳转之前的位置持续等待。
                }
                SDL_UnlockMutex(mVideoState.mutexProcess);
            }
            SDL_LockMutex(mVideoState.mutexProcess);
            mVideoState.seek_req = 0;
            mVideoState.seek_time = mVideoState.seek_pos ; //精确到微妙
            mVideoState.seek_flag_audio = 1; // 用来音频解码时，判断跳转的标志
            mVideoState.seek_flag_video = 1; // 用来视频解码时，判断跳转的标志
            SDL_UnlockMutex(mVideoState.mutexProcess);
        }
        if(this->mVideoState.isPause ==  true)
        {
            SDL_Delay(10);
            continue;
        }
        //  size 为缓冲区中数据包的字节数大于上限，则持续等待，缓解内存空间
        if (mVideoState.audioq->size > MAX_AUDIO_SIZE || mVideoState.videoq->size > MAX_VIDEO_SIZE)
        {
            SDL_Delay(10);
            continue;
        }

        if (av_read_frame(pFormatCtx, packet) < 0)
        {
             //这里认为视频读取完了,或者出现了rtmp数据流卡顿问题
            // 卡顿3s再退出
            DelayCount++;
            if(DelayCount >= 300)
            {
                SDL_LockMutex(mVideoState.mutexProcess);
                this->mVideoState.readFinished = true;  // 读取标志置位
                SDL_UnlockMutex(mVideoState.mutexProcess);
                DelayCount = 0;
            }

            if(this->mVideoState.quit) // 若推出标志==1 则退出
                break;
            //否则线程卡在该位置
            SDL_Delay(10);
            continue;
        }
        DelayCount = 0;


        // 若当前得到的packet为视频流
        if (packet->stream_index == videoStream)
        {
            // 将packet压入videoq视频队列中
            packet_queue_put(mVideoState.videoq, packet);
        }
        else if( packet->stream_index == audioStream )
        {
            packet_queue_put(mVideoState.audioq, packet);
        }
        else
        {
            av_free_packet(packet);
        }
    }
    while(!this->mVideoState.quit)  // 若read结束任务自己退出来
    {
        SDL_Delay(100);  // 放置由于解码太快，关键信息被销毁
    }

    this->stop(false);

    avcodec_close(pCodecCtx);

    // 在视频解码线程结束前，不可以关闭pFormatCtx。否则关闭视频会出现野指针的问题
    while( !this->mVideoState.videoThreadFinished )
    {
        SDL_Delay(100);
    }

    // 对资源队列进行释放
    if(this->mVideoState.videoStream != -1)
        packet_queue_flush(this->mVideoState.videoq);
    if(this->mVideoState.audioStream != -1)
        packet_queue_flush(this->mVideoState.audioq);

    if(mVideoState.videoStream != -1)
    {
        avcodec_close(pCodecCtx);
    }
    else
    {
        mVideoState.videoThreadFinished = true;
    }

    avformat_close_input(&pFormatCtx);

    mVideoState.readThreadFinished = true;
    qDebug() << "mVideoState.readThreadFinished = true";
}


// 音频解码
int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size)
{
    static AVPacket pkt;
    static uint8_t *audio_pkt_data = NULL;
    static int audio_pkt_size = 0;
    int len1, data_size;

    AVCodecContext *aCodecCtx = is->aCodecCtx;
    AVFrame *audioFrame = is->audioFrame;
    PacketQueue *audioq = is->audioq;

    static struct SwrContext   *swr_ctx = NULL;

    int convert_len;
    int n = 0;

    while(1)
    {
        if(is->quit) return -1;

        if(is->isPause == true)  // 暂停播放音视频
        {
            SDL_Delay(10);
            continue;
        }

        // 注意该线程是SDL线程，是不会人为指定去销毁的。
        // 若推出，则由于memset将packet队列清空，则取数据就会出现问题
        if(!audioq) return -1;

        // 需要注意：对于非阻塞等待的情况，有可能会返回0.队列中没有元素的时候会返回0
        if(packet_queue_get(audioq, &pkt, 0) <= 0)
        {
            return -1;
        }

        if(strcmp((char*)pkt.data,FLUSH_DATA) == 0) // 若清空缓存
        {
            avcodec_flush_buffers(is->audio_st->codec);// 清空解码器中的相关数据
            av_free_packet(&pkt);  // 初始化packet
            is->video_clock = 0;
            continue;
        }

        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;

        while(audio_pkt_size > 0)
        {
            if(is->quit) return -1;
            int got_picture;

            int ret =avcodec_decode_audio4( aCodecCtx, audioFrame, &got_picture, &pkt);
            if( ret < 0 ) {
                printf("Error in decoding audio frame.\n");
                exit(0);
            }

//            // 1. 目前只能播放双声道的视频，对于单声道有问题。单声道所计算出来的data_size大于实际的一帧音频数据的长度
//            // 导致计算的主时钟audio_clock变慢
//            data_size = audioFrame->nb_samples * 4;
//            // 利用完成解码的audioFrame来计算时间戳
//            // 对于音频数据而言，解码数据的大小对应着其播放的时间长度
//            // 因为SDL播放音频是通过定时时间调用回调函数，回调函数播放指定大小数据。
//            // 所以可以利用解码数据的大小来计算播放该帧需要的时间
//            n = 2*is->audio_st->codec->channels;
//            is->audio_clock += (double)data_size* 1000000
//                    / (double) (n*is->audio_st->codec->sample_rate);


            // 2. 利用当前的声道，动态计算主时钟
            data_size = audioFrame->nb_samples * is->audio_st->codec->channels *2; // 利用当前的声道，计算一帧音频数据的大小

            // 计算一帧音频数据大小
            int sampleSize = av_samples_get_buffer_size(NULL,is->audio_st->codec->channels,audioFrame->nb_samples,is->audio_st->codec->sample_fmt,1);
            //计算每一帧数据所占用的字节数
            n = av_get_bytes_per_sample(is->audio_st->codec->sample_fmt)*is->audio_st->codec->channels;
            // 利用数据帧大小，计算当前的音频时钟（主时钟）
            is->audio_clock += (double)sampleSize*1000000 / (double)(n*is->audio_st->codec->sample_rate);

            if(is->seek_flag_audio)
            {
                if(is->audio_clock < is->seek_time) //  定位的关键帧往前飘
                {
                    // 此处需要注意，尽管跳转关键帧到定位关键帧之间的packet被丢弃。
                    // 但是音频时钟同样在向后移动
                    if( pkt.pts != AV_NOPTS_VALUE )
                    {
                        // 若pkt下的音频时间戳有效，获取音频时间戳作为主时钟，
                        // 对时间戳进行刷新，对于未来向前定位播放位置，这种获取时间戳得到方式比较有利
                        SDL_LockMutex(is->mutexProcess);
                        is->audio_clock = av_q2d(is->audio_st->time_base) * pkt.pts * 1000000;
                        SDL_UnlockMutex(is->mutexProcess);
                    }
                    break;
                }
                else
                {
                    if( pkt.pts != AV_NOPTS_VALUE )
                    {
                        // 若pkt下的音频时间戳有效，获取音频时间戳作为主时钟，
                        // 对时间戳进行刷新，对于未来向前定位播放位置，这种获取时间戳得到方式比较有利
                        SDL_LockMutex(is->mutexProcess);
                        is->audio_clock = av_q2d(is->audio_st->time_base) * pkt.pts * 1000000;
                        SDL_UnlockMutex(is->mutexProcess);
                    }
                    is->seek_flag_audio = 0;
                }
            }

            if(got_picture)
            {
                if (swr_ctx != NULL)
                {
                     swr_free(&swr_ctx);
                     swr_ctx = NULL;
                }
                // 实现音频转码
                swr_ctx = swr_alloc_set_opts(NULL, is->out_frame.channel_layout,
                       (AVSampleFormat)is->out_frame.format,is->out_frame.sample_rate,
                       audioFrame->channel_layout,(AVSampleFormat)audioFrame->format,
                       audioFrame->sample_rate, 0, NULL);

                if (swr_ctx == NULL || swr_init(swr_ctx) < 0)
                {
                     printf("swr_init error\n");
                     break;
                }

                convert_len = swr_convert(swr_ctx, &audio_buf,
                      AVCODEC_MAX_AUDIO_FRAME_SIZE,
                      (const uint8_t **)audioFrame->data,
                      audioFrame->nb_samples);
            }
            audio_pkt_size -= ret;

            if (audioFrame->nb_samples <= 0)
            {
                continue;
            }
            // videoState中的audio_clock为主时钟

            //注意该位置获取的音频时间，也就是主时钟。
            // 若暂停播放，av_gettime函数的作用是获取当前的时间，当前时间是持续向后进行的。
            //  所以尽管暂停播放，但是当前的主时钟仍然在向后跑。
            // 所以为了解决这个问题，我们需要在暂停的时候start_time记录当前时间
            // 主时钟的提升方式 ：+当前时间与上一次解码时间的差值
            // 若暂停，则当前时间与上一次解码时间相同，主时钟同样暂停
//            is->audio_clock += av_gettime() - is->start_time; //主时钟时间
//            is->start_time = av_gettime();

            return data_size;
        }
        if(pkt.data)
            av_free_packet(&pkt);

//        is->audio_clock += av_gettime() - is->start_time; //主时钟时间
//        is->start_time = av_gettime();
    }
}
void audio_callback(void *userdata, Uint8 *stream, int len)
{
    VideoState *is = (VideoState *) userdata;

    int len1, audio_data_size;

    static uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
    static unsigned int audio_buf_size = 0;  // 当前数据位置
    static unsigned int audio_buf_index = 0;  // 当前播放位置

    while (len > 0)
    {
        // 需要注意：在SDL音频的回调函数中，对音频进行解码以及播放
        // 在回调函数中，不可以追加音频的播放控制
        // 因为音频的播放控制涉及主时钟的设定，我们要控制线程在解码函数中进行暂停

        // 若缓冲区的所有数据均播放完成
        if (audio_buf_index >= audio_buf_size)
        {
            // 解码一个packet，对缓冲区进行覆盖
            audio_data_size = audio_decode_frame(is, audio_buf,sizeof(audio_buf));
            if (audio_data_size < 0)
            {
                /* silence */
                audio_buf_size = 1024;
                /* 清零，静音 */
                memset(audio_buf, 0, audio_buf_size);
            }
            else
            {
                audio_buf_size = audio_data_size; // 标记缓冲区未播放数据位置
            }
            audio_buf_index = 0;// 从头开始
        }

        // 若缓冲中数据多与len个字节数，则仅仅读取len个
        len1 = audio_buf_size - audio_buf_index;
        if (len1 > len) {
            len1 = len;
        }

        memcpy(stream, (uint8_t *) audio_buf + audio_buf_index, len1);
        //SDL_MixAudio(stream,(uint8_t *) audio_buf + audio_buf_index,len1,SDL_MIX_MAXVOLUME);

        len -= len1;
        stream += len1;
        audio_buf_index += len1;
    }

}

// 计算下一帧的时间戳
double synchronize_video(VideoState *is, AVFrame *src_frame, double pts)
{
    double frame_delay;
    // 设置pts当前的时间戳
    if (pts != 0) {
        /* if we have pts, set video clock to it */
        is->video_clock = pts;
    } else {
        /* if we aren't given a pts, set it to the clock */
        pts = is->video_clock;
    }
    // 计算单位一帧图片的·延时时间
    frame_delay = av_q2d(is->video_st->codec->time_base); // 计算视频数据流的时基

    // 解码后，AVFrame结构体中的repeat_pict表示有多少张图片必须要延时。
    // 即播放完当前Frame需要延时多长时间
    // 额外的延时时间 = repeat_pict/ 2 * fps(时基)
    frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);

    // 当前的播放时间戳 + 播放该图片的进行延时的时间 = 下次播放的时间
    is->video_clock += frame_delay;
    return pts;
}


