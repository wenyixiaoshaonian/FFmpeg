#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C"
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale_internal.h>
#include <libavutil/timestamp.h>


static FILE *video_dst_file = NULL;
static FILE *audio_dst_file = NULL;
static uint8_t *video_dst_data[4] = {NULL};
static int video_dst_linesize[4];
static int audio_frame_count = 0;
static int video_dst_bufsize = 0;
static AVCodecContext *pCodecCtx_v= NULL;
static AVCodecContext *pCodecCtx_a= NULL;
static int get_format_from_sample_fmt(const char **fmt,
                                      enum AVSampleFormat sample_fmt)
{
    int i;
    struct sample_fmt_entry {
        enum AVSampleFormat sample_fmt; const char *fmt_be, *fmt_le;
    } sample_fmt_entries[] = {
        { AV_SAMPLE_FMT_U8,  "u8",    "u8"    },
        { AV_SAMPLE_FMT_S16, "s16be", "s16le" },
        { AV_SAMPLE_FMT_S32, "s32be", "s32le" },
        { AV_SAMPLE_FMT_FLT, "f32be", "f32le" },
        { AV_SAMPLE_FMT_DBL, "f64be", "f64le" },
    };
    *fmt = NULL;

    for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
        struct sample_fmt_entry *entry = &sample_fmt_entries[i];
        if (sample_fmt == entry->sample_fmt) {
            *fmt = AV_NE(entry->fmt_be, entry->fmt_le);
            return 0;
        }
    }

    fprintf(stderr,
            "sample format %s is not supported as output format\n",
            av_get_sample_fmt_name(sample_fmt));
    return -1;
}

static int output_video_frame(AVCodecContext *dec,AVFrame *frame)
{
    if (frame->width != dec->width || frame->height != dec->height ||
        frame->format != dec->pix_fmt) {
        /* To handle this change, one could call av_image_alloc again and
         * decode the following frames into another rawvideo file. */
        fprintf(stderr, "Error: Width, height and pixel format have to be "
                "constant in a rawvideo file, but the width, height or "
                "pixel format of the input video changed:\n"
                "old: width = %d, height = %d, format = %s\n"
                "new: width = %d, height = %d, format = %s\n",
                dec->width, dec->height, av_get_pix_fmt_name(dec->pix_fmt),
                frame->width, frame->height,
                av_get_pix_fmt_name(frame->format));
        return -1;
    }

    /* copy decoded frame to destination buffer:
     * this is required since rawvideo expects non aligned data */
    av_image_copy(video_dst_data, video_dst_linesize,
                  (const uint8_t **)(frame->data), frame->linesize,
                  dec->pix_fmt, dec->width, dec->height);

    /* write to rawvideo file */
    fwrite(video_dst_data[0], 1, video_dst_bufsize, video_dst_file);
    return 0;
}

static int output_audio_frame(AVCodecContext *dec,AVFrame *frame)
{
    int i, ch;
    int ret, data_size;
    size_t unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(frame->format);
    printf("audio_frame n:%d nb_samples:%d pts:%s\n",
           audio_frame_count++, frame->nb_samples,
           av_ts2timestr(frame->pts, &dec->time_base));

     data_size = av_get_bytes_per_sample(dec->sample_fmt);
     if (data_size < 0) {
        /* This should not occur, checking just for paranoia */
        fprintf(stderr, "Failed to calculate data size\n");
        return -1;
        }
    for (i = 0; i < frame->nb_samples; i++)
        for (ch = 0; ch < dec->channels; ch++)
            fwrite(frame->data[ch] + data_size*i, 1, data_size, audio_dst_file);

    return 0;
}

