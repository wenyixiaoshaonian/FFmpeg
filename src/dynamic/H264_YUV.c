#include "stdio.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
//#include <libswscale/swscale_internal.h>



#define INBUF_SIZE 204800  //每次读200k
static FILE *video_dst_file = NULL;
static FILE *video_src_file = NULL;

static uint8_t *video_dst_data[4] = {NULL};
static int video_dst_linesize[4];
static int video_dst_bufsize;




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
                        "old: width = %d, height = %d, format = %d\n"
                        "new: width = %d, height = %d, format = %d\n",
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
//不使用format
static int open_input_file(const char *file,AVOutputFormat* fmt_x, AVCodec *codc,AVCodecContext *codec_ctx)
{


	codec_ctx->codec_id = fmt_x->video_codec;
	codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
	codec_ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    codec_ctx->bit_rate = 400000;   // 码率
    codec_ctx->gop_size=250;        // 每250帧产生一个关键帧
	codec_ctx->width = 960;  
	codec_ctx->height = 544;
    /* frames per second 帧率*/
    codec_ctx->time_base = (AVRational){1, 30};
    codec_ctx->framerate = (AVRational){30, 1}; 
          
    codec_ctx->qmin = 10;
    codec_ctx->qmax = 51;       //量化因子，越大编码的质量越
 
    //Optional Param
    codec_ctx->max_b_frames=3;      //默认值为3   ，最多x个连续P帧可以被替换为B帧,增加压缩率
 
    // Set Option
    AVDictionary *param = 0;
    //H.264
    if(codec_ctx->codec_id == AV_CODEC_ID_H264) {
        av_dict_set(&param, "preset", "slow", 0);      //压缩速度慢，保证视频质量
        av_dict_set(&param, "tune", "zerolatency", 0); //零延迟
        //av_dict_set(¶m, "profile", "main", 0);
    }

    /* open it */
    if (avcodec_open2(codec_ctx, codc, &param) < 0) {
        fprintf(stderr, "Could not open codec\n");
        return -1;
    }
    return 0;

}

int CBX_H264toYUV(const char *src,const char *des)
{
    const char *filename, *outfilename;
    AVCodec *codec;
    AVCodecContext *c= NULL;
    AVOutputFormat* fmt;
    enum AVPixelFormat pix_fmt;
    int width, height;

    AVFrame *frame;
    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    uint8_t *data;
    int   data_size;
    int ret;
    AVPacket *pkt;
    AVCodecParserContext *parser = NULL;
    
    filename    = src;
    outfilename = des;

    fmt = av_guess_format(NULL, filename, NULL); 
    
    codec = avcodec_find_decoder(fmt->video_codec);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        return -1;
    }
    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        return -1;
    }
    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    //打开输入文件，填充解码器等部件
    ret = open_input_file(filename,fmt, codec,c);
    if(ret < 0)
    {
        fprintf(stderr, "open_input_file error\n");
        return -1;
    }
    
    parser = av_parser_init(codec->id);
    if (!parser) {
        fprintf(stderr, "Parser not found\n");
        return -1;
    }

    width = c->width;
    height = c->height;
    pix_fmt = c->pix_fmt; 

    ret = av_image_alloc(video_dst_data, video_dst_linesize,
                         width, height, pix_fmt, 1);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate raw video buffer\n");
        return -1;
    }
    video_dst_bufsize = ret;

    video_src_file = fopen(filename, "rb");
    if (!video_src_file) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    video_dst_file = fopen(outfilename, "wb+");
    if (!video_dst_file) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    pkt = av_packet_alloc();
    if (!pkt)
        return -1;

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        return -1;
    }
    
    //开始解析数据
    while (!feof(video_src_file)) {
        /* read raw data from the input file */
        data_size = fread(inbuf, 1, INBUF_SIZE, video_src_file);
        if (!data_size)
        {
            fprintf(stderr, "Could not read video frame\n");
            fclose(video_src_file);
            fclose(video_dst_file);
            return -1;
        }
        printf(">>>=====55555 pkt->size %3d   data_size = %d\n",pkt->size,data_size);

        data = inbuf;
        while(data_size > 0) {
            ret = av_parser_parse2(parser, c, &pkt->data, &pkt->size,
                                   data, data_size,
                                   AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (ret < 0) {
                fprintf(stderr, "Error while parsing\n");
                return -1;
            }
            data      += ret;
            data_size -= ret;

            if (pkt->size)
                decode(c, frame, pkt, outfilename);
        }
    }

    /* flush the decoder */
    decode(c, frame, NULL, outfilename);

    if (video_src_file)
        fclose(video_src_file);
    if (video_dst_file)
        fclose(video_dst_file);

    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    av_free(video_dst_data[0]);
    av_parser_close(parser);

    return 0;
}
