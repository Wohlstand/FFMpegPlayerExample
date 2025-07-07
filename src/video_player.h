#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include <SDL2/SDL_audio.h>
#include <SDL2/SDL_mutex.h>

extern "C"
{
#include <libavcodec/version_major.h>
#include <libavcodec/packet.h>
#include <libavutil/samplefmt.h>
#include <libavutil/pixfmt.h>
}

#include <vector>


struct SDL_Renderer;
typedef struct SDL_Renderer SDL_Renderer;
struct SDL_Texture;
typedef struct SDL_Texture SDL_Texture;
struct _SDL_AudioStream;
typedef struct _SDL_AudioStream SDL_AudioStream;

// FFMPEG's structures
struct AVFormatContext;
typedef struct AVFormatContext AVFormatContext;
struct AVStream;
typedef struct AVStream AVStream;
struct AVCodecContext;
typedef struct AVCodecContext AVCodecContext;
struct AVCodec;
typedef struct AVCodec AVCodec;
struct AVFrame;
typedef struct AVFrame AVFrame;
struct SwrContext;
typedef struct SwrContext SwrContext;
struct SwsContext;
typedef struct AVIOContext AVIOContext;
struct AVIOContext;

class DerVideoPlayer
{
    friend int64_t _rw_seek(void *opaque, int64_t offset, int whence);
    friend int _rw_read_buffer(void *opaque, uint8_t *buf, int buf_size);
    Uint8 *in_buffer = nullptr;
    size_t in_buffer_size = 0;

    //! Where to draw
    SDL_Renderer   *m_render = nullptr;
    SDL_Texture    *m_texture = nullptr;
    SDL_mutex      *m_textureMutex = nullptr;
    std::vector<uint8_t> m_texturePixelData;
    AVIOContext     *avio_in = nullptr;
    SDL_RWops       *m_src = nullptr;
    bool            m_freesrc = false;

    SwsContext      *m_video_cvt = nullptr;

    AVPixelFormat   m_texture_colour = AV_PIX_FMT_NONE;
    int             m_texture_w = 0;
    int             m_texture_h = 0;
    int             m_texture_pitch = 0;

    AVPixelFormat   m_dst_colour = AV_PIX_FMT_NONE;
    int             m_dst_w = 0;
    int             m_dst_h = 0;

    double          m_time = 0.0;
    bool            m_atEnd = false;
    bool            m_hasVideoFrame = false;

    /* ------------------------------------------ */
    //! Input context of video stream
    AVFormatContext *m_inputCtx = nullptr;
    //! Number of video stream
    int             m_streamVideo = 0;
    //! Number of audio stream
    int             m_streamAudio = -1;
    //! Actual stream of video
    AVStream       *m_video = nullptr;
    //! Video decoder context
    AVCodecContext *m_decoderVideoCtx = nullptr;

    //! Decoder itself
#if LIBAVCODEC_VERSION_MAJOR >= 60
    const AVCodec  *m_decoderVideo = nullptr;
#else
    AVCodec        *m_decoderVideo = nullptr;
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 60
    const AVCodec  *m_decoderAudio = nullptr;
#else
    AVCodec        *m_decoderAudio = nullptr;
#endif
    AVCodecContext *m_decoderAudioCtx = nullptr;

    //! Frames to process
    AVFrame        *sw_frame = nullptr;
    AVFrame        *in_frame = nullptr;
    //! Packet buffer
    AVPacket        m_paquet;

    //! Actual stream of audio
    AVStream       *m_audio = nullptr;
    AVFrame        *m_audio_frame = nullptr;

    //! Audio stream to adjust
    SDL_AudioStream *m_audio_cvt = nullptr;
    //! Converts planar audio streams to the compatible format
    SwrContext      *m_swr_ctx = nullptr;
    enum AVSampleFormat m_sfmt = AV_SAMPLE_FMT_NONE;
    int             m_srate = 0;
    int             m_schannels = 0;
    bool            m_planar = false;

    enum AVSampleFormat m_dst_sample_fmt = AV_SAMPLE_FMT_NONE;
    std::vector<uint8_t> m_merge_buffer;

    SDL_AudioSpec   m_dstSpec;
    /* ------------------------------------------ */

    /**
     * @brief Synchronise audio converters with the stream
     * @return true if all okay, or false if error happen
     *
     * Synchronises all the audio converters if stream changes the content (this might happen if stream is a Frankenstein).
     */
    bool updateAudioStream();
    bool updateVideoStream();

    int decode_audio_packet(bool &got);
    int decode_video_packet(bool &got);

public:
    explicit DerVideoPlayer(SDL_Renderer *dst = nullptr);
    ~DerVideoPlayer();

    void setAudioSpec(SDL_AudioSpec &spec);
    void setRender(SDL_Renderer *dst);

    void close();

    bool loadVideo(struct SDL_RWops *src, bool freesrc);

    bool atEnd() const;
    bool hasVideoFrame() const;

    void drawVideoFrame();

    int runAV(Uint8 *stream, int len);

    static void audio_out_stream(void *self, Uint8 *stream, int bytes);
};

#endif // VIDEO_PLAYER_H
