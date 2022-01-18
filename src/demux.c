#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale_internal.h>

#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>



static int open_codec_context(AVFormatContext *ifmt,AVCodecContext **pCodecCtx,int *stream_idx,enum AVMediaType type)
{
    int ret, stream_index;
    AVStream *st;
    const AVCodec *dec = NULL;
    AVDictionary *opts = NULL;


    ret = av_find_best_stream(ifmt, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input file\n",
                av_get_media_type_string(type));
        return ret;
    } else {
        stream_index = ret;
        st = ifmt->streams[stream_index];
        
        /* find decoder for the stream */
        dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            fprintf(stderr, "Failed to find %s codec\n",
                    av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* Allocate a codec context for the decoder */
        *pCodecCtx = avcodec_alloc_context3(dec);
        if (!*pCodecCtx) {
            fprintf(stderr, "Failed to allocate the %s codec context\n",
                    av_get_media_type_string(type));
            return AVERROR(ENOMEM);
        }

        /* Copy codec parameters from input stream to output codec context */
        if ((ret = avcodec_parameters_to_context(*pCodecCtx, st->codecpar)) < 0) {
            fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                    av_get_media_type_string(type));
            return ret;
        }

        /* Init the decoders */
        if ((ret = avcodec_open2(*pCodecCtx, dec, &opts)) < 0) {
            fprintf(stderr, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
        *stream_idx = stream_index;
    }

    return 0;
}

static int open_input_file(const char *filename,AVFormatContext **ifmt)
{
    int ret;
    AVCodec *dec;
 
    if ((ret = avformat_open_input(ifmt, filename, NULL, NULL)) < 0) {
        printf( "Cannot open input file\n");
        return ret;
    }
 
    if ((ret = avformat_find_stream_info(*ifmt, NULL)) < 0) {
        printf( "Cannot find stream information\n");
        return ret;
    }
    av_dump_format(*ifmt, 0, filename, 0);
    return ret;
}


int CBX_demux(const char *src,const char *des_video,const char *des_audio)
{
    AVFormatContext *ifmt_ctx = NULL,*ofmt_ctx_v = NULL,*ofmt_ctx_a = NULL;
    AVCodecContext *pCodecCtx_v= NULL;
    AVCodecContext *pCodecCtx_a= NULL;
    
    const char *infile,*out_videofile,*out_audiofile;
    AVStream *out_v_stream,*out_a_stream;
    int stream_idx_v,stream_idx_a;
    AVStream *out_stream_v,*out_stream_a;
    AVPacket *pkt;
    const AVOutputFormat *ofmt_v = NULL,*ofmt_a = NULL;

    int ret;

    infile        = src;
    out_videofile = des_video;
    out_audiofile = des_audio;

    //打开输入文件 获取其中的数据流信息
    open_input_file(infile,&ifmt_ctx);

    //创建两个format结构体，分别存放video、audio数据
    avformat_alloc_output_context2(&ofmt_ctx_v,NULL,NULL,out_videofile);
    avformat_alloc_output_context2(&ofmt_ctx_a,NULL,NULL,out_audiofile);

    //创建输出格式结构体
    ofmt_v = ofmt_ctx_v->oformat;
    ofmt_a = ofmt_ctx_a->oformat;
    
    //创建两个数据流
    out_stream_v = avformat_new_stream(ofmt_ctx_v,NULL);
    out_stream_a = avformat_new_stream(ofmt_ctx_a,NULL);
    
    //获取编码器信息
    open_codec_context(ifmt_ctx,&pCodecCtx_v,&stream_idx_v,AVMEDIA_TYPE_VIDEO);
    open_codec_context(ifmt_ctx,&pCodecCtx_a,&stream_idx_a,AVMEDIA_TYPE_AUDIO);

    //复制编码器信息
    avcodec_parameters_from_context(out_stream_v->codecpar,pCodecCtx_v);
    out_stream_v->codecpar->codec_tag = 0; 

    avcodec_parameters_from_context(out_stream_a->codecpar,pCodecCtx_a);
    out_stream_a->codecpar->codec_tag = 0; 

    //打开输出文件
    if (!(ofmt_v->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx_v->pb, out_videofile, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open output file '%s'", out_videofile);
            goto end;
        }
    }
    if (!(ofmt_a->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx_a->pb, out_audiofile, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open output file '%s'", out_audiofile);
            goto end;
        }
    }   
    //写入头部
    ret = avformat_write_header(ofmt_ctx_v, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }
    ret = avformat_write_header(ofmt_ctx_a, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }
    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "Could not allocate packet\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    while(1)
    {
        ret = av_read_frame(ifmt_ctx,pkt);
        if (ret < 0)
            break;

        //写入数据
        if(pkt->stream_index == stream_idx_v) {
              printf(">>>===111 video pkt.size = %d\n",pkt->size);
              ret = av_write_frame(ofmt_ctx_v, pkt);
        }
        else if (pkt->stream_index == stream_idx_a) {
              printf(">>>===222 audio pkt.size = %d\n",pkt->size);
              pkt->stream_index = out_stream_a->index;  //源文件中的audio_index = 1，到新文件中需要重置 否则会报错
              ret = av_write_frame(ofmt_ctx_a, pkt);
        }
        else
            printf("pkt->stream_index = %d  stream_idx_v = %d  stream_idx_a = %d\n",
                                        pkt->stream_index,stream_idx_v,stream_idx_a);
        av_packet_unref(pkt);
        if (ret < 0)
            break;
    }

    //写入尾部
    av_write_trailer(ofmt_ctx_v);
    av_write_trailer(ofmt_ctx_a);

end:
    avcodec_free_context(&pCodecCtx_v);
    avcodec_free_context(&pCodecCtx_a);
    avformat_close_input(&ifmt_ctx);
    av_packet_free(&pkt);

    return 0;
}

