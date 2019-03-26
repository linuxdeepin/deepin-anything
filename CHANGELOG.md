<a name="0.0.5"></a>
## 0.0.5 (2019-03-26)


#### Features

*   Support segmentation search ([4a5d8f66](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/4a5d8f669918f8ed04be6cbdc5e4362fbe1a4f4f))
*   support cancel build job ([f021b0ba](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/f021b0badd9d7c8764cb5ed8e68decaae44a0ecd))
*   support log file ([20cc9fd9](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/20cc9fd9b27986af40c51206e61a10d5992414ca))
*   auto update index files when autoIndexInternal/autoIndexExternal property changed ([3587dbf9](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/3587dbf98097ea84c0f33b8b94ce09d8b643667f))
*   add propertys "autoIndexInternal" "autoIndexExternal" ([a818029c](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/a818029cfd9baeb0de2771fdf107da05657e8706))
*   add interfaces "quit" "rebuildPath" ([21bc8594](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/21bc8594f9fd284c4b5d59a57a145592d70d0015))
*   auto restart monitor systemd server ([85db8384](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/85db83848864ee88488f799d84ff89c43039db6b))
*   add the "update-lft" plugin for monitor ([7a58dbfb](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/7a58dbfb160f4eaa06cb8d8112718d32d74bbcef))
*   deepin-anything-tool use system bus ([2c1d64d1](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/2c1d64d1dcc3c32e585f14562ff819cd252d315f))
*   add interface "hasLFTSubdirectories" ([79300a84](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/79300a847734b5f19b5114e7731ca6cd74e73911))
*   add dbus interfaces ([2e779a9f](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/2e779a9fa68ab13bc108cd0e1ba7896ddaa2dee3))
*   add "insertFileToLFTBuf" "removeFileFromLFTBuf" "renameFileOfLFTBuf" interfaces ([05743dc3](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/05743dc3d9779cd3707e126ce7df62be8ea20df0))
*   arm64 arch support ([d249a775](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/d249a7753c5c72546bbd65e1098c789e49bcb84e))

#### Bug Fixes

*   support for linux 5.0+ ([d3c353f1](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/d3c353f11989a6221f9378b4ce7bf0608c0cd8e9))
*   the lft file save path ([bcb24cc8](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/bcb24cc86cb3c4baaecdda7c490799b42c55ffc3))
*   set the locale codec to UTF-8 if it is ASCII ([a0c443a0](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/a0c443a0e7a3b1ed270cf04761b47fb2316d7288))
*   crash at application exit ([9c95021f](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/9c95021f7e4b1d9c77375bed5e0f1fc4115fbb9c))
*   log message error ([b1198fc9](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/b1198fc9a02865d48283e28c6f94e7e1cd81f31a))
*   The file will be deleted immediately after the index data is synchronized to the filex ([e838078b](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/e838078b420acc05c312c97d586741c76e972ab4))
*   crash in getFsBufByPath ([d5fff65d](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/d5fff65d770393271d67461dc47886b46ab06d54))
*   searh file no results ([7a8659fb](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/7a8659fb9dcc2206fc643094b01912eb52473873))
*   the LFTManager::hasLFT ([0e581eb8](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/0e581eb806b111061be01faa0b6541b700d84d9a))
*   can not search subdirectory ([9ef50d4c](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/9ef50d4c5db094cf75e71487bef99b2c17e4ac89))
*   install the deepin-anything-monitor.service file to "/lib/systemd/system" ([2a6231bf](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/2a6231bf25d0223a786aaf52b54fb724f4de0dd7))
*   failed sync LFT file  to locale ([11f27edd](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/11f27edd93f677842e8c3851db527e2f138a7c02))
*   Failed on install the deepin-anything-server package ([86406e5f](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/86406e5f0a587d5552cc8812d0ae991f47f9874f))
*   can not start deepin-anything-tool with dbus mode ([d402f918](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/d402f918e3ec9c5aa7619b10941364d2d03e3a19))
*   can not found libanything.so when build in shuttle ([7071745a](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/7071745af96114f37bec78d55587a6873c4ecbea))
*   support for linux 4.20+ ([34353bfc](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/34353bfcc680990bbb583936415e2b03434d4a93))



