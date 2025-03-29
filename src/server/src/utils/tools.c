// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils/tools.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>

#define MAX_EXTENSION   10

int get_file_info(const char *file_path, const char **file_type, int64_t *modify_time, int64_t *file_size)
{
    static const char * file_type_maps[] = {
        "app", ";desktop;DESKTOP;",
        "archive", ";7z;ace;arj;bz2;cab;gz;gzip;jar;r00;r01;r02;r03;r04;r05;r06;r07;r08;r09;r10;r11;r12;r13;r14;r15;r16;r17;r18;r19;r20;r21;r22;r23;r24;r25;r26;r27;r28;r29;rar;tar;tgz;z;zip;7Z;ACE;ARJ;BZ2;CAB;GZ;GZIP;JAR;R00;R01;R02;R03;R04;R05;R06;R07;R08;R09;R10;R11;R12;R13;R14;R15;R16;R17;R18;R19;R20;R21;R22;R23;R24;R25;R26;R27;R28;R29;RAR;TAR;TGZ;Z;ZIP;",
        "audio", ";aac;ac3;aif;aifc;aiff;au;cda;dts;fla;flac;it;m1a;m2a;m3u;m4a;mid;midi;mka;mod;mp2;mp3;mpa;ogg;opus;ra;rmi;spc;rmi;snd;umx;voc;wav;wma;xm;AAC;AC3;AIF;AIFC;AIFF;AU;CDA;DTS;FLA;FLAC;IT;M1A;M2A;M3U;M4A;MID;MIDI;MKA;MOD;MP2;MP3;MPA;OGG;OPUS;RA;RMI;SPC;RMI;SND;UMX;VOC;WAV;WMA;XM;",
        "doc", ";c;chm;cpp;csv;cxx;doc;docm;docx;dot;dotm;dotx;h;hpp;htm;html;hxx;ini;java;lua;mht;mhtml;ods;odt;odp;pdf;potx;potm;ppam;ppsm;ppsx;pps;ppt;pptm;pptx;rtf;sldm;sldx;thmx;txt;vsd;wpd;wps;wri;xlam;xls;xlsb;xlsm;xlsx;xltm;xltx;xml;C;CHM;CPP;CSV;CXX;DOC;DOCM;DOCX;DOT;DOTM;DOTX;H;HPP;HTM;HTML;HXX;INI;JAVA;LUA;MHT;MHTML;ODS;ODT;ODP;PDF;POTX;POTM;PPAM;PPSM;PPSX;PPS;PPT;PPTM;PPTX;RTF;SLDM;SLDX;THMX;TXT;VSD;WPD;WPS;WRI;XLAM;XLS;XLSB;XLSM;XLSX;XLTM;XLTX;XML;",
        "pic", ";ani;bmp;gif;ico;jpe;jpeg;jpg;pcx;png;psd;tga;tif;tiff;webp;wmf;ANI;BMP;GIF;ICO;JPE;JPEG;JPG;PCX;PNG;PSD;TGA;TIF;TIFF;WEBP;WMF;",
        "video", ";3g2;3gp;3gp2;3gpp;amr;amv;asf;avi;bdmv;bik;d2v;divx;drc;dsa;dsm;dss;dsv;evo;f4v;flc;fli;flic;flv;hdmov;ifo;ivf;m1v;m2p;m2t;m2ts;m2v;m4b;m4p;m4v;mkv;mp2v;mp4;mp4v;mpe;mpeg;mpg;mpls;mpv2;mpv4;mov;mts;ogm;ogv;pss;pva;qt;ram;ratdvd;rm;rmm;rmvb;roq;rpm;smil;smk;swf;tp;tpr;ts;vob;vp6;webm;wm;wmp;wmv;3G2;3GP;3GP2;3GPP;AMR;AMV;ASF;AVI;BDMV;BIK;D2V;DIVX;DRC;DSA;DSM;DSS;DSV;EVO;F4V;FLC;FLI;FLIC;FLV;HDMOV;IFO;IVF;M1V;M2P;M2T;M2TS;M2V;M4B;M4P;M4V;MKV;MP2V;MP4;MP4V;MPE;MPEG;MPG;MPLS;MPV2;MPV4;MOV;MTS;OGM;OGV;PSS;PVA;QT;RAM;RATDVD;RM;RMM;RMVB;ROQ;RPM;SMIL;SMK;SWF;TP;TPR;TS;VOB;VP6;WEBM;WM;WMP;WMV;",
        NULL,
    };

    *file_type = "other";
    *modify_time = *file_size = 0;

    struct stat statbuf;
    if (stat(file_path, &statbuf) != 0)
        return 1;

    *modify_time = statbuf.st_mtim.tv_sec;
    *file_size = statbuf.st_size;

    if (S_ISDIR(statbuf.st_mode)) {
        *file_type = "dir";
    } else if (S_ISREG(statbuf.st_mode)) {
        // 处理文件后缀
        const char *dot = strrchr(file_path, '.');
        if (!dot) { // 没有后缀
            *file_type = "other";
            return 0;
        }

        int ext_len = strlen(file_path) - (dot-file_path); /* include . */
        if (ext_len > MAX_EXTENSION) { // 无效后缀
            *file_type = "other";
            return 0;
        }

        g_autofree char *ext = g_strconcat(";", dot+1, ";", NULL);
        for (const char **p = file_type_maps; *p; p+=2) {
            if (strstr(*(p+1), ext)) {
                *file_type = *p;
                return 0;
            }
        }

        *file_type = "other";
    } else {
        *file_type = "other";
    }

    return 0;
}

char *format_time(int64_t modify_time)
{
    // 将 UNIX 时间戳转换为 GDateTime 对象（本地时区）
    g_autoptr(GDateTime) datetime = g_date_time_new_from_unix_local(modify_time);
    if (!datetime) {
        return g_strdup("Invalid time");
    }

    // 格式化时间字符串
    return g_date_time_format(datetime, "%Y/%m/%d %H:%M:%S");
}

char *format_size(int64_t size)
{
    if (size < 1000)
        return g_strdup_printf("%ld B", size);

    return g_format_size(size);
}

