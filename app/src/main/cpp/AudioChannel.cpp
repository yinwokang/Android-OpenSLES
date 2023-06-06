#include "AudioChannel.h"

void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *args);
void *task_audio_decode(void *args);
void *task_audio_play(void *args);

AudioChannel::AudioChannel(AVCodecContext *codecContext) {
    this->codecContext = codecContext;

    out_channels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    out_sample_size = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16); // 每个sample是16 bit == 2字节
    out_sample_rate = 44100; // 采样率

    out_buffers_size = out_sample_rate * out_sample_size * out_channels; // 大小 = 采样率 * 帧率 * 通道数
    out_buffers = static_cast<uint8_t *>(malloc(out_buffers_size)); // 堆区开辟

    // FFmpeg 初始化 重采样上下文
    swr_ctx = swr_alloc_set_opts(0,
                                 AV_CH_LAYOUT_STEREO,  // 声道布局类型 双声道
                                 AV_SAMPLE_FMT_S16,  // 采样大小 16bit
                                 out_sample_rate, // 采样率  44100
                                 codecContext->channel_layout, // 声道布局类型
                                 codecContext->sample_fmt, // 采样大小 32bit  aac
                                 codecContext->sample_rate,  // 采样率
                                 0, 0);
    swr_init(swr_ctx);
}

void AudioChannel::start() {
    isPlaying = 1;

    // 队列开始工作
    packets.setWork(1);
    frames.setWork(1);

    // 音频解码线程
    pthread_create(&pid_audio_decode, nullptr, task_audio_decode, this);
    // 音频播放线程
    pthread_create(&pid_audio_play, nullptr, task_audio_play, this);
}

void *task_audio_decode(void *args) {  // 很头痛，C的子线程函数必须是这个格式，所以要包装一层....
    auto *audio_channel = static_cast<AudioChannel *>(args);
    audio_channel->audio_decode();
    return nullptr;
}

/**
 * 音频解码：codecContext 把 AVPacket 解码为 AVFrame
 */
void AudioChannel::audio_decode() {
    AVPacket *pkt = nullptr;
    while (isPlaying) {
        if (isPlaying && frames.size() > 100) {
            av_usleep(10 * 1000);
            continue;
        }
        int ret = packets.getQueueAndDel(pkt);
        if (!ret) {
            continue; // 生产-消费模型，所以可能会失败，重来就行
        }
        // TODO 把 AVPacket 给 codecContext 解码
        ret = avcodec_send_packet(codecContext, pkt);

        AVFrame *frame = av_frame_alloc(); // 给 AVFrame 开辟内存

        // TODO 从 codecContext 中拿解码后的产物 AVFrame
        ret = avcodec_receive_frame(codecContext, frame);

        if (ret == AVERROR(EAGAIN))
            continue; // 音频帧可能获取失败，重新拿一次

        // 原始包 AVFrame 加入播放队列
        frames.insertToQueue(frame);
    }
}

void *task_audio_play(void *args) {  // 头痛头痛
    auto *audio_channel = static_cast<AudioChannel *>(args);
    audio_channel->audio_play();
    return nullptr;
}

/**
 * 音频播放
 */
void AudioChannel::audio_play() {
    SLresult result; // 用于接收 执行成功或者失败的返回值

    // TODO 创建引擎对象并获取【引擎接口】
    slCreateEngine(&engineObject, 0, 0,
                   0, 0, 0);
    (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineInterface);

    // TODO 创建、初始化混音器
    (*engineInterface)->CreateOutputMix(engineInterface, &outputMixObject, 0, 0, 0);
    (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);

    // TODO 创建并初始化播放器
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 10};
    SLDataFormat_PCM format_pcm = {SL_DATAFORMAT_PCM, // PCM数据格式
                                   2, // 声道数
                                   SL_SAMPLINGRATE_44_1, // 采样率（每秒44100个点）
                                   SL_PCMSAMPLEFORMAT_FIXED_16, // 每秒采样样本 存放大小 16bit
                                   SL_PCMSAMPLEFORMAT_FIXED_16, // 每个样本位数 16bit
                                   SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT, // 前左声道  前右声道
                                   SL_BYTEORDER_LITTLEENDIAN};
    SLDataSource audioSrc = {&loc_bufq, &format_pcm};
    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
    SLDataSink audioSnk = {&loc_outmix, NULL};
    const SLInterfaceID ids[1] = {SL_IID_BUFFERQUEUE};
    const SLboolean req[1] = {SL_BOOLEAN_TRUE};
    (*engineInterface)->CreateAudioPlayer(engineInterface, &bqPlayerObject, &audioSrc, &audioSnk, 1, ids, req);
    (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);
    (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY, &bqPlayerPlay);

    // TODO 设置回调函数
    (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_BUFFERQUEUE, &bqPlayerBufferQueue);
    (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue, bqPlayerCallback, this);

    // TODO 设置播放状态
    (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);

    // 6.手动激活回调函数
    bqPlayerCallback(bqPlayerBufferQueue, this);
}

/**
 * 真正播放的函数，这个函数会一直调用
 * 关键是 SLAndroidSimpleBufferQueueItf 这个结构体，往这个结构体的队列加 PCM 数据就能播放了
 */
void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *args) {
    auto *audio_channel = static_cast<AudioChannel *>(args);
    int pcm_size = audio_channel->getPCM();

    (*bq)->Enqueue(bq, audio_channel->out_buffers, pcm_size);
}

/**
 * 音频重采样 和 计算 PCM 数据大小
 */
int AudioChannel::getPCM() {
    int pcm_data_size = 0;
    AVFrame *frame = nullptr;
    while (1) {
        int ret = frames.getQueueAndDel(frame);

        if (!ret) continue;

        int dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, frame->sample_rate) +
                                            frame->nb_samples,
                                            out_sample_rate,
                                            frame->sample_rate,
                                            AV_ROUND_UP);
        int samples_per_channel = swr_convert(swr_ctx,
                                              &out_buffers, // 重采样后的 buffer
                                              dst_nb_samples,
                                              (const uint8_t **) frame->data,
                                              frame->nb_samples);
        pcm_data_size = samples_per_channel * out_sample_size * out_channels; // 通道样本数 * 样本格式字节数 * 声道数

        break;
    }
    return pcm_data_size;
}



