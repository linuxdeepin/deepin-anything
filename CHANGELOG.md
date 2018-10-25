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
