#include <SDL2/SDL_log.h>
#include <SDL2/SDL_render.h>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "video_player.h"

#define AUDIO_INBUF_SIZE 4096

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 24, 100)
#define AVCODEC_NEW_CHANNEL_LAYOUT
#endif

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 0, 100)
#define AVFORMAT_NEW_avcodec_find_decoder
#endif

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(60, 12, 100)
#define AVFORMAT_NEW_avio_alloc_context
#endif

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(59, 0, 100)
#define AVFORMAT_NEW_avformat_open_input
#define AVFORMAT_NEW_av_find_best_stream
#endif

#if LIBSWRESAMPLE_VERSION_INT >= AV_VERSION_INT(4, 14, 100)
#define AVFORMAT_NEW_swr_convert
#endif

static std::string av_error_to_str(int err)
{
    std::string ret;
    ret.resize(AV_ERROR_MAX_STRING_SIZE);
    av_strerror(err, &ret[0], AV_ERROR_MAX_STRING_SIZE);
    ret.resize(SDL_strlen(ret.c_str()));
    return ret;
}

int _rw_read_buffer(void *opaque, uint8_t *buf, int buf_size)
{
    DerVideoPlayer *music = (DerVideoPlayer *)opaque;
    size_t ret = SDL_RWread(music->m_src, buf, 1, buf_size);

    if (ret == 0) {
        return AVERROR_EOF;
    }

    return ret;
}

int64_t _rw_seek(void *opaque, int64_t offset, int whence)
{
    DerVideoPlayer *music = (DerVideoPlayer *)opaque;
    int rw_whence;

    switch(whence)
    {
    default:
    case SEEK_SET:
        rw_whence = RW_SEEK_SET;
        break;
    case SEEK_CUR:
        rw_whence = RW_SEEK_CUR;
        break;
    case SEEK_END:
        rw_whence = RW_SEEK_END;
        break;
    case AVSEEK_SIZE:
        return SDL_RWsize(music->m_src);
    }

    return SDL_RWseek(music->m_src, offset, rw_whence);
}


