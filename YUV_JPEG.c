#include <stdio.h>
 
#define __STDC_CONSTANT_MACROS
 
#ifdef _WIN32
//Windows
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
};
#else
//Linux...
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavcodec/avcodec.h>

#include <libavformat/avformat.h>


#ifdef __cplusplus
};
#endif
#endif

#include <libavformat/internal.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavformat/swscale_internal.h>

 static int width, height;

 static void rgb_2_yuv(AVFrame *src_frame, AVFrame *dst_frame)
 {
     SwsContext *ctx= NULL;   
     int ret;
     
     ctx = sws_getCachedContext(ctx,
         width,height,AV_PIX_FMT_RGB24,
         width,height,AV_PIX_FMT_YUVJ444P,NULL,
         NULL,NULL,NULL);
 
     ret = sws_scale(ctx,src_frame->data,src_frame->linesize,0,src_frame->
height,dst_frame->data,dst_frame->linesize);
     if (ret<=0)
     {
        printf(stderr, "Error rgb_2_yuv\n");
         exit(1);
     }
 
 //    /* write to rawvideo file */
 //    fwrite(yuv_frame->data[0], 1, video_dst_bufsize, dst);
 
 }

 static void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt, 
FILE *dst_file)
 {
     int ret;
 
     /* send the frame to the encoder */
     if (frame)
         printf("Send frame %3"PRId64"\n", frame->pts);
 
     ret = avcodec_send_frame(enc_ctx, frame);
     if (ret < 0) {
         fprintf(stderr, "Error sending a frame for encoding\n");
         exit(1);
     }
 
     while (ret >= 0) {
         ret = avcodec_receive_packet(enc_ctx, pkt);
         if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
             return;
         else if (ret < 0) {
             fprintf(stderr, "Error during encoding\n");
             exit(1);
         }
 
         fwrite(pkt->data, 1, pkt->size, dst_file);
         av_packet_unref(pkt);
     }
 }

int main(int argc, char* argv[])
{
    const char *filename, *outfilename;
	AVFormatContext* pFormatCtx;
	AVOutputFormat* fmt;
	AVStream* video_st;
	AVCodec* pCodec;
    AVFrame *yuv_frame;
    AVFrame* picture;
    AVPacket* pkt;
    AVCodecContext* pCodecCtx;

	uint8_t* picture_buf;

	int y_size;
	int got_picture=0;
	int size;
	int ret=0;
 
	FILE *in_file = NULL;                            //YUV source
    FILE *out_file = NULL;   
	int in_w=3936,in_h=2624;                           //YUV's width and height
    int video_dst_bufsize;

    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        exit(0);
    }
    filename    = argv[1];
    outfilename = argv[2];

	in_file =  fopen(filename, "rb");
    out_file = fopen(outfilename, "wb+");

	//1.Guess format
	fmt = av_guess_format(NULL, outfilename, NULL); 
    //2.find encoder
	pCodec = avcodec_find_encoder(fmt->video_codec);     
	if (!pCodec){
		printf("Codec not found.");
		return -1;
	}
    //3.init AVCodecContext
    pCodecCtx = avcodec_alloc_context3(pCodec);

	pCodecCtx->codec_id = fmt->video_codec;
	pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	pCodecCtx->pix_fmt = AV_PIX_FMT_YUVJ444P;
	pCodecCtx->width = in_w;  
	pCodecCtx->height = in_h;
    /* frames per second 帧率*/
    pCodecCtx->time_base = (AVRational){1, 25};
    pCodecCtx->framerate = (AVRational){25, 1};

    width = in_w;
    height = in_h;
	if (avcodec_open2(pCodecCtx, pCodec,NULL) < 0){       
		printf("Could not open codec.");
		return -1;
	}

    //4.init buffer
    pkt = av_packet_alloc();
    if (!pkt)
        exit(1);
    //创建存放原始图片数据的结构体
	picture = av_frame_alloc();
	size = av_image_get_buffer_size(pCodecCtx->pix_fmt, pCodecCtx->width, 
pCodecCtx->height); 
	picture_buf = (uint8_t *)av_malloc(size);   
	if (!picture_buf)
	{
		return -1;
	}
    picture->format = AV_PIX_FMT_RGB24;
    picture->width  = pCodecCtx->width;
    picture->height = pCodecCtx->height;
    ret = av_frame_get_buffer(picture, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate the video frame data\n");
        exit(1);
    }
    
    //存放转换之后的YUV数据    
    yuv_frame = av_frame_alloc();
    if (!yuv_frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    yuv_frame->format = AV_PIX_FMT_YUVJ444P;
    yuv_frame->width = pCodecCtx->width;
    yuv_frame->height = pCodecCtx->height;
    ret = av_frame_get_buffer(yuv_frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate audio data buffers\n");
        exit(1);
    }  
    
//    video_dst_bufsize = av_image_get_buffer_size(picture->format, picture->
//width, picture->height, 1);
	y_size = pCodecCtx->width * pCodecCtx->height;
	printf(">>>>====1111 video_dst_bufsize = %d \n",size);      
	//YUV420，水平垂直方向采样都会减半    
	//http://blog.sina.com.cn/s/blog_68eac14e0102vva0.html
	if (pCodecCtx->pix_fmt == AV_PIX_FMT_YUVJ420P) {        
        if (fread(picture_buf, 1, size, in_file) <=0)  
            {
                printf("Could not read input file.");
                return -1;
            }
        picture->data[0] = picture_buf;
        picture->data[1] = picture_buf+ y_size;     
        picture->data[2] = picture_buf+ y_size*5/4; 

	}
	else if (pCodecCtx->pix_fmt == AV_PIX_FMT_YUVJ444P) {   
    	if (fread(picture_buf, 1, size, in_file) <=0)          
    	{
    		printf("Could not read input file.");
    		return -1;
    	}
        printf(">>>>====1111 size = %d \n",size);
        //RGB的数据都存放再data[0]内
    	picture->data[0] = picture_buf;
 //   	picture->data[1] = picture_buf+ y_size;
 //   	picture->data[2] = picture_buf+ y_size*2;
	}
    rgb_2_yuv(picture,yuv_frame);  


	//Encode
    encode(pCodecCtx, yuv_frame, pkt,out_file);

	printf("Encode Successful.\n");
    
	av_free(picture_buf);
    av_frame_free(&picture);
    av_packet_free(&pkt); 
#if 0     
    av_packet_free(&yuv_frame); 
    avcodec_free_context(&pCodecCtx);
	fclose(in_file);
    fclose(out_file);
#endif
	return 0;
}
