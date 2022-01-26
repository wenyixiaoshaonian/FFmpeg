#include <stdio.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libavutil/opt.h"
#include <libavformat/internal.h>
//#include <fdk-aac/aacenc_lib.h>
#include <libavcodec/codec.h>

 /* check that a given sample format is supported by the encoder */
 static int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt)
 {
     const enum AVSampleFormat *p = codec->sample_fmts;
 
     while (*p != AV_SAMPLE_FMT_NONE) {
         if (*p == sample_fmt)
             return 1;
         p++;
     }
     return 0;
 }

 /* just pick the highest supported samplerate */
 static int select_sample_rate(const AVCodec *codec)
 {
     const int *p;
     int best_samplerate = 0;
 
     if (!codec->supported_samplerates)
         return 44100;
 
     p = codec->supported_samplerates;
     while (*p) {
         if (!best_samplerate || abs(44100 - *p) < abs(44100 - best_samplerate))
             best_samplerate = *p;
         p++;
     }
     return best_samplerate;
 }
 static int init_filter_graph(AVFilterGraph **graph, AVFilterContext **src,
                             AVFilterContext **sink)
 {
     AVFilterGraph *filter_graph;
     AVFilterContext *abuffer_ctx;
     const AVFilter  *abuffer;
     AVFilterContext *volume_ctx;
     const AVFilter  *volume;
     AVFilterContext *aformat_ctx;
     const AVFilter  *aformat;
     AVFilterContext *abuffersink_ctx;
     const AVFilter  *abuffersink;
 
     AVDictionary *options_dict = NULL;
     uint8_t options_str[1024];
     uint8_t ch_layout[64];
 
     int err;
 
     /* Create a new filtergraph, which will contain all the filters. */
     filter_graph = avfilter_graph_alloc();
     if (!filter_graph) {
         fprintf(stderr, "Unable to create filter graph.\n");
         return AVERROR(ENOMEM);
     }
 
     /* Create the abuffer filter;
      * it will be used for feeding the data into the graph. */
     abuffer = avfilter_get_by_name("abuffer");
     if (!abuffer) {
         fprintf(stderr, "Could not find the abuffer filter.\n");
         return AVERROR_FILTER_NOT_FOUND;
     }
 
     abuffer_ctx = avfilter_graph_alloc_filter(filter_graph, abuffer, "src");
     if (!abuffer_ctx) {
         fprintf(stderr, "Could not allocate the abuffer instance.\n");
         return AVERROR(ENOMEM);
     }
 
     /* Set the filter options through the AVOptions API. */
     av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, AV_CH_LAYOUT_STEREO);
     av_opt_set    (abuffer_ctx, "channel_layout", ch_layout,                            AV_OPT_SEARCH_CHILDREN);
     av_opt_set    (abuffer_ctx, "sample_fmt",     av_get_sample_fmt_name(AV_SAMPLE_FMT_FLT), AV_OPT_SEARCH_CHILDREN);
     av_opt_set_q  (abuffer_ctx, "time_base",      (AVRational){ 1, 48000 },  AV_OPT_SEARCH_CHILDREN);
     av_opt_set_int(abuffer_ctx, "sample_rate",    48000,                     AV_OPT_SEARCH_CHILDREN);
 
     /* Now initialize the filter; we pass NULL options, since we have already
      * set all the options above. */
     err = avfilter_init_str(abuffer_ctx, NULL);
     if (err < 0) {
         fprintf(stderr, "Could not initialize the abuffer filter.\n");
         return err;
     }
 
     /* Create volume filter. */
     volume = avfilter_get_by_name("volume");
     if (!volume) {
         fprintf(stderr, "Could not find the volume filter.\n");
         return AVERROR_FILTER_NOT_FOUND;
     }
 
     volume_ctx = avfilter_graph_alloc_filter(filter_graph, volume, "volume");
     if (!volume_ctx) {
         fprintf(stderr, "Could not allocate the volume instance.\n");
         return AVERROR(ENOMEM);
     }
 
     /* A different way of passing the options is as key/value pairs in a
      * dictionary. */
     av_dict_set(&options_dict, "volume", AV_STRINGIFY(0.9), 0);
     err = avfilter_init_dict(volume_ctx, &options_dict);
     av_dict_free(&options_dict);
     if (err < 0) {
         fprintf(stderr, "Could not initialize the volume filter.\n");
         return err;
     }
 
     /* Create the aformat filter;
      * it ensures that the output is of the format we want. */
     aformat = avfilter_get_by_name("aformat");
     if (!aformat) {
         fprintf(stderr, "Could not find the aformat filter.\n");
         return AVERROR_FILTER_NOT_FOUND;
     }
 
     aformat_ctx = avfilter_graph_alloc_filter(filter_graph, aformat, "aformat");
     if (!aformat_ctx) {
         fprintf(stderr, "Could not allocate the aformat instance.\n");
         return AVERROR(ENOMEM);
     }
 
     /* A third way of passing the options is in a string of the form
      * key1=value1:key2=value2.... */
     snprintf(options_str, sizeof(options_str),
              "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%"PRIx64,
              av_get_sample_fmt_name(AV_SAMPLE_FMT_S16), 48000,
              (uint64_t)AV_CH_LAYOUT_STEREO);
     err = avfilter_init_str(aformat_ctx, options_str);
     if (err < 0) {
         av_log(NULL, AV_LOG_ERROR, "Could not initialize the aformat filter.\n");
         return err;
     }
 
     /* Finally create the abuffersink filter;
      * it will be used to get the filtered data out of the graph. */
     abuffersink = avfilter_get_by_name("abuffersink");
     if (!abuffersink) {
         fprintf(stderr, "Could not find the abuffersink filter.\n");
         return AVERROR_FILTER_NOT_FOUND;
     }
 
     abuffersink_ctx = avfilter_graph_alloc_filter(filter_graph, abuffersink, "sink");
     if (!abuffersink_ctx) {
         fprintf(stderr, "Could not allocate the abuffersink instance.\n");
         return AVERROR(ENOMEM);
     }
 
     /* This filter takes no options. */
     err = avfilter_init_str(abuffersink_ctx, NULL);
     if (err < 0) {
         fprintf(stderr, "Could not initialize the abuffersink instance.\n");
         return err;
     }
 
     /* Connect the filters;
      * in this simple case the filters just form a linear chain. */
     err = avfilter_link(abuffer_ctx, 0, volume_ctx, 0);
     if (err >= 0)
         err = avfilter_link(volume_ctx, 0, aformat_ctx, 0);
     if (err >= 0)
         err = avfilter_link(aformat_ctx, 0, abuffersink_ctx, 0);
     if (err < 0) {
         fprintf(stderr, "Error connecting filters\n");
         return err;
     }
 
     /* Configure the graph. */
     err = avfilter_graph_config(filter_graph, NULL);
     if (err < 0) {
         av_log(NULL, AV_LOG_ERROR, "Error configuring the filter graph\n");
         return err;
     }
 
     *graph = filter_graph;
     *src   = abuffer_ctx;
     *sink  = abuffersink_ctx;
 
     return 0;
 }

 static int encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt, FILE *outfile)
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
    if(ret >=0) {
        fwrite(pkt->data, 1, pkt->size, outfile);

    }
     if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
         return 0;
     else if (ret < 0) {
         fprintf(stderr, "Error encoding audio frame\n");
         return -1;
     }
    return 0;
 }

 int CBX_pcmtoaac_raw(const char *src,const char *des)
 {
     const char *out_file, *in_file;
     AVOutputFormat* fmt = NULL;
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
     AVFilterGraph *graph;
     AVFilterContext *fsrc, *fsink;
     in_file    = src;
     out_file    = des;

     file= fopen(in_file, "rb");
     if (!file) {
         fprintf(stderr, "Could not open %s\n", in_file);
         return -1;
     }
     outfile= fopen(out_file, "wr");
     if (!outfile) {
         fprintf(stderr, "Could not open %s\n", in_file);
         return -1;
     }     

    pCodec = avcodec_find_encoder_by_name("libfdk_aac");
    if (!pCodec){
      printf("Can not find encoder!\n");
      return -1;
    }
    pCodecCtx = avcodec_alloc_context3(pCodec);
    if (!pCodecCtx) {
        fprintf(stderr, "Could not avcodec_alloc_context3\n");
        return -1;
    }

     //创建AVCodecContext并且填充数据流
    pCodecCtx->codec_id = pCodec->id;
    pCodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
    pCodecCtx->sample_fmt = AV_SAMPLE_FMT_S16;
    pCodecCtx->sample_rate= 48000;
    pCodecCtx->channel_layout=AV_CH_LAYOUT_STEREO;
    pCodecCtx->channels = av_get_channel_layout_nb_channels(pCodecCtx->channel_layout);
    pCodecCtx->bit_rate = 188000;  
    pCodecCtx->profile=FF_PROFILE_AAC_LOW ;
    pCodecCtx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL; 

    printf(">>>===  fmt->audio_codec = %d   AV_CODEC_ID_AAC = %d  pCodec->sample_fmts[0] = %d \n",pCodec->id,AV_CODEC_ID_AAC,pCodec->sample_fmts[0]);

    if ((ret = avcodec_open2(pCodecCtx, pCodec,NULL)) < 0){
      printf("Failed to open encoder!\n");
      return -1;
    } 
#if 0
     //创建并初始化SwrContext转换器
    swr = swr_alloc();
    av_opt_set_int(swr, "in_channel_layout",  AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(swr, "out_channel_layout", AV_CH_LAYOUT_STEREO,  0);
    av_opt_set_int(swr, "in_sample_rate",     48000,  0);
    av_opt_set_int(swr, "out_sample_rate",    48000,  0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt",  AV_SAMPLE_FMT_FLT, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_S16,  0);
    ret = swr_init(swr);
#else
    init_filter_graph(&graph, &fsrc, &fsink);
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
     size = size*2;
     outs[0]=(uint8_t *)av_malloc(size);
     outs[1]=(uint8_t *)av_malloc(size);
     printf(">>>===  aaa  size = %d channels = %d frame_size = %d  sample_fmt = %d \n"
                        ,size,pCodecCtx->channels,pCodecCtx->frame_size,pCodecCtx->sample_fmt);
     frame_buf = (uint8_t *)av_malloc(size);


     i = 0;
     while (!feof(file)){
         //Read PCM
         if (fread(frame_buf, 1, size, file) <= 0){
             printf("Failed to read raw data! \n");
             return -1;
         }
 #if 0        
         int count=swr_convert(swr, outs,pCodecCtx->frame_size,(const uint8_t **)&frame_buf,pFrame->nb_samples);//len 为4096
         pFrame->data[0] =(uint8_t*)outs[0];//audioFrame 是VFrame
         pFrame->data[1] =(uint8_t*)outs[1];

         pFrame->pts=i;
         i++;
#else
        /* push the audio data from decoded frame into the filtergraph */
        if (av_buffersrc_add_frame_flags(fsrc, frame_buf, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error while feeding the audio filtergraph\n");
            break;
        }

        /* pull filtered audio from the filtergraph */
        while (1) {
            ret = av_buffersink_get_frame(fsink, pFrame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0)
                break;
        }
#endif

         //Encode
         encode(pCodecCtx,pFrame,pkt,outfile);

         printf(" count = %d   pkt->pts = %d  pkt->size = %d\n",i,pkt->pts,pkt->size);
         av_packet_unref(pkt); 
     }
     //Flush Encoder
     encode(pCodecCtx,NULL,pkt,outfile);
     av_packet_unref(pkt); 
     av_packet_free(&pkt);

     av_free(pFrame);
     av_free(frame_buf);
     av_free(outs[0]);
     av_free(outs[1]);
  
     fclose(in_file);
  
     return 0;
 }

