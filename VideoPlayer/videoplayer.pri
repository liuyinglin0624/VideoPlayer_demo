HEADERS += \
    $$PWD/videoplayer.h

SOURCES += \
    $$PWD/videoplayer.cpp

INCLUDEPATH += $$PWD/ffmpeg/include \
               $$PWD/SDL2-2.0.10/include

LIBS += $$PWD/ffmpeg/lib/avcodec.lib\
        $$PWD/ffmpeg/lib/avdevice.lib\
        $$PWD/ffmpeg/lib/avfilter.lib\
        $$PWD/ffmpeg/lib/avformat.lib\
        $$PWD/ffmpeg/lib/avutil.lib\
        $$PWD/ffmpeg/lib/postproc.lib\
        $$PWD/ffmpeg/lib/swresample.lib\
        $$PWD/ffmpeg/lib/swscale.lib\
        $$PWD/SDL2-2.0.10/lib/x86/SDL2.lib
