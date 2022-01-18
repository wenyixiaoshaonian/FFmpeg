#include "stdio.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale_internal.h>

#define INBUF_SIZE 204800  //每次读200k

//最后进行文件读写的相关变量
static FILE *video_dst_file = NULL;
static int video_dst_linesize[4];
static int video_dst_bufsize;
static uint8_t *video_dst_data[4] = {NULL};


static void decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt,
                   const char *filename)
{
    char buf[1024];
    int ret;
    char *ccc;
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error sending a packet for decoding\n");
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }

        printf("saving frame %3d\n", dec_ctx->frame_number);
        fflush(stdout);
        
        printf(">>>====  Width, height and pixel format have to be "
                        "constant in a rawvideo file, but the width, height or "
                        "pixel format of the input video changed:\n"
                        "old: width = %d, height = %d, format = %s\n"
                        "new: width = %d, height = %d, format = %s\n",
                        dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                        frame->width, frame->height,
                        dec_ctx->pix_fmt);


        //如果原始格式为yuv，将解码帧复制到目标缓冲区后直接写入

        /* copy decoded frame to destination buffer:
         * this is required since rawvideo expects non aligned data */
        av_image_copy(video_dst_data, video_dst_linesize,
                      (const uint8_t **)(frame->data), frame->linesize,
                      dec_ctx->pix_fmt, dec_ctx->width, dec_ctx->height);

        /* write to rawvideo file */
        fwrite(video_dst_data[0], 1, video_dst_bufsize, video_dst_file);

    }
}



static int open_input_file(const char *filename, AVFormatContext *fmt_ctx)
{
    int ret;

    
    if ((ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL)) < 0) {
        printf( "Cannot open input file\n");
        return ret;
    }
 
    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        printf( "Cannot find stream information\n");
        return ret;
    }
    return 0;
}
static int open_codec_context(int *stream_idx,
                              AVCodecContext *dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;
    AVCodec *dec;
    AVStream *st;

    /* select the video stream */
    ret = av_find_best_stream(fmt_ctx, type, -1, -1, &dec, 0);
    if (ret < 0) {
        printf( "Cannot find a video stream in the input file\n");
        return ret;
    }
    stream_index = ret;
    st = fmt_ctx->streams[stream_index];

    dec = avcodec_find_decoder(st->codecpar->codec_id);

    dec_ctx = avcodec_alloc_context3(dec);

    /* Copy codec parameters from input stream to output codec context */
    if ((ret = avcodec_parameters_to_context(dec_ctx, st->codecpar)) < 0) {
        printf( "Failed to copy codec parameters to decoder context\n");
        return ret;
    }

    /* init the video decoder */
    if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
        printf( "Cannot open video decoder\n");
        return ret;
    }
    *stream_idx = stream_index;

    return 0;
}


int CBX_264toYUV(const char *src,const char *des)
{
    const char *filename, *outfilename;
    const AVCodec *codec;
    AVOutputFormat* fmt;

    AVFrame *frame;
    uint8_t *data;
    int   data_size;
    int ret;
    AVPacket *pkt;
    struct stat sbuf;
    int video_stream_index = -1;
    AVFormatContext *pFormatCtx;
    AVCodecContext *pCodecCtx= NULL;
    static int width, height;
    static enum AVPixelFormat pix_fmt;

    filename    = src;
    outfilename = des;

    //打开输入文件 获取流信息
    ret = open_input_file(filename);
    if(ret)
    {
        printf("open_input_file failed\n");
        exit(1);
    }
    //找到对应编码器的信息
    ret = open_codec_context(&video_stream_index, pCodecCtx, pFormatCtx, AVMEDIA_TYPE_VIDEO);
    if(ret < 0)
    {
        exit(1);

    }
    video_dst_file = fopen(outfilename, "wb+");
    if (!video_dst_file) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    pkt = av_packet_alloc();
    if (!pkt)
        exit(1);

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    /* allocate image where the decoded image will be put */
    width = pCodecCtx->width;
    height = pCodecCtx->height;
    pix_fmt = pCodecCtx->pix_fmt;
    ret = av_image_alloc(video_dst_data, video_dst_linesize,
                         width, height, pix_fmt, 1);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate raw video buffer\n");
        exit(1);
    }
    video_dst_bufsize = ret;

    /* read frames from the file */
    while (av_read_frame(pFormatCtx, pkt) >= 0) {
        // check if the packet belongs to a stream we are interested in, otherwise
        // skip it
        if (pkt->stream_index == video_stream_idx)
            ret = decode(pCodecCtx, frame, pkt, outfilename);

        av_packet_unref(pkt);
        if (ret < 0)
            break;
    }

    /* flush the decoder */
    decode(pCodecCtx, frame, NULL, outfilename);

    if (video_dst_file)
        fclose(video_dst_file);

    avcodec_free_context(&pCodecCtx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    av_free(video_dst_data[0]);

    return 0;
}
