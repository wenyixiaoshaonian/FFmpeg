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

 //      fwrite(pkt->data, 1, pkt->size, dst_file);
 //      av_packet_unref(pkt);
   }
}


int main(int argc, char* argv[])
{
    const char *filename, *outfilename;
	AVFormatContext* pFormatCtx;
	AVOutputFormat* fmt;
	AVStream* video_st;
	AVCodec* pCodec;
    AVFrame* picture;
    AVFrame* yuv_frame;
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
  
    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        exit(0);
    }
    filename    = argv[1];
    outfilename = argv[2];

	in_file =  fopen(filename, "rb");
    out_file = fopen(outfilename, "wb+");

 
	//Method 1
	pFormatCtx = avformat_alloc_context();     
	//Guess format
	fmt = av_guess_format("mjpeg", NULL, NULL); 
	pFormatCtx->oformat = fmt;
    //Output URL
	if (avio_open(&pFormatCtx->pb,outfilename, AVIO_FLAG_READ_WRITE) < 0){    
		printf("Couldn't open output file.");
		return -1;
	}
    pFormatCtx->url = av_strdup(outfilename);
	//Method 2. More simple
	//avformat_alloc_output_context2(&pFormatCtx, NULL, NULL, out_file);
	//fmt = pFormatCtx->oformat;
 
	video_st = avformat_new_stream(pFormatCtx, 0);   
	if (video_st==NULL){
		return -1;
	}
	pCodecCtx = video_st->internal->avctx;
	pCodecCtx->codec_id = fmt->video_codec;
	pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	pCodecCtx->pix_fmt = AV_PIX_FMT_YUVJ422P;
 
	pCodecCtx->width = in_w;  
	pCodecCtx->height = in_h;
    /* frames per second 帧率*/
    pCodecCtx->time_base = (AVRational){1, 25};
    pCodecCtx->framerate = (AVRational){25, 1}; 


    ret = avcodec_parameters_from_context(video_st->codecpar, pCodecCtx);
    if (ret < 0) {
        fprintf(stderr, "Could not initialize stream parameters\n");
        return -1;
    }

	//Output some information
//	av_dump_format(pFormatCtx, 0, out_file, 1);
    width = in_w;
    height = in_h; 
	pCodec = avcodec_find_encoder(pCodecCtx->codec_id);     
	if (!pCodec){
		printf("Codec not found.");
		return -1;
	}
    //原本这里还有个流程：avcodec_alloc_context3，但AVCodecContext在avformat_new_stream已经初始化好了，并且上面也都初始化好了
    
	if (avcodec_open2(pCodecCtx, pCodec,NULL) < 0){       
		printf("Could not open codec.");
		return -1;
	}
    //存放图片原始数据 
	picture = av_frame_alloc();     //创建存放编码前(原始数据)的结构体
	size = av_image_get_buffer_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height); 
	picture_buf = (uint8_t *)av_malloc(size);   
	if (!picture_buf)
	{
		return -1;
	}
    picture->format = pCodecCtx->pix_fmt;
    picture->width  = pCodecCtx->width;
    picture->height = pCodecCtx->height;
//	avpicture_fill((AVPicture *)picture, picture_buf, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);
    ret = av_frame_get_buffer(picture, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate the video frame data\n");
        exit(1);
    }
    pkt = av_packet_alloc();
    if (!pkt)
        exit(1);

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
    
	//Write Header   https://blog.csdn.net/leixiaohua1020/article/details/44116215
	avformat_write_header(pFormatCtx,NULL);   
 
	y_size = pCodecCtx->width * pCodecCtx->height;
	//Read YUV
    if (fread(picture_buf, 1, size, in_file) <=0)          
    {
        printf("Could not read input file.");
        return -1;
    } 
    printf(">>>>====1111 size = %d \n",size); 
    //RGB的数据都存放再data[0]内
    picture->data[0] = picture_buf;
//     picture->data[1] = picture_buf+ y_size;
//     picture->data[2] = picture_buf+ y_size*2;

    rgb_2_yuv(picture,yuv_frame);  

	//Encode
    encode(pCodecCtx, yuv_frame, pkt,out_file);

	pkt->stream_index = video_st->index;
 //   pkt->pts = 100;
 //   pkt->dts = 10;
	ret = av_write_frame(pFormatCtx, pkt);
 
    av_packet_free(pkt);

	//Write Trailer
	av_write_trailer(pFormatCtx);
 
	printf("Encode Successful.\n");
 
	if (video_st){
		avcodec_close(video_st->internal->avctx);
		av_free(picture);
		av_free(picture_buf);
	}
	avio_close(pFormatCtx->pb);
	avformat_free_context(pFormatCtx);
 
	fclose(in_file);
 
	return 0;
}
