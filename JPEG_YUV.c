#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavformat/swscale_internal.h>



#define INBUF_SIZE 5841227  //最大支持5.4M的图片
//FILE *f,*q;
static FILE *video_dst_file = NULL;
static FILE *video_src_file = NULL;

static uint8_t *video_dst_data[4] = {NULL};
static int      video_dst_linesize[4];
static int video_dst_bufsize;
static enum AVPixelFormat pix_fmt;
static int width, height;

#define USE_RGB //原始数据是否为RGB
static void yuv_2_rgb(AVFrame *frame, FILE *dst)
{
    SwsContext *ctx= NULL;
    AVFrame *RGB_frame;
    int ret;
    RGB_frame = av_frame_alloc();
    if (!RGB_frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    RGB_frame->format = AV_PIX_FMT_RGB24;
	RGB_frame->width = width;
	RGB_frame->height = height;
    
    ret = av_frame_get_buffer(RGB_frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate audio data buffers\n");
        exit(1);
    }    
	ctx = sws_getCachedContext(ctx,
		width,height,AV_PIX_FMT_YUV444P,
		width,height,AV_PIX_FMT_RGB24,NULL,
		NULL,NULL,NULL);

    int h = sws_scale(ctx,frame->data,frame->linesize,0,frame->height,RGB_frame->data,RGB_frame->linesize);
    if (h<=0)
    {
        exit(1);
    }

    /* write to rawvideo file */
    fwrite(RGB_frame->data[0], 1, video_dst_bufsize, dst);

}

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
        
        printf(">>>>>====== pix_fmt = %d   frame->format = %d\n",pix_fmt,frame->format);
        printf(">>>====  Width, height and pixel format have to be "
                        "constant in a rawvideo file, but the width, height or "
                        "pixel format of the input video changed:\n"
                        "old: width = %d, height = %d, format = %s\n"
                        "new: width = %d, height = %d, format = %s\n",
                        width, height, "1231dsa",
                        frame->width, frame->height,
                        "asdasdfas");
#ifdef USE_RGB
        //如果是RGB格式，调用如下接口
        yuv_2_rgb(frame,video_dst_file);

#else
        //如果原始格式为yuv，将解码帧复制到目标缓冲区后直接写入

        /* copy decoded frame to destination buffer:
         * this is required since rawvideo expects non aligned data */
        av_image_copy(video_dst_data, video_dst_linesize,
                      (const uint8_t **)(frame->data), frame->linesize,
                      pix_fmt, width, height);

        /* write to rawvideo file */
        fwrite(video_dst_data[0], 1, video_dst_bufsize, video_dst_file);

#endif
    }
}

int main(int argc, char **argv)
{
    const char *filename, *outfilename;
    const AVCodec *codec;
    AVCodecContext *c= NULL;
    AVOutputFormat* fmt;

    AVFrame *frame;
    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    uint8_t *data;
    int   data_size;
    int ret;
    AVPacket *pkt;
    struct stat sbuf;

    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <input file(JPEG)> <output file(YUV)>\n"
                "And check your input file is encoded by mpeg1video please.\n", argv[0]);
        exit(0);
    }
    filename    = argv[1];
    outfilename = argv[2];



    /* set end of buffer to 0 (this ensures that no overreading happens for damaged MPEG streams) */
    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    fmt = av_guess_format(NULL, filename, NULL); 

    /* find the MJPEG video decoder */
    codec = avcodec_find_decoder(fmt->video_codec);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }


    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }


    //这两个属性可以不填充，后面会被覆盖
    c->codec_type = AVMEDIA_TYPE_VIDEO;
    c->pix_fmt = AV_PIX_FMT_YUVJ444P;
    //这两个属性必须填写
    c->width = 3936;  
    c->height = 2624;
    
    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }
    width = c->width;
    height = c->height;
    pix_fmt = c->pix_fmt; 

    ret = av_image_alloc(video_dst_data, video_dst_linesize,
                         width, height, pix_fmt, 1);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate raw video buffer\n");
        exit(1);
    }
    video_dst_bufsize = ret;

//    video_dst_bufsize = av_image_get_buffer_size(pix_fmt, width, height, 1);;

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
        exit(1);

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    /* read raw data from the input file */
    data_size = fread(inbuf, 1, INBUF_SIZE, video_src_file);
    if (!data_size)
    {
        fprintf(stderr, "Could not read video frame\n");
        fclose(video_src_file);
        fclose(video_dst_file);
        exit(1);
    }

    /* use the parser to split the data into frames */
    data = inbuf;

    pkt->data = data;
    pkt->size = data_size;
    printf(">>>=====55555 pkt->size %3d   data_size = %d\n",pkt->size,data_size);

    if (pkt->size)
        decode(c, frame, pkt, outfilename);



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

    return 0;
}
