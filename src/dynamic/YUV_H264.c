#include "stdio.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/internal.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
//#include <libavformat/swscale_internal.h>


static int encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt)
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
   ret = avcodec_receive_packet(enc_ctx, pkt);
   if(ret < 0) {
       fprintf(stderr, "Error avcodec_receive_packet\n");
       return -1;     
   }
   else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
       return 0;

   return 0;
}
static int alloc_codec_contxt(AVStream* st_video,AVOutputFormat* fmt_x, AVCodec *codc,AVCodecContext *codec_ctx,int w,int h)
{
    int ret;
    
    codec_ctx = st_video->internal->avctx;
    codec_ctx->codec_id = fmt_x->video_codec;
    codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    codec_ctx->bit_rate = 24656000;   // 码率
    codec_ctx->gop_size=250;        // 每250帧产生一个关键帧
    codec_ctx->width = w;  
    codec_ctx->height = h;
    /* frames per second 帧率*/
    codec_ctx->time_base = (AVRational){1, 30};
    codec_ctx->framerate = (AVRational){30, 1}; 
          
    //H264
    codec_ctx->qmax = 3;       //量化因子，越大编码的质量越差
    codec_ctx->qcompress =1;
    //Optional Param
    codec_ctx->max_b_frames=0;      //默认值为3   ，最多x个连续P帧可以被替换为B帧,增加压缩率  ,设置为0，表示不需要B帧
 
    // Set Option
    AVDictionary *param = 0;
    //H.264
    if(codec_ctx->codec_id == AV_CODEC_ID_H264) {
        av_dict_set(&param, "preset", "slow", 0);      //压缩速度慢，保证视频质量
        av_dict_set(&param, "tune", "zerolatency", 0); //零延迟
        //av_dict_set(¶m, "profile", "main", 0);
    }  

    ret = avcodec_parameters_from_context(st_video->codecpar, codec_ctx);
    if (ret < 0) {
        fprintf(stderr, "Could not initialize stream parameters\n");
        return -1;
    }

    //Output some information
//  av_dump_format(pFormatCtx, 0, out_file, 1);
    codc = avcodec_find_encoder(codec_ctx->codec_id);     
    if (!codc){
        printf("Codec not found.");
        return -1;
    }
    
    if (avcodec_open2(codec_ctx, codc,&param) < 0){       
        printf("Could not open codec.");
        return -1;
    }

}
int CBX_yuvtoH264(const char *src,const char *des)
{
    const char *filename, *outfilename;
	AVFormatContext* pFormatCtx;
	AVOutputFormat* fmt = NULL;
	AVStream* video_st;
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

#if 0
	//Method 1
	pFormatCtx = avformat_alloc_context();     
	//Guess format
	fmt = av_guess_format(NULL, outfilename, NULL); 
	pFormatCtx->oformat = fmt;
    printf(">>>==fmt->video_codec = %d \n",fmt->video_codec);
#endif
	//Method 2. More simple
	avformat_alloc_output_context2(&pFormatCtx, fmt, NULL, out_file);
//	fmt = pFormatCtx->oformat;

    //Output URL
	if (avio_open(&pFormatCtx->pb,outfilename, AVIO_FLAG_READ_WRITE) < 0){    
		printf("Couldn't open output file.");
		return -1;
	}
    pFormatCtx->url = av_strdup(outfilename);
    
	video_st = avformat_new_stream(pFormatCtx, 0);   
	if (video_st==NULL){
		return -1;
	}
    //创建一个AVCodecContext并填充数据流中的对应信息
    alloc_codec_contxt(video_st,fmt,pCodec,pCodecCtx,in_w,in_h);

    //存放图片原始数据 
	picture = av_frame_alloc();     //创建存放编码前(原始数据)的结构体
	size = av_image_get_buffer_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height); 
	picture_buf = (uint8_t *)av_malloc(size);   
	if (!picture_buf)
	{
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
    pkt = av_packet_alloc();
    if (!pkt)
        return -1;

    y_size = pCodecCtx->width * pCodecCtx->height;
	avformat_write_header(pFormatCtx,NULL);
    int i = 0;
    //开始读取数据
    while (!feof(in_file)) {
    	//Read YUV
        if (fread(picture_buf, 1, y_size * 3/2, in_file) <=0)          
        {
            printf("Could not read input file.\n");
            return -1;
        } 
        //RGB的数据都存放再data[0]内
        picture->data[0] = picture_buf;
        picture->data[1] = picture_buf+ y_size;     
        picture->data[2] = picture_buf+ y_size*5/4; 

        picture->pts = i * (video_st->time_base.den)/((video_st->time_base.num)*30);
        i++;

    	//Encode
        encode(pCodecCtx, picture, pkt);

    	pkt->stream_index = video_st->index;
        printf(">>>>>>>>>>>===== pkt->size = %d",pkt->size);
    	ret = av_write_frame(pFormatCtx, pkt);
        if (ret < 0) {
            fprintf(stderr, "Could not av_write_frame\n");
            return -1;
        }     
        av_packet_unref(pkt);   
    }

    encode(pCodecCtx, NULL, pkt);
	ret = av_write_frame(pFormatCtx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Could not av_write_frame\n");
        return -1;
    }     
    av_packet_unref(pkt);  
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
    av_packet_free(&pkt);

    fclose(in_file);
    fclose(out_file);

	return 0;
}
