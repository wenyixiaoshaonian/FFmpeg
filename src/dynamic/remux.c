#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C"
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
//#include <libswscale/swscale_internal.h>


static int open_codec_context(AVStream *in_streams,
                              AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;
    const AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    /* find decoder for the stream */
    dec = avcodec_find_decoder(in_streams->codecpar->codec_id);
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
    if ((ret = avcodec_parameters_to_context(*dec_ctx, in_streams->codecpar)) < 0) {
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

    return 0;
}

//AVMEDIA_TYPE_VIDEO
static int open_input_file(const char *filename,AVFormatContext **fmt_ctx)
{
    int ret;
    AVCodec *dec;
 
    if ((ret = avformat_open_input(fmt_ctx, filename, NULL, NULL)) < 0) {
        printf( "Cannot open input file\n");
        return ret;
    }
 
    if ((ret = avformat_find_stream_info(*fmt_ctx, NULL)) < 0) {
        printf( "Cannot find stream information\n");
        return ret;
    }
    av_dump_format(fmt_ctx, 0, filename, 0);
    return 0;
}

int CBX_remux(const char *src,const char *des)
{
    AVFormatContext *ifmt_ctx = NULL,*ofmt_ctx = NULL;
    int ret = 0;
    const char *infile,*outfile;
    const AVOutputFormat *ofmt = NULL;
    AVPacket *pkt;

    AVCodecParameters *in_codecpar;
    AVStream *in_stream, *out_stream;
    AVCodecContext *pCodecCtx= NULL;
    int i,index;
    
    infile  = src;
    outfile = des;

    //打开输入文件
    ret = open_input_file(infile,&ifmt_ctx);
    if(ret < 0)
    {
        fprintf(stderr,"open_input_file error\n");
        exit(0);
    }
    
    //创建输出文件信息
    avformat_alloc_output_context2(&ofmt_ctx, ofmt, NULL, outfile);
    if(!ofmt_ctx)
    {
        fprintf(stderr,"avformat_alloc_output_context2 error\n");
        exit(0);

    }
//    ofmt = ofmt_ctx->oformat;

    pkt = av_packet_alloc();
    if (!pkt)
        return -1;

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        in_stream = ifmt_ctx->streams[i];
        in_codecpar = in_stream->codecpar;

        //在输出文件中创建数据流信息,赋值输入文件中的流信息
        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        //复制编码器(参数等)
        open_codec_context(in_stream,&pCodecCtx,ifmt_ctx,in_codecpar->codec_type);
        
        avcodec_parameters_from_context(out_stream->codecpar,pCodecCtx);

        out_stream->codecpar->codec_tag = 0;
        
    }

    av_dump_format(ofmt_ctx, 0, outfile, 1);
    //打开输出文件
    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, outfile, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open output file '%s'", outfile);
            exit(0);
        }
    }

    //写入头部
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }

    while(1)
    {
        //读取一帧视频数据，当读到eof或者其他错误时，返回小于0的错误码
        ret = av_read_frame(ifmt_ctx, pkt);
        if (ret < 0)
            break;

        in_stream  = ifmt_ctx->streams[pkt->stream_index];

        out_stream = ofmt_ctx->streams[pkt->stream_index];

        /* copy packet */
        //重新计算时间戳，计算 "a * b / c" 的值并分五种方式来取整
        //以输入、输出流中不同数据流中的时间基为区别
        pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
        pkt->pos = -1;

        ret = av_interleaved_write_frame(ofmt_ctx, pkt);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }
        av_packet_unref(pkt);  
    }

    av_write_trailer(ofmt_ctx);
end:
    avformat_close_input(&ifmt_ctx);

    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);


    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }
    return 0;
}