bool DerVideoPlayer::updateAudioStream()
{
    if(!m_audio || !m_audio->codecpar)
        return true; // No audio - no actions!

    enum AVSampleFormat sfmt = (enum AVSampleFormat)m_audio->codecpar->format;
    int srate = m_audio->codecpar->sample_rate;

#if defined(AVCODEC_NEW_CHANNEL_LAYOUT)
    int channels = m_audio->codecpar->ch_layout.nb_channels;
#else
    int channels = m_audio->codecpar->channels;
#endif
    int fmt = 0;

#if defined(AVCODEC_NEW_CHANNEL_LAYOUT)
    AVChannelLayout layout;
#else
    int layout;
#endif

    if(srate == 0 || channels == 0)
        return false;

    if(sfmt != m_sfmt || srate != m_srate || channels != m_schannels || !m_audio_cvt)
    {
        m_planar = false;

        switch(sfmt)
        {
        case AV_SAMPLE_FMT_U8P:
            m_planar = true;
            m_dst_sample_fmt = AV_SAMPLE_FMT_U8;
            /*fallthrough*/
        case AV_SAMPLE_FMT_U8:
            fmt = AUDIO_U8;
            break;

        case AV_SAMPLE_FMT_S16P:
            m_planar = true;
            m_dst_sample_fmt = AV_SAMPLE_FMT_S16;
            /*fallthrough*/
        case AV_SAMPLE_FMT_S16:
            fmt = AUDIO_S16SYS;
            break;

        case AV_SAMPLE_FMT_S32P:
            m_planar = true;
            m_dst_sample_fmt = AV_SAMPLE_FMT_S32;
            /*fallthrough*/
        case AV_SAMPLE_FMT_S32:
            fmt = AUDIO_S32SYS;
            break;

        case AV_SAMPLE_FMT_FLTP:
            m_planar = true;
            m_dst_sample_fmt = AV_SAMPLE_FMT_FLT;
            /*fallthrough*/
        case AV_SAMPLE_FMT_FLT:
            fmt = AUDIO_F32SYS;
            break;

        default:
            return -1; /* Unsupported audio format */
        }

        if(m_audio_cvt)
        {
            SDL_FreeAudioStream(m_audio_cvt);
            m_audio_cvt = NULL;
        }

        m_merge_buffer.clear();

        if(m_swr_ctx)
        {
            swr_free(&m_swr_ctx);
            m_swr_ctx = nullptr;
        }

        m_audio_cvt = SDL_NewAudioStream(fmt, (Uint8)channels, srate,
                                         m_dstSpec.format, m_dstSpec.channels, m_dstSpec.freq);
        if(!m_audio_cvt)
            return false;

        if(m_planar)
        {
            m_swr_ctx = swr_alloc();
#if defined(AVCODEC_NEW_CHANNEL_LAYOUT)
            layout = m_audio->codecpar->ch_layout;
#else
            layout = m_audio->codecpar->channel_layout;
#endif

#if defined(AVCODEC_NEW_CHANNEL_LAYOUT)
            if(layout.u.mask == 0)
            {
                layout.order = AV_CHANNEL_ORDER_NATIVE;
                layout.nb_channels = channels;

                if(channels > 2)
                    layout.u.mask = AV_CH_LAYOUT_SURROUND;
                else if(channels == 2)
                    layout.u.mask = AV_CH_LAYOUT_STEREO;
                else if(channels == 1)
                    layout.u.mask = AV_CH_LAYOUT_MONO;
            }

            av_opt_set_chlayout(m_swr_ctx, "in_chlayout",  &layout, 0);
            av_opt_set_chlayout(m_swr_ctx, "out_chlayout", &layout, 0);
#else
            if(layout == 0)
            {
                if(channels > 2)
                    layout = AV_CH_LAYOUT_SURROUND;
                else if(channels == 2)
                    layout = AV_CH_LAYOUT_STEREO;
                else if(channels == 1)
                    layout = AV_CH_LAYOUT_MONO;
            }

            av_opt_set_int(m_swr_ctx, "in_channel_layout",  layout, 0);
            av_opt_set_int(m_swr_ctx, "out_channel_layout", layout, 0);
#endif
            av_opt_set_int(m_swr_ctx, "in_sample_rate",     srate, 0);
            av_opt_set_int(m_swr_ctx, "out_sample_rate",    srate, 0);
            av_opt_set_sample_fmt(m_swr_ctx, "in_sample_fmt",  sfmt, 0);
            av_opt_set_sample_fmt(m_swr_ctx, "out_sample_fmt", m_dst_sample_fmt,  0);
            swr_init(m_swr_ctx);

#if defined(AVCODEC_NEW_CHANNEL_LAYOUT)
            av_channel_layout_uninit(&layout);
#endif

            m_merge_buffer.resize(channels * av_get_bytes_per_sample(sfmt) * 4096);
        }

        m_sfmt = sfmt;
        m_srate = srate;
        m_schannels = channels;
    }

    return true;
}

bool DerVideoPlayer::updateVideoStream()
{
    if(!m_video || !m_video->codecpar)
        return true; // No video - no actions!

    AVPixelFormat pixfmt = (AVPixelFormat)m_video->codecpar->format;
    int w = m_video->codecpar->width;
    int h = m_video->codecpar->height;

    if(pixfmt == AV_PIX_FMT_NONE || w == 0 || h == 0)
        return false;

    if(w != m_dst_w || h != m_dst_h || pixfmt != m_dst_colour || !m_video_cvt)
    {
        if(m_video_cvt)
        {
            sws_freeContext(m_video_cvt);
            m_video_cvt = nullptr;
        }

        m_video_cvt = sws_getContext(w, h, pixfmt, m_dst_w, m_dst_h, m_dst_colour, 0, 0, 0, 0);
        if(!m_video_cvt)
            return false;

        SDL_LockMutex(m_textureMutex);
        uint8_t *dst_data[4];
        int dst_line_sizes[4];

        int data_size = av_image_fill_arrays(dst_data, dst_line_sizes, nullptr, m_dst_colour, w, h, 8);
        m_texture_pitch = dst_line_sizes[0];
        m_texturePixelData.resize(data_size);

        SDL_UnlockMutex(m_textureMutex);
    }

    return false;
}