static int decode_packet(AVCodecContext *dec, const AVPacket *pkt,AVFrame *frames)
{
    int ret = 0;

    // submit the packet to the decoder
    ret = avcodec_send_packet(dec, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error submitting a packet for decoding (%s)\n", av_err2str(ret));
        return ret;
    }

    // get all the available frames from the decoder
    while (ret >= 0) {
        ret = avcodec_receive_frame(dec, frames);
        if (ret < 0) {
            // those two return values are special and mean there is no output
            // frame available, but there were no errors during decoding
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                return 0;

            fprintf(stderr, "Error during decoding (%s)\n", av_err2str(ret));
            return ret;
        }

        // write the frame data to output file
        if (dec->codec->type == AVMEDIA_TYPE_VIDEO)
            ret = output_video_frame(dec,frames);
        else
            ret = output_audio_frame(dec,frames);

        av_frame_unref(frames);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int open_input_file(const char *filename,AVFormatContext **ifmt)
{
    int ret;
    AVCodec *dec;
 
    if ((ret = avformat_open_input(ifmt, filename, NULL, NULL)) < 0) {
        printf( "Cannot open input file\n");
        return ret;
    }
 
    if ((ret = avformat_find_stream_info(*ifmt, NULL)) < 0) {
        printf( "Cannot find stream information\n");
        return ret;
    }
//    av_dump_format(ifmt, 0, filename, 0);
}

static int open_codec_context(int *stream_idx,
                              AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;
    AVStream *st;
    const AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input file\n",
                av_get_media_type_string(type));
        return ret;
    } else {
        stream_index = ret;
        st = fmt_ctx->streams[stream_index];

        /* find decoder for the stream */
        dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            fprintf(stderr, "Failed to find %s codec\n",
                    av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* Allocate a codec context for the decoder */
        *dec_ctx = avcodec_alloc_context3(dec);
        if (!*dec_ctx) {
            fprintf(stderr, "Failed to allocate the %s codec context\n",
                    av_get_media_type_string(type));
            return AVERROR(ENOMEM);
        }

        /* Copy codec parameters from input stream to output codec context */
        if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
            fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                    av_get_media_type_string(type));
            return ret;
        }

        /* Init the decoders */
        if ((ret = avcodec_open2(*dec_ctx, dec, &opts)) < 0) {
            fprintf(stderr, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
        *stream_idx = stream_index;
    }

    return 0;
}

int CBX_demux_codec(const char *src,const char *des_video,const char *des_audio)

{
    AVFormatContext *ifmt_ctx = NULL;
    int ret = 0;
    const char *infile,*out_videofile,*out_audiofile;
    int stream_v_idx,stream_a_idx;
    enum AVSampleFormat sfmt;
    int n_channels = 0;
    const char *fmt;

    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;

    infile        = src;
    out_videofile = des_video;
    out_audiofile = des_audio;

    //读取输入文件信息
    open_input_file(infile,&ifmt_ctx);

    //获取编码器信息
    open_codec_context(&stream_v_idx,&pCodecCtx_v,ifmt_ctx,AVMEDIA_TYPE_VIDEO);
    open_codec_context(&stream_a_idx,&pCodecCtx_a,ifmt_ctx,AVMEDIA_TYPE_AUDIO);

    //创建存放解码前后数据的缓冲区
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "Could not allocate the pkt data\n");
        exit(1);
    }
    //存放输出数据的缓冲区，用于重置格式
    ret = av_image_alloc(video_dst_data, video_dst_linesize,
                         pCodecCtx_v->width, pCodecCtx_v->height, pCodecCtx_v->pix_fmt, 1);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate raw video buffer\n");
        goto end;
    }
    video_dst_bufsize = ret;

    //打开输出文件
    audio_dst_file = fopen(out_audiofile, "wb+");
    if (!audio_dst_file) {
        fprintf(stderr, "Could not open %s\n", out_audiofile);
        exit(1);
    }
    video_dst_file = fopen(out_videofile, "wb+");
    if (!video_dst_file) {
        fprintf(stderr, "Could not open %s\n", out_videofile);
        exit(1);
    }
    //开始读取数据解码
    while(1)
    {
        ret = av_read_frame(ifmt_ctx,pkt);
        if(ret < 0)
        {
            printf("read frame finished...\n");
            break;
        }
        if(stream_v_idx == pkt->stream_index)
        {
            decode_packet(pCodecCtx_v,pkt,frame);
        }
        else if(stream_a_idx == pkt->stream_index)
        {
            decode_packet(pCodecCtx_a,pkt,frame);
        }
    }

    decode_packet(pCodecCtx_v,NULL,frame);
    decode_packet(pCodecCtx_a,NULL,frame);

    printf("Demuxing succeeded.\n");

    /* print output pcm infomations, because there have no metadata of pcm */
    sfmt = pCodecCtx_a->sample_fmt;

    if (av_sample_fmt_is_planar(sfmt)) {
        const char *packed = av_get_sample_fmt_name(sfmt);
        printf("Warning: the sample format the decoder produced is planar "
               "(%s). This example will output the first channel only.\n",
               packed ? packed : "?");
        sfmt = av_get_packed_sample_fmt(sfmt);
    }

    n_channels = pCodecCtx_a->channels;
    if ((ret = get_format_from_sample_fmt(&fmt, sfmt)) < 0)
        goto end;

    printf("Play the output audio file with the command:\n"
           "ffplay -f %s -ac %d -ar %d %s\n",
           fmt, n_channels, pCodecCtx_a->sample_rate,
           out_audiofile);
    
    printf("Play the output video file with the command:\n"
           "ffplay -f rawvideo -pix_fmt %s -video_size %dx%d %s\n",
           av_get_pix_fmt_name(pCodecCtx_v->pix_fmt), pCodecCtx_v->width, pCodecCtx_v->height,
           out_videofile);


end:
    avcodec_free_context(&pCodecCtx_v);
    avcodec_free_context(&pCodecCtx_a);
    avformat_close_input(&ifmt_ctx);
    if (video_dst_file)
        fclose(video_dst_file);
    if (audio_dst_file)
        fclose(audio_dst_file);
    av_packet_free(&pkt);
    av_frame_free(&frame);
    av_free(video_dst_data[0]);

    return 0;
}