<a name="0.0.4"></a>
## 0.0.4 (2018-10-25)


#### Bug Fixes

*   build failure due to missing include ([33afa678](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/33afa678a15ac34593bafd7b837a421c4c66b9a1))



<a name="0.0.3"></a>
## 0.0.3 (2018-08-12)


#### Bug Fixes

*   configure the "deepin-anything-server" package failed on debian ([6807829f](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/6807829f4af6f074ad377f990a3b7177a0d60629))
*   the "get_path_range" function may fall into an infinite loop ([00157be2](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/00157be24206a710faf03139ccac83e5115d9c26))
*   can not get the root path range if it is not end with '/' ([7fcc7c2a](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/7fcc7c2aa2a29ef4ae2cfc4417904ee01c3b3838))
*   crash at walkdir when the fsbuf realloc ([49107c6e](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/49107c6e5c18526918fb3c74af9ff8a942efec7a))
*   can not get the root path pange info ([147fa733](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/147fa733b172c26801c24255b8629af1817835ae))
*   crash in build_fstree(the "parts" array out of bounds) ([e4265c1b](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/e4265c1b677b6855dc5afbb005c1297264ae8f89))
*   segment faults when invoke strlen, now change it to strncmp. ([a26c2634](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/a26c2634f523fdb5d5f0acd621605c668a86d800))



<a name="0.0.2"></a>
## 0.0.2 (2018-07-26)


#### Bug Fixes

*   remove dependency of deepin kernel header ([0091731d](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/0091731dc2fc4848572fa1e222e32cfbeebc0e8a))
*   failed when insert the module on 4.17 version kernel ([b3997f5b](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/b3997f5be93f727be2047e8e27ed0b5b3873af55))
*   hidden the "deepin_anything_server" user ([7d96b298](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/7d96b29895e1595f03fc4f3f38544ee850a2220f))
*   failed build(kernel version > 4.14) ([fa382e81](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/fa382e8133b8b31061942dd715e1fa9d9b08ed7d))
*   failed on configure package ([f33effcc](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/f33effcc7831ae611e74bf2b72ce06426a651d86))
*   disable warning ([7a6d87c4](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/7a6d87c491982445cd0dbb05b97e88a7cd96de9c))
* **deepin-anything-server:**  set QLibrary::ResolveAllSymbolsHint for QPluginLoader ([569f461f](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/569f461f032ddde5bb00df0b3876ff9e81ff24b8))

#### Features

*   add dkms depend ([f204db2d](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/f204db2d44164ed4ac366de331e85b625a737fcb))
*   support fuzzy search. ([18ffe1a9](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/18ffe1a9cf907d96d0828fe5ff32e0ba053a22d4))
*   reload plugin file when the file if modified ([d2b338e4](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/d2b338e4f9171977b412e7b28cccda7276cacedd))
*   support dynamic insertion and removal of plugins ([9d93afe1](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/9d93afe16d7275c399bba9e70e983aa0f9676621))
*   add deepin-anything-server ([92b46aeb](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/92b46aeb2b3c3f8f707d266be47e0e69580af3cf))
*   add the vfs_change_consts.h file to dev package ([27e0db00](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/27e0db00700c8d551a9a902431bd832c0d4a3828))
*   add the "vfs_change_uapi.h" file to dev package ([11e71768](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/11e71768e0b63f69cd4dcf8ea85c6129133a84c2))
*   add VC_IOCTL_WAITDATA for vfs_changes ([fa0584ac](https://github.com/linuxdeepin/deepin-anything/tree/master/commit/fa0584aca3dd5c13d035ea09e54079de3b21214b))



<a name="0.0.1"></a>
## 0.0.1 (2018-05-04)


#### Bug Fixes

*   crash on vfs_changed ([941bb45d](941bb45d))
*   change __vfs_read(xxx) to kernel_read(xxx) ([8eb5dbf6](8eb5dbf6))

#### Features

*   use GFP_ATOMIC ([e95d92f7](e95d92f7))
