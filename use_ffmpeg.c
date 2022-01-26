#include "stdio.h"

//解封装
int CBX_demux(const char *src,const char *des_video,const char *des_audio);


//视频解码
extern int CBX_264toYUV(const char *src,const char *des);          //use avformat
extern int CBX_H264toYUV(const char *src,const char *des);         //not use avformat
//视频编码
extern int CBX_YUVto264(const char *src,const char *des);          //not use avformat
extern int CBX_yuvtoH264(const char *src,const char *des);         //use avformat

extern int CBX_pcmtoaac_raw(const char *src,const char *des);

extern int CBX_pcmtoaac_raw_filter(const char *src,const char *des);

int main(int argc, char **argv)
{
#if 0
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        exit(0);
    }
#else
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input file> <output video file> <output audio file>\n", argv[0]);
        exit(0);
    }
#endif
//      CBX_demux(argv[1],argv[2],argv[3]);
//    CBX_264toYUV(argv[1],argv[2]);
//    CBX_H264toYUV(argv[1],argv[2]);
//    CBX_aactopcm(argv[1],argv[2]);
//    CBX_pcmtoaac(argv[1],argv[2]);
//    CBX_demux_codec(argv[1],argv[2],argv[3]);
//    CBX_pcmtoaac_raw(argv[1],argv[2]);
    CBX_pcmtoaac_raw_filter(argv[1],argv[2]);

//    CBX_YUVto264(argv[1],argv[2]);
//    CBX_yuvtoH264(argv[1],argv[2]);
    return 0;
}