int DerVideoPlayer::decode_audio_packet(bool &got)
{
    int ret = 0;
    size_t unpadded_linesize;
    size_t sample_size;

    got = false;

    ret = avcodec_send_packet(m_decoderAudioCtx, &m_paquet);
    if(ret < 0)
    {
        if(ret == AVERROR_EOF)
            return ret;

        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "FFMPEG: ERROR: Error submitting a packet for decoding (%s)", av_error_to_str(ret).c_str());
        return ret;
    }

    while(ret >= 0)
    {
        ret = avcodec_receive_frame(m_decoderAudioCtx, m_audio_frame);

        if(ret < 0)
        {
            /* those two return values are special and mean there is no output */
            /* frame available, but there were no errors during decoding */
            if(ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                return 0;

            SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "FFMPEG: Error during decoding (%s)", av_error_to_str(ret).c_str());
            return ret;
        }

        updateAudioStream();

        if(m_planar)
        {
            sample_size = av_get_bytes_per_sample((enum AVSampleFormat)m_audio_frame->format);
            unpadded_linesize = sample_size * m_audio_frame->nb_samples * m_schannels;

            if(unpadded_linesize > m_merge_buffer.size())
                m_merge_buffer.resize(unpadded_linesize);

            uint8_t *out = m_merge_buffer.data();

            swr_convert(m_swr_ctx, &out, m_audio_frame->nb_samples,
                        (const Uint8**)m_audio_frame->extended_data, m_audio_frame->nb_samples);

            if (m_paquet.pts != AV_NOPTS_VALUE)
                m_time = (double)m_paquet.pts * av_q2d(m_audio->time_base);
            else
                m_time = -1.0;

            if(SDL_AudioStreamPut(m_audio_cvt, m_merge_buffer.data(), unpadded_linesize) < 0)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "FFMPEG: Failed to put audio stream");
                return -1;
            }
        }
        else
        {
            unpadded_linesize = m_audio_frame->nb_samples * av_get_bytes_per_sample((enum AVSampleFormat)m_audio_frame->format);

            if(SDL_AudioStreamPut(m_audio_cvt, m_audio_frame->extended_data[0], unpadded_linesize) < 0)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "FFMPEG: Failed to put audio stream");
                return -1;
            }
        }

        av_frame_unref(m_audio_frame);

        got = true;

        if(ret < 0)
            return ret;
    }

    return 0;
}

int DerVideoPlayer::decode_video_packet(bool &got)
{
    int ret = 0;
    size_t unpadded_linesize;
    size_t sample_size;

    got = false;

    ret = avcodec_send_packet(m_decoderVideoCtx, &m_paquet);
    if(ret < 0)
    {
        if(ret == AVERROR_EOF)
            return ret;

        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "FFMPEG: ERROR: Error submitting a packet for decoding (%s)", av_error_to_str(ret).c_str());
        return ret;
    }

    while(ret >= 0)
    {
        ret = avcodec_receive_frame(m_decoderVideoCtx, in_frame);

        if(ret < 0)
        {
            /* those two return values are special and mean there is no output */
            /* frame available, but there were no errors during decoding */
            if(ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                return 0;

            SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "FFMPEG: Error during decoding (%s)", av_error_to_str(ret).c_str());
            return ret;
        }

        updateVideoStream();

        SDL_LockMutex(m_textureMutex);

        uint8_t *out[] = {m_texturePixelData.data()};
        int lines[] = {m_texture_pitch};

        sws_scale(m_video_cvt,
                  in_frame->data, in_frame->linesize, 0, in_frame->height,
                  out, lines);

        SDL_UnlockMutex(m_textureMutex);
        m_hasVideoFrame = true;

        av_frame_unref(in_frame);

        got = true;

        if (ret < 0)
            return ret;
    }

    return 0;
}

DerVideoPlayer::DerVideoPlayer(SDL_Renderer *dst) :
    m_render(dst)
{
    SDL_memset(&m_paquet, 0, sizeof(AVPacket));
}

DerVideoPlayer::~DerVideoPlayer()
{
    close();
}

void DerVideoPlayer::setAudioSpec(SDL_AudioSpec &spec)
{
    m_dstSpec = spec;
}

void DerVideoPlayer::setRender(SDL_Renderer *dst)
{
    m_render = dst;
}

