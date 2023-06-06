#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <sys/types.h>
#include "safe_queue.h"
#include "log.h"

extern "C" {
#include <libswresample/swresample.h> // 对音频数据进行转换（重采样）
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
}


class AudioChannel {
private:
    pthread_t pid_audio_decode;
    pthread_t pid_audio_play;

public:
    int out_channels;
    int out_sample_size;
    int out_sample_rate;
    int out_buffers_size;
    uint8_t *out_buffers = 0;
    SwrContext *swr_ctx = 0;

    bool isPlaying; // 音频 和 视频 都会有的标记 是否播放
    SafeQueue<AVPacket *> packets; // 压缩的 数据包   AudioChannel.cpp(packets 1)   VideoChannel.cpp(packets 2)
    SafeQueue<AVFrame *> frames; // 原始的 数据包     AudioChannel.cpp(frames 3)   VideoChannel.cpp(frames 4)

    AVCodecContext *codecContext = 0; // 音频 视频 都需要的 解码器上下文

    // 引擎
    SLObjectItf engineObject = 0;
    // 引擎接口
    SLEngineItf engineInterface = 0;
    // 混音器
    SLObjectItf outputMixObject = 0;
    // 播放器
    SLObjectItf bqPlayerObject = 0;
    // 播放器接口
    SLPlayItf bqPlayerPlay = 0;

    // 播放器队列接口
    SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue = 0;

    AudioChannel(AVCodecContext *codecContext);

    ~AudioChannel();

    void stop();

    void start();

    void audio_decode();

    void audio_play();

    int getPCM();

};