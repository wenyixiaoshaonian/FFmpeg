#include <stdio.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libavutil/opt.h"
#include <libavformat/internal.h>

 static int encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt)
 {
     int ret;
 
     /* send the frame for encoding */
     ret = avcodec_send_frame(ctx, frame);
     if (ret < 0) {
         fprintf(stderr, "Error sending the frame to the encoder\n");
         return -1;
     }
 
     /* read all the available output packets (in general there may be any
      * number of them */
     ret = avcodec_receive_packet(ctx, pkt);

     else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
         return 0;
     else if (ret < 0) {
         fprintf(stderr, "Error encoding audio frame\n");
         return -1;
     }
    return 0;
 }
static int alloc_swr(SwrContext *pswr)
{
    int ret;
    pswr = swr_alloc();
    if(pswr = NULL)
    {
       fprintf(stderr, "Error swr_alloc\n");
       return -1;
    }
    av_opt_set_int(pswr, "in_channel_layout",  AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(pswr, "out_channel_layout", AV_CH_LAYOUT_STEREO,  0);
    av_opt_set_int(pswr, "in_sample_rate",     16000, 0);
    av_opt_set_int(pswr, "out_sample_rate",    16000, 0);
    av_opt_set_sample_fmt(pswr, "in_sample_fmt",  AV_SAMPLE_FMT_S32, 0);
    av_opt_set_sample_fmt(pswr, "out_sample_fmt", AV_SAMPLE_FMT_FLTP,  0);
    ret = swr_init(pswr);
    if(ret < 0)
    {
        fprintf(stderr, "Error swr_init\n");
        return -1;
    }
    return 0;
}
 static int alloc_codec_tex(AVStream* st_audio,AVOutputFormat* xfmt,AVCodecContext* CodecCtx,AVCodec* Codec)
{
    int ret;
    
    Codec = avcodec_find_encoder(xfmt->video_codec);
    if (!Codec){
      printf("Can not find encoder!\n");
      return -1;
    }
    CodecCtx = st_audio->internal->avctx;
    CodecCtx->codec_id = AV_CODEC_ID_AAC;
    CodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
    CodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    CodecCtx->sample_rate= 16000;
    CodecCtx->channel_layout=AV_CH_LAYOUT_STEREO;
    CodecCtx->channels = av_get_channel_layout_nb_channels(CodecCtx->channel_layout);
    CodecCtx->bit_rate = 33000;  
    CodecCtx->profile=FF_PROFILE_AAC_LOW ;
    CodecCtx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL; 


    //Show some information
    //     av_dump_format(pFormatCtx, 0, out_file, 1);

    ret = avcodec_parameters_from_context(st_audio->codecpar, CodecCtx);
    if (ret < 0) {
     fprintf(stderr, "Could not initialize stream parameters\n");
     return -1;
    }
    if ((ret = avcodec_open2(CodecCtx, Codec,NULL)) < 0){
      printf("Failed to open encoder!\n");
      return -1;
    } 

    return 0;
}
 int CBX_pcmtoaac(const char *src,const char *des)
 {
     const char *out_file, *in_file;
     AVFormatContext* pFormatCtx;
     AVOutputFormat* fmt = NULL;
     AVStream* audio_st;
     AVCodecContext* pCodecCtx;
     AVCodec* pCodec;
  
     uint8_t* frame_buf;
     AVFrame* pFrame;
     AVPacket* pkt = NULL;
     SwrContext *swr;
     uint8_t *outs[2];    
     int ret=0;
     int size=0;
  
     FILE *in_file=NULL;                         //Raw PCM data
     int framenum=1000;                          //Audio frame number
     int i;
  
     
      if (argc <= 2) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        exit(0);
     }
     in_file    = src;
     out_file    = des;

     in_file= fopen(in_file, "rb");

     //Method 1.
     avformat_alloc_output_context2(&pFormatCtx, fmt, NULL, out_file);
 //    fmt = pFormatCtx->oformat;
  
     //Open output URL
     if (avio_open(&pFormatCtx->pb,out_file, AVIO_FLAG_READ_WRITE) < 0){
         printf("Failed to open output file!\n");
         return -1;
     }
     audio_st = avformat_new_stream(pFormatCtx, 0);
     if (audio_st==NULL){
         return -1;
     }
     //创建AVCodecContext并且填充数据流
     ret = alloc_codec_tex(audio_st,fmt,pCodecCtx,pCodec);
     if(ret < 0)
     {
         printf("Failed to alloc_codec_tex!\n");
         return -1;
     }
     //创建并初始化SwrContext转换器
     ret = alloc_swr(swr);
     if(ret < 0)
     {
         printf("Failed to alloc_swr!\n");
         return -1;
     }

     pFrame = av_frame_alloc();
     if (!pFrame) {
         printf(">>>====error av_frame_alloc \n");
         return -1;
     }
     pFrame->nb_samples     = pCodecCtx->frame_size;
     pFrame->format         = pCodecCtx->sample_fmt;
     pFrame->channel_layout = pCodecCtx->channel_layout;
     ret = av_frame_get_buffer(pFrame, 0);
     if (ret < 0) {
         fprintf(stderr, "Could not allocate audio data buffers\n");
         return -1;
     }

     pkt = av_packet_alloc();
     if (!pkt) {
         printf(">>>====error av_packet_alloc \n");
         return -1;
     }
     size = av_samples_get_buffer_size(NULL, pCodecCtx->channels,pCodecCtx->frame_size,pCodecCtx->sample_fmt, 1);

     outs[0]=(uint8_t *)av_malloc(size);
     outs[1]=(uint8_t *)av_malloc(size);
     printf(">>>===  aaa  size = %d channels = %d frame_size = %d  sample_fmt = %d \n"
                        ,size,pCodecCtx->channels,pCodecCtx->frame_size,pCodecCtx->sample_fmt);
     frame_buf = (uint8_t *)av_malloc(size);


     //Write Header
     avformat_write_header(pFormatCtx,NULL);

     for (i=0; i<framenum; i++){
         //Read PCM
         if (fread(frame_buf, 1, size, in_file) <= 0){
             printf("Failed to read raw data! \n");
             return -1;
         }else if(feof(in_file)){
             printf("read to eof! \n");
             break;
         }   
         int count=swr_convert(swr, outs,pCodecCtx->frame_size,(const uint8_t **)&frame_buf,pFrame->nb_samples);//len 为4096
         pFrame->data[0] =(uint8_t*)outs[0];//audioFrame 是VFrame
         pFrame->data[1] =(uint8_t*)outs[1];

         pFrame->pts=i*100;
         //Encode
         encode(pCodecCtx,pFrame,pkt);

         printf("Succeed to encode 1 frame! \tsize:%5d\n",pkt->size);
         pkt->stream_index = audio_st->index;
         ret = av_write_frame(pFormatCtx, pkt);
         if (ret < 0) {
             fprintf(stderr, "av_write_frame error\n");
             return -1;
         }
         av_packet_unref(pkt); 
     }
     //Flush Encoder
     encode(pCodecCtx,NULL,pkt);
     ret = av_write_frame(pFormatCtx, pkt);
     if (ret < 0) {
         fprintf(stderr, "av_write_frame_ending error\n");
         return -1;
     }  
     av_packet_unref(pkt); 
     av_packet_free(pkt);
     //Write Trailer
     av_write_trailer(pFormatCtx);
     //Clean
     if (audio_st){
         avcodec_close(audio_st->internal->avctx);
         av_free(pFrame);
         av_free(frame_buf);
     }
     av_free(outs[0]);
     av_free(outs[1]);
     avio_close(pFormatCtx->pb);
     avformat_free_context(pFormatCtx);
  
     fclose(in_file);
  
     return 0;
 }

