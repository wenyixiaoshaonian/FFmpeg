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


static int open_codec_context(int *stream_idx,
                              AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;
    AVStream *st;
    const AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n",
                av_get_media_type_string(type), src_filename);
        return ret;
    } else {
        stream_index = ret;
        st = fmt_ctx->streams[stream_index];

        /* find decoder for the stream */
        dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            fprintf(stderr, "Failed to find %s codec\n",
                    av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* Allocate a codec context for the decoder */
        *dec_ctx = avcodec_alloc_context3(dec);
        if (!*dec_ctx) {
            fprintf(stderr, "Failed to allocate the %s codec context\n",
                    av_get_media_type_string(type));
            return AVERROR(ENOMEM);
        }

        /* Copy codec parameters from input stream to output codec context */
        if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
            fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                    av_get_media_type_string(type));
            return ret;
        }

        /* Init the decoders */
        if ((ret = avcodec_open2(*dec_ctx, dec, &opts)) < 0) {
            fprintf(stderr, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
        *stream_idx = stream_index;
    }

    return 0;
}

static int open_input_file(const char *filename,AVFormatContext *ifmt)
{
    int ret;
    AVCodec *dec;
 
    if ((ret = avformat_open_input(&ifmt, filename, NULL, NULL)) < 0) {
        printf( "Cannot open input file\n");
        return ret;
    }
 
    if ((ret = avformat_find_stream_info(ifmt, NULL)) < 0) {
        printf( "Cannot find stream information\n");
        return ret;
    }
    av_dump_format(ifmt, 0, filename, 0);
    return ret;
}


int CBX_demux(const char *src,const char *des_video,const char *des_audio)
{
    AVFormatContext *ifmt_ctx = NULL,*ofmt_ctx_v = NULL,*ofmt_ctx_a = NULL;
    const char *infile,*out_videofile,*out_audiofile;
    AVStream *out_v_stream,*out_a_stream;
    AVStream *in_stream;
    int *stream_idx_v,*stream_idx_a;
    AVStream *out_stream_v,*out_stream_a;
    AVPacket pkt;
    AVCodecParameters *in_codecpar;
    const AVOutputFormat *ofmt_v = NULL,*ofmt_a = NULL;
    static AVCodecContext *pCodecCtx_v= NULL;
    static AVCodecContext *pCodecCtx_a= NULL;
    int ret;
    if(argc < 4)
    {
        fprintf(srderr,"Usage: %s <input file> <vodeo output file> <audio output file>\n",argv[0]);
        exit(0);
    }

    infile        = src;
    out_videofile = des_video;
    out_audiofile = des_audio;

    //打开输入文件 获取其中的数据流信息
    open_input_file(infile,ifmt_ctx);

    //创建两个format结构体，分别存放video、audio数据
    avformat_alloc_output_context2(&ofmt_ctx_v,ofmt_v,NULL,out_videofile);
    avformat_alloc_output_context2(&ofmt_ctx_a,ofmt_a,NULL,out_audiofile);

    //创建输出格式结构体
//    ofmt_v = ofmt_ctx_v->oformat;
//    ofmt_a = ofmt_ctx_a->oformat;
    
    //创建两个数据流
    out_stream_v = avformat_new_stream(ofmt_ctx_v,NULL);
    out_stream_a = avformat_new_stream(ofmt_ctx_a,NULL);
    
    //获取编码器信息
    open_codec_context(stream_idx_v,&pCodecCtx_v,ifmt_ctx,AVMEDIA_TYPE_VIDEO);
    open_codec_context(stream_idx_a,&pCodecCtx_a,ifmt_ctx,AVMEDIA_TYPE_AUDIO);

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
            exit(0);
        }
    }
    if (!(ofmt_a->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx_a->pb, out_audiofile, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open output file '%s'", out_audiofile);
            exit(0);
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
        av_read_frame(ifmt_ctx,pkt);
        if (ret < 0)
            break;

        in_stream  = ifmt_ctx->streams[pkt.stream_index];
        in_codecpar = in_stream->codecpar;

        //写入数据
        if(pkt.stream_index == stream_idx_v)
              ret = av_write_frame(ofmt_ctx_v, pkt);
              
        else if (pkt.stream_index == stream_idx_a)
              ret = av_write_frame(ofmt_ctx_a, pkt);
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
    av_frame_free(&frame);

    return 0;
}