void DerVideoPlayer::close()
{
    if(m_audio_cvt)
    {
        SDL_FreeAudioStream(m_audio_cvt);
        m_audio_cvt = nullptr;
    }

    if(m_swr_ctx)
    {
        swr_free(&m_swr_ctx);
        m_swr_ctx = nullptr;
    }

    if(m_video_cvt)
    {
        sws_freeContext(m_video_cvt);
        m_video_cvt = nullptr;
    }

    m_texture_colour = AV_PIX_FMT_NONE;
    m_texture_w = 0;
    m_texture_h = 0;

    m_dst_colour = AV_PIX_FMT_NONE;
    m_dst_w = 0;
    m_dst_h = 0;

    if(m_texture)
    {
        SDL_DestroyTexture(m_texture);
        m_texture = nullptr;
    }

    m_merge_buffer.clear();

    av_frame_free(&sw_frame);
    av_frame_free(&in_frame);
    av_frame_free(&m_audio_frame);

    /* flush the decoder */
    m_paquet.data = nullptr;
    m_paquet.size = 0;
    if(m_paquet.buf)
        av_packet_unref(&m_paquet);

    SDL_memset(&m_paquet, 0, sizeof(AVPacket));

    if(m_decoderAudioCtx)
        avcodec_free_context(&m_decoderAudioCtx);

    if(m_decoderVideoCtx)
        avcodec_free_context(&m_decoderVideoCtx);

    if(m_inputCtx)
    {
        avformat_close_input(&m_inputCtx);
        m_inputCtx = nullptr;
    }

    if(m_textureMutex)
    {
        SDL_DestroyMutex(m_textureMutex);
        m_textureMutex = nullptr;
    }

    in_buffer = NULL; /* This buffer is already freed by FFMPEG side*/
    in_buffer_size = 0;

    if(m_src && m_freesrc)
        SDL_RWclose(m_src);

    m_src = nullptr;
    m_freesrc = false;

    m_video = nullptr;
    m_audio = nullptr;
    m_decoderVideo = nullptr;
    m_decoderAudio = nullptr;
    m_sfmt = AV_SAMPLE_FMT_NONE;
    m_srate = 0;
    m_schannels = 0;
    m_planar = false;
}

bool DerVideoPlayer::loadVideo(SDL_RWops *src, bool freesrc)
{
    AVDictionary *options = nullptr;
    int ret;
    char proto[] = "file:///sdl_rwops";
    close();

    in_buffer = (uint8_t *)av_malloc(AUDIO_INBUF_SIZE);
    in_buffer_size = AUDIO_INBUF_SIZE;
    if(!in_buffer)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "FFMPEG: Out of memory");
        close();
        return false;
    }

    m_src = src;

    avio_in = avio_alloc_context(in_buffer,
                                 in_buffer_size,
                                 0,
                                 this,
                                 _rw_read_buffer,
                                 nullptr,
                                 _rw_seek);
    if(!avio_in)
    {
        close();
        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "FFMPEG: Unhandled file format");
        return false;
    }

    m_inputCtx = avformat_alloc_context();
    m_inputCtx->pb = avio_in;
    m_inputCtx->url = proto;

    /* open the input file */
    ret = avformat_open_input(&m_inputCtx, nullptr, nullptr, &options);
    av_dict_free(&options);

    if(ret != 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "Cannot open input file");
        close();
        return false;
    }

    if(avformat_find_stream_info(m_inputCtx, NULL) < 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "Cannot find input stream information");
        close();
        return false;
    }

    ret = av_find_best_stream(m_inputCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &m_decoderVideo, 0);
    if(ret < 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "No suitable video stream in the input file");
        close();
        return false;
    }

    m_streamVideo = ret;
    m_video = m_inputCtx->streams[ret];

    ret = av_find_best_stream(m_inputCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &m_decoderAudio, 0);
    if(ret >= 0)
    {
        m_streamAudio = ret;
        m_audio = m_inputCtx->streams[ret];
    }

    if(!(m_decoderVideoCtx = avcodec_alloc_context3(m_decoderVideo)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "No enough memory to initialise the video decoder!");
        close();
        return false;
    }

    if(!m_video->codecpar)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "FFMPEG: codec parameters aren't recognised");
        close();
        return false;
    }

    if(m_audio && !(m_decoderAudioCtx = avcodec_alloc_context3(m_decoderAudio)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "No enough memory to initialise the audio decoder!");
        close();
        return false;
    }

    if(m_audio && !m_audio->codecpar)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "FFMPEG: codec parameters aren't recognised    m_texture_colour = AV_PIX_FMT_RGB24;");
        close();
        return false;
    }

    if(avcodec_parameters_to_context(m_decoderVideoCtx, m_video->codecpar) < 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "Error of avcodec_parameters_to_context (video)");
        close();
        return false;
    }

    if(m_audio && avcodec_parameters_to_context(m_decoderAudioCtx, m_audio->codecpar) < 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "Error of avcodec_parameters_to_context (audio)");
        close();
        return false;
    }

    m_decoderVideoCtx->sw_pix_fmt = AV_PIX_FMT_RGB24;
    m_decoderVideoCtx->opaque = this;

    if(m_decoderAudioCtx)
        m_decoderAudioCtx->opaque = this;

    ret = avcodec_open2(m_decoderVideoCtx, m_decoderVideo, nullptr);
    if(ret < 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "Failed avcodec_open2 (video)");
        close();
        return false;
    }

    if(m_audio)
    {
        ret = avcodec_open2(m_decoderAudioCtx, m_decoderAudio, nullptr);
        if(ret < 0)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "Failed avcodec_open2 (audio)");
            close();
            return false;
        }
    }

    if(!(in_frame = av_frame_alloc()) || !(sw_frame = av_frame_alloc()))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "Can not alloc frame");
        close();
        return false;
    }

    if(m_audio && !(m_audio_frame = av_frame_alloc()))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_ERROR, "Can not alloc audio frame");
        close();
        return false;
    }

    m_freesrc = freesrc;

    m_texture_colour = AV_PIX_FMT_RGB24;
    m_texture_w = 0;
    m_texture_h = 0;

    m_dst_colour = AV_PIX_FMT_RGB24;
    m_dst_w = m_video->codecpar->width;
    m_dst_h = m_video->codecpar->height;

    m_texturePixelData.resize(m_dst_h * m_texture_pitch);
    SDL_memset(m_texturePixelData.data(), 0, m_texturePixelData.size());

    updateVideoStream();
    updateAudioStream();

    // av_dump_format(m_inputCtx, m_streamVideo, video_path.c_str(), 0);

    // if(m_streamAudio >= 0)
    //     av_dump_format(m_inputCtx, m_streamAudio, video_path.c_str(), 0);

    m_time = 0.0;
    m_atEnd = false;

    m_textureMutex = SDL_CreateMutex();

    return true;
}

