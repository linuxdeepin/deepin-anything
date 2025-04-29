// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/config.h"
#include "spdlog/spdlog.h"
#include "utils/tools.h"

#include <glib.h>
#include <sstream>

Config& Config::instance() {
    static Config instance;
    return instance;
}

Config::Config()
{
    // init blacklist_paths_
    blacklist_paths_ = {
        "/.git",
        "/.svn",
        "$HOME/.cache",
        "$HOME/.config",
        "$HOME/.local/share/Trash",
    };
    // Replace $HOME with actual home directory path
    auto home = g_get_home_dir();
    for (auto& path : blacklist_paths_) {
        size_t pos = path.find("$HOME");
        if (pos != std::string::npos) {
            path.replace(pos, 5, home);
        }
    }

    // init indexing_paths_
    indexing_paths_ = {
        "$HOME",
    };
    // Replace $HOME with actual home directory path
    for (auto& path : indexing_paths_) {
        size_t pos = path.find("$HOME");
        if (pos != std::string::npos) {
            path.replace(pos, 5, home);
        }
    }

    // init file_type_mapping_
    file_type_mapping_ = {
        {"app",     "desktop"},
        {"archive", "7z;ace;arj;bz2;cab;gz;gzip;jar;r00;r01;r02;r03;r04;r05;r06;r07;r08;r09;"
                    "r10;r11;r12;r13;r14;r15;r16;r17;r18;r19;r20;r21;r22;r23;r24;r25;r26;r27;"
                    "r28;r29;rar;tar;tgz;z;zip"},
        {"audio",   "aac;ac3;aif;aifc;aiff;au;cda;dts;fla;flac;it;m1a;m2a;m3u;m4a;mid;midi;"
                    "mka;mod;mp2;mp3;mpa;ogg;opus;ra;rmi;spc;rmi;snd;umx;voc;wav;wma;xm;ape"},
        {"doc",     "c;chm;cpp;csv;cxx;doc;docm;docx;dot;dotm;dotx;h;hpp;htm;html;hxx;ini;java;"
                    "lua;mht;mhtml;ods;odt;odp;pdf;potx;potm;ppam;ppsm;ppsx;pps;ppt;pptm;pptx;rtf;"
                    "sldm;sldx;thmx;txt;vsd;wpd;wps;wri;xlam;xls;xlsb;xlsm;xlsx;xltm;xltx;xml;latex;"
                    "wpt;md;odg;dps;sh;xhtml;dhtml;shtm;shtml;json;css;yaml;bat;js;sql;uof;ofd;log;tex"},
        {"pic",     "ani;bmp;gif;ico;jpe;jpeg;jpg;pcx;png;psd;tga;tif;tiff;webp;wmf;svg"},
        {"video",   "3g2;3gp;3gp2;3gpp;amr;amv;asf;asx;avi;bdmv;bik;d2v;divx;drc;dsa;dsm;dss;dsv;"
                    "evo;f4v;flc;fli;flic;flv;hdmov;ifo;ivf;m1v;m2p;m2t;m2ts;m2v;m4b;m4p;m4v;mkv;"
                    "mp2v;mp4;mp4v;mpe;mpeg;mpg;mpls;mpv2;mpv4;mov;mts;ogm;ogv;pss;pva;qt;ram;"
                    "ratdvd;rm;rmm;rmvb;roq;rpm;smil;smk;swf;tp;tpr;ts;vob;vp6;webm;wm;wmp;wmv"},
    };
}

std::shared_ptr<event_handler_config> Config::make_event_handler_config()
{
    auto config = std::make_shared<event_handler_config>();
    config->persistent_index_dir = std::string(g_get_user_cache_dir()) + "/deepin-anything-server";
    config->volatile_index_dir = std::string(g_get_user_runtime_dir()) + "/deepin-anything-server";
    // 4 threads: main thread, event thread, dbus thread, timer thread
    std::size_t free_threads = std::max(std::thread::hardware_concurrency() - 4, 1U);
    config->thread_pool_size = get_thread_pool_size_from_env(free_threads);
    config->blacklist_paths = blacklist_paths_;
    config->indexing_paths = indexing_paths_;
    config->file_type_mapping = file_type_mapping_;
    for (const auto& [file_type, file_exts] : file_type_mapping_) {
        std::stringstream ss(file_exts);
        std::string item;
        while (std::getline(ss, item, ';')) {
            config->file_type_mapping[item] = file_type;
        }
    }

    return config;
}

bool is_path_in_blacklist(const std::string& path, const std::vector<std::string>& blacklist_paths)
{
    for (const auto& blacklisted_path : blacklist_paths) {
        if (path.find(blacklisted_path) != std::string::npos) {
            return true;
        }
    }
    return false;
}
