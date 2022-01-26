#include <iostream>
#include <SDL2/SDL.h>
#include "SDL2/SDL_image.h"
extern "C"
{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale_internal.h>

#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>

}

static int open_input_file(const char *filename,AVFormatContext *ifmt_ctx)
{
    int ret = 0;

    infile = filename;
    if ((ret = avformat_open_input(&ifmt_ctx, filename, NULL, NULL)) < 0) {
//        std::cout << "Cannot open input file\n";
        printf("Cannot open input file");
        return ret;
    }
 
    if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
        printf("Cannot find stream information");
        return ret;
    }
    av_dump_format(ifmt_ctx, 0, filename, 0);
    return 0;

}

static int open_codec_context(int *stream_idx,
                              AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;
    AVCodec *dec;
    AVStream *st;

    /* select the video stream */
    ret = av_find_best_stream(fmt_ctx, type, -1, -1, &dec, 0);
    if (ret < 0) {
        printf( "Cannot find a video stream in the input file\n");
        return ret;
    }
    stream_index = ret;
    st = fmt_ctx->streams[stream_index];

    dec = avcodec_find_decoder(st->codecpar->codec_id);

    *dec_ctx = avcodec_alloc_context3(dec);

    /* Copy codec parameters from input stream to output codec context */
    if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
        printf( "Failed to copy codec parameters to decoder context\n");
        return ret;
    }

    /* init the video decoder */
    if ((ret = avcodec_open2(*dec_ctx, dec, NULL)) < 0) {
        printf( "Cannot open video decoder\n");
        return ret;
    }
    *stream_idx = stream_index;

    return 0;
}

int main(int argc, char* argv[])
{
 
	AVOutputFormat *ofmt = NULL;
	AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVCodecContext *dec_ctx;
	AVPacket pkt;
	const char *in_filename, *out_filename;
	int ret, i;
	int videoindex=-1;
    int audioindex=-1;
	int frame_index=0;
    int audio_index=0;
    int64_t start_time=0;
    AVStream *in_stream, *out_stream;
    AVCodecParameters *in_codecpar;
    AVStream *in_stream, *out_stream;

    out_filename = "rtmp://localhost/publishlive/livestream";//输出 URL（Output URL）[RTMP]
	//out_filename = "rtp://233.233.233.233:6666";//输出 URL（Output URL）[UDP]
	
    if(argc <2) {
        printf("Usage: %s <input media file>",argv[0]);
    }

    in_filename = argv[1];

    avformat_network_init();

    open_input_file(in_filename,ifmt_ctx);
    
//    open_codec_context(*videoindex,&dec_ctx_v, ifmt_ctx, AVMEDIA_TYPE_VIDEO);
//    open_codec_context(*videoindex,&dec_ctx_a, ifmt_ctx, AVMEDIA_TYPE_AUDIO);

    avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", out_filename); //RTMP
    if(!ofmt_ctx)
    {
        fprintf(stderr,"avformat_alloc_output_context2 error\n");
        exit(0);

    }

    ofmt = ofmt_ctx->oformat;    

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
        open_codec_context(in_stream,&dec_ctx,ifmt_ctx,in_codecpar->codec_type);
        
        avcodec_parameters_from_context(out_stream->codecpar,dec_ctx);

        if (in_codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            videoindex = in_stream;
        if (in_codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            audioindex = in_stream;     

        out_stream->codecpar->codec_tag = 0;
    }
    //打开输出文件
    ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
        printf( "Error avio_open\n");
        exit(0);
    }

    //写文件头（Write file header）
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        printf( "Error occurred when opening output URL\n");
        exit(0);
    }
    start_time=av_gettime();

    while (1) {

            //获取一个AVPacket（Get an AVPacket）
            ret = av_read_frame(ifmt_ctx, &pkt);
            if (ret < 0)
                break;

            //视频处理 
            if(pkt.stream_index==videoindex){  
                //Simple Write PTS
                if(pkt.pts==AV_NOPTS_VALUE){
                    //Write PTS
                    AVRational time_base1=ifmt_ctx->streams[videoindex]->time_base;
                    //Duration between 2 frames (us)
                    int64_t calc_duration=(double)AV_TIME_BASE/av_q2d(ifmt_ctx->streams[videoindex]->r_frame_rate);
                    //Parameters
                    pkt.pts=(double)(frame_index*calc_duration)/(double)(av_q2d(time_base1)*AV_TIME_BASE);
                    pkt.dts=pkt.pts;
                    pkt.duration=(double)calc_duration/(double)(av_q2d(time_base1)*AV_TIME_BASE);
                }
                //添加视频推流的延迟,长度为视频帧的持续时长
                AVRational time_base=ifmt_ctx->streams[videoindex]->time_base;
                int64_t pts_time = av_rescale_q(pkt.dts, time_base, AV_TIME_BASE_Q);    //切换到文件播放的时间
                int64_t now_time = av_gettime() - start_time;
                //播放此帧的时间戳和实际的时间比较
                if (pts_time > now_time)
                    av_usleep(pts_time - now_time);
     
                //Print to Screen
                printf("Send %8d video frames to output URL\n",frame_index);
                frame_index++;
            }
            //音频处理 
            //音频数据很小，推流时不用加延时，但需要加上pts
            if(pkt.stream_index==audioindex){   
            //todo https://www.cnblogs.com/leisure_chn/p/10623968.html
                if(pkt.pts==AV_NOPTS_VALUE){
                    //Write PTS
                    AVRational time_base1=ifmt_ctx->streams[audioindex]->time_base;
                    //Duration between 2 frames (us)
                    int64_t calc_duration=(double)AV_TIME_BASE/av_q2d(ifmt_ctx->streams[audioindex]->r_frame_rate);
                    //Parameters
                    pkt.pts=(double)(audio_index*calc_duration)/(double)(av_q2d(time_base1)*AV_TIME_BASE);
                    pkt.dts=pkt.pts;
                    pkt.duration=(double)calc_duration/(double)(av_q2d(time_base1)*AV_TIME_BASE);
                    
                }
            printf("Send %8d video frames to output URL\n",audio_index);
            audio_index++;

            }
            in_stream  = ifmt_ctx->streams[pkt.stream_index];
            out_stream = ofmt_ctx->streams[pkt.stream_index];            
            //转换PTS/DTS（Convert PTS/DTS）
            pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
            pkt.pos = -1;

            //ret = av_write_frame(ofmt_ctx, &pkt);
            ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
     
            if (ret < 0) {
                printf( "Error muxing packet\n");
                break;
            }
            
            av_free_packet(&pkt);
            
        }
        //写文件尾（Write file trailer）
        av_write_trailer(ofmt_ctx);

        
        avformat_close_input(&ifmt_ctx);
        /* close output */
        if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
            avio_close(ofmt_ctx->pb);
        avformat_free_context(ofmt_ctx);
        if (ret < 0 && ret != AVERROR_EOF) {
            printf( "Error occurred.\n");
            return -1;
        }

    return 0;
}