bool DerVideoPlayer::atEnd() const
{
    return m_atEnd;
}

bool DerVideoPlayer::hasVideoFrame() const
{
    return m_hasVideoFrame;
}

void DerVideoPlayer::drawVideoFrame()
{
    SDL_LockMutex(m_textureMutex);

    if(m_texture_w != m_dst_w || m_texture_h != m_dst_h)
    {
        if(m_texture)
        {
            SDL_DestroyTexture(m_texture);
            m_texture = nullptr;
        }
        m_texture_w = m_dst_w;
        m_texture_h = m_dst_h;
    }

    if(!m_texture)
    {
        m_texture = SDL_CreateTexture(m_render, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, m_texture_w, m_texture_h);
        SDL_UpdateTexture(m_texture, nullptr, m_texturePixelData.data(), m_texture_pitch);
        m_hasVideoFrame = false;
    }

    if(m_hasVideoFrame)
    {
        SDL_UpdateTexture(m_texture, nullptr, m_texturePixelData.data(), m_texture_pitch);
        m_hasVideoFrame = false;
    }

    // FIXME: Implement aspect ration keeping!
    SDL_RenderCopy(m_render, m_texture, nullptr, nullptr);

    SDL_UnlockMutex(m_textureMutex);
}

int DerVideoPlayer::runAV(Uint8 *stream, int len)
{
    int filled, ret = 0;
    bool got_some, got_video;

    filled = SDL_AudioStreamGet(m_audio_cvt, stream, len);
    if(filled != 0)
        return filled;

    got_some = false;
    got_video = false;

    while(av_read_frame(m_inputCtx, &m_paquet) >= 0)
    {
        /* check if the packet belongs to a stream we are interested in, otherwise */
        /* skip it */
        if(m_paquet.stream_index == m_streamAudio)
            ret = decode_audio_packet(got_some);
        else if(m_paquet.stream_index == m_streamVideo)
            ret = decode_video_packet(got_video);

        av_packet_unref(&m_paquet);

        if(ret < 0 || got_some)
            break;
    }

    if(!got_some || ret == AVERROR_EOF)
    {
        SDL_AudioStreamFlush(m_audio_cvt);
        m_atEnd = true;
    }

    return 0;
}

void DerVideoPlayer::audio_out_stream(void *self, Uint8 *stream, int bytes)
{
    DerVideoPlayer *p = (DerVideoPlayer*)self;
    int got;
    Uint8 *snd = (Uint8 *)stream;
    Uint8 *dst = snd;
    int len = bytes;
    int zero_cycles = 0;

    while(len > 0 && !p->m_atEnd)
    {
        got = p->runAV(dst, len);

        if(got <= 0)
        {
            ++zero_cycles;
            if(zero_cycles >= 10)
                p->m_atEnd = true;
            continue;
        }

        dst += got;
        len -= got;
    }
}
