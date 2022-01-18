#include "stdio.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/internal.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavformat/swscale_internal.h>

static int encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt,
                   FILE *outfile)
{
    int ret;

    /* send the frame to the encoder */
    if (frame)
        printf("Send frame %3"PRId64"\n", frame->pts);

    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        return -1;
    }
    printf(">>>===333\n");

    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            return -1;
        }

        printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);
        fwrite(pkt->data, 1, pkt->size, outfile);
        av_packet_unref(pkt);
    }
    return 0;
}

static int open_input_codec_context(const char *file,
                             AVCodecContext *dec_ctx, AVOutputFormat* fmt_x, AVCodec* Codec,int w,int h)
{
    fmt_x = av_guess_format(NULL, file, NULL); 
    
    Codec = avcodec_find_decoder(fmt_x->video_codec);
    if (!Codec) {
        fprintf(stderr, "Codec not found\n");
        return -1;
    }
   
    dec_ctx = avcodec_alloc_context3(Codec);

	dec_ctx->codec_id = fmt_x->video_codec;
	dec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
	dec_ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    dec_ctx->bit_rate = 565960;   // 码率
    dec_ctx->gop_size=250;        // 每250帧产生一个关键帧
	dec_ctx->width = w;  
	dec_ctx->height = h;
    /* frames per second 帧率*/
    dec_ctx->time_base = (AVRational){1, 30};
    dec_ctx->framerate = (AVRational){30, 1}; 
      
    dec_ctx->qmax = 3;       //量化因子，越大编码的质量越
    dec_ctx->qcompress =1;
    //Optional Param
    dec_ctx->max_b_frames=0;      //默认值为3   ，最多x个连续P帧可以被替换为B帧,增加压缩率  ,设置为0，表示不需要B帧

    // Set Option
    AVDictionary *param = 0;
    //H.264
    if(dec_ctx->codec_id == AV_CODEC_ID_H264) {
        av_dict_set(&param, "preset", "slow", 0);      //压缩速度慢，保证视频质量
        av_dict_set(&param, "tune", "zerolatency", 0); //零延迟
        //av_dict_set(¶m, "profile", "main", 0);
    }
    /* open it */
    if (avcodec_open2(dec_ctx, Codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        return -1;
    }
    return 0;
}

int CBX_YUVto264(const char *src,const char *des)
{
    const char *filename, *outfilename;
	AVOutputFormat* fmt;
	AVCodec* pCodec;
    AVFrame* picture;
    AVPacket* pkt;
    AVCodecContext* pCodecCtx; 
	uint8_t* picture_buf;
    int y_size;

	int size;
	int ret=0;
 
	FILE *in_file = NULL;                            //YUV source
	FILE *out_file = NULL;   
	int in_w=960,in_h=544;                           //YUV's width and height


    filename    = src;
    outfilename = des;

	in_file =  fopen(filename, "rb");
    out_file = fopen(outfilename, "wb+");

    ret = open_input_codec_context(outfilename,pCodecCtx,fmt,pCodec,in_w,in_h);
    if(ret > 0)
    {
        fprintf(stderr, "open_input_codec_context error \n");
        return -1;
    }

    //存放图片原始数据 
	picture = av_frame_alloc();     //创建存放编码前(原始数据)的结构体
	if (!picture_buf)
	{
        fprintf(stderr, "av_frame_alloc error \n");
		return -1;
	}
    picture->format = AV_PIX_FMT_YUVJ420P;
    picture->width  = pCodecCtx->width;
    picture->height = pCodecCtx->height;

    ret = av_frame_get_buffer(picture, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate the video frame data\n");
        return -1;
    }
  
    //创建从原始文件中读取数据的缓冲区
    size = av_image_get_buffer_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);   
	picture_buf = (uint8_t *)av_malloc(size);   

    //创建存放编码后的结构体
    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "av_packet_alloc error\n");
        return -1;
    }
    int i = 0;
    y_size = pCodecCtx->width * pCodecCtx->height;

    //开始读取数据
    while (!feof(in_file)) {
    	//Read YUV
        if (fread(picture_buf, 1, y_size * 3/2, in_file) <=0)          
        {
            printf("Could not read input file.\n");
            return -1;
        } 
        picture->data[0] = picture_buf;
        picture->data[1] = picture_buf+ y_size;     
        picture->data[2] = picture_buf+ y_size*5/4; 

        picture->pts = i * (pCodecCtx->time_base.den)/((pCodecCtx->time_base.num)*30);
        i++;

    	//Encode
        encode(pCodecCtx, picture, pkt,out_file);
        printf(">>>>>>>>>>>===== pkt->size = %d",pkt->size);

    }    
    encode(pCodecCtx, NULL, pkt, out_file);

    av_packet_free(pkt);
 
    printf("Encode Successful.\n");
 
    avcodec_free_context(&pCodecCtx);
    av_packet_free(&pkt);
    av_free(picture);
    av_free(picture_buf);

    fclose(in_file);
    fclose(out_file);
    return 0;
    }

