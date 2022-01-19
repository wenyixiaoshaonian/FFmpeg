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

     if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
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

    av_opt_set_int(pswr, "in_channel_layout",  AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(pswr, "out_channel_layout", AV_CH_LAYOUT_STEREO,  0);
    av_opt_set_int(pswr, "in_sample_rate",     48000, 0);
    av_opt_set_int(pswr, "out_sample_rate",    48000, 0);
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
     FILE *file, *outfile;
     int i;
  
     in_file    = src;
     out_file    = des;

     file= fopen(in_file, "rb");
     if (!file) {
         fprintf(stderr, "Could not open %s\n", in_file);
         return -1;
     }

     //Method 1.
     avformat_alloc_output_context2(&pFormatCtx, NULL, NULL, out_file);
     fmt = pFormatCtx->oformat;
  
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
    pCodecCtx = audio_st->internal->avctx;
    pCodecCtx->codec_id = AV_CODEC_ID_AAC;
    pCodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
    pCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    pCodecCtx->sample_rate= 48000;
    pCodecCtx->channel_layout=AV_CH_LAYOUT_STEREO;
    pCodecCtx->channels = av_get_channel_layout_nb_channels(pCodecCtx->channel_layout);
    pCodecCtx->bit_rate = 158696;  
    pCodecCtx->profile=FF_PROFILE_AAC_LOW ;
    pCodecCtx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL; 

    pCodec = avcodec_find_encoder(pCodecCtx->codec_id);
    if (!pCodec){
      printf("Can not find encoder!\n");
      return -1;
    }

    //Show some information
    av_dump_format(pFormatCtx, 0, out_file, 1);

    ret = avcodec_parameters_from_context(audio_st->codecpar, pCodecCtx);
    if (ret < 0) {
     fprintf(stderr, "Could not initialize stream parameters\n");
     return -1;
    }
    if ((ret = avcodec_open2(pCodecCtx, pCodec,NULL)) < 0){
      printf("Failed to open encoder!\n");
      return -1;
    } 
#if 0
     //创建并初始化SwrContext转换器
    swr = swr_alloc();
    av_opt_set_int(swr, "in_channel_layout",  AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(swr, "out_channel_layout", AV_CH_LAYOUT_STEREO,  0);
    av_opt_set_int(swr, "in_sample_rate",     48000, 0);
    av_opt_set_int(swr, "out_sample_rate",    48000, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt",  AV_SAMPLE_FMT_S32, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLTP,  0);
    ret = swr_init(swr);
#endif
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
     i = 0;
     while (!feof(file)){
         //Read PCM
         if (fread(frame_buf, 1, size, file) <= 0){
             printf("Failed to read raw data! \n");
             return -1;
         }
 //        int count=swr_convert(swr, outs,pCodecCtx->frame_size,(const uint8_t **)&frame_buf,pFrame->nb_samples);//len 为4096
         pFrame->data[0] =frame_buf;//audioFrame 是VFrame
 //        pFrame->data[1] =(uint8_t*)outs[1];

         pFrame->pts=i*100;
         i++;
         //Encode
         encode(pCodecCtx,pFrame,pkt);

  //       printf("Succeed to encode 1 frame! \tsize:%5d\n",pkt->size);
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
