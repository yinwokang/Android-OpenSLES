#include <jni.h>
#include <string>
#include "log.h"
#include <android/native_window_jni.h>
#include <pthread.h>
#include "AudioChannel.h"

extern "C" { // ffmpeg是纯c写的，必须采用c的编译方式，否则奔溃
#include <libavformat/avformat.h>
}

void *task_start(void *args);
void *task_prepare(void *args);

pthread_t pid_prepare;
pthread_t pid_start;
AudioChannel *audio_channel = nullptr;
AVFormatContext *formatContext = nullptr; // 媒体上下文 封装格式
AVDictionary *dictionary = nullptr;

extern "C"
JNIEXPORT void JNICALL
Java_cn_wk_opensl_1demo_MainActivity_audioPlayer(JNIEnv *env, jobject thiz, jstring dataStr) {
    const char *dataSource = env->GetStringUTFChars(dataStr, nullptr);

    pthread_create(&pid_prepare, nullptr, task_prepare, (void *) dataSource);
}

/**
 * FFmpeg 对媒体文件 做处理
 */
void *task_prepare(void *args) {
    const char *data_source = (const char *) args;
    LOGI("data_source: %s", data_source)

    formatContext = avformat_alloc_context(); // 给 媒体上下文 开辟内存
    av_dict_set(&dictionary, "timeout", "5000000", 0); // 设置字典参数

    // TODO 打开媒体地址（如：文件路径，直播地址rtmp等）
    avformat_open_input(&formatContext, data_source, nullptr, &dictionary);
    // 释放字典(用完就释放)
    av_dict_free(&dictionary);

    // TODO 查找媒体中的音视频流的信息
    avformat_find_stream_info(formatContext, nullptr);

    // TODO 根据流信息，把 音频流、视频流 分开处理
    for (int stream_index = 0; stream_index < formatContext->nb_streams; ++stream_index) {
        AVStream *stream = formatContext->streams[stream_index];      // 获取媒体流（视频，音频）
        AVCodecParameters *parameters = stream->codecpar;             // 从流中获取 编解码 参数
        AVCodec *codec = avcodec_find_decoder(parameters->codec_id);  // 获取编解码器
        AVCodecContext *codecContext = avcodec_alloc_context3(codec); // 给 codecContext 开辟内存
        avcodec_parameters_to_context(codecContext, parameters);      // codecContext 初始化
        avcodec_open2(codecContext, codec, nullptr);          // 打开 编解码器

        // 从编解码参数中，区分流的类型，分别处理 (codec_type == 音频流/视频流/字幕流)
        if (parameters->codec_type == AVMediaType::AVMEDIA_TYPE_AUDIO) {
            LOGI("音频流")
            audio_channel = new AudioChannel(codecContext); // codecContext 才是真正干活的
            audio_channel->start(); // 开启 解码线程 和 播放线程

            pthread_create(&pid_start, nullptr, task_start, nullptr); // 数据传输线程
        } else if (parameters->codec_type == AVMediaType::AVMEDIA_TYPE_VIDEO) {
            LOGI("视频流")
        } else if (parameters->codec_type == AVMediaType::AVMEDIA_TYPE_SUBTITLE) {
            LOGI("字幕流")
        }
    }
    return nullptr; // 函数的返回值是 void* 必须返回 nullptr
}

/**
 * 将 AVPacket 传给 AudioChannel
 */
void *task_start(void *args) {
    while (1) {
        if (audio_channel && audio_channel->packets.size() > 100) {
            av_usleep(10 * 1000); // FFmpeg 的时间是微秒，所以这个是10毫秒
            continue;
        }
        AVPacket *packet = av_packet_alloc();             // 给 AVPacket 开辟内存
        int ret = av_read_frame(formatContext, packet);   // 从 formatContext 读帧赋值到 AVPacket
        if (!ret) {
            audio_channel->packets.insertToQueue(packet);
        } else {
            break;
        }
    }
    return nullptr;
}