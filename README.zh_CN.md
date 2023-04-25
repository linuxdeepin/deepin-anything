# deepin-anything

Something like everything, but nothing is really like anything...

anything 的开发源自于 Windows 下的 everything，它致力于为 Linux 用户提供一个闪电般速度的文件名搜索功能，并提供了离线搜索功能。原来在 Linux 中有一个类似的 rlocate 的程序，但太过陈旧，所以进行了重写。

### 编译依赖

*当前的开发分支为**master**，编译依赖可能会在没有更新本说明的情况下发生变化，请参考`./debian/control`以获取构建依赖项列表*。

- debhelper (>= 9)
- dkms
- qtbase5-dev
- pkg-config
- libudisks2-qt5-dev
- libmount-dev
- libglib2.0-dev
- libpcre3-dev

## 安装

### 构建过程

1. 确保已经安装所有依赖库。

   *不同发行版的软件包名称可能不同，如果您的发行版提供了deepin-anything，请检查发行版提供的打包脚本。*

如果你使用的是 [Deepin](https://distrowatch.com/table.php?distribution=deepin) 或其他提供了 anything 的基于 Debian 的发行版：

```shell
# apt build-dep deepin-anything
```

2. 构建：

```shell
$ cd deepin-anything
$ make
```

3. 安装：

```shell
# make install
```

## 文档

## 帮助

- [官方论坛](https://bbs.deepin.org/) 
- [开发者中心](https://github.com/linuxdeepin/developer-center) 
- [Wiki](https://wiki.deepin.org/)
- [docs](https://github.com/linuxdeepin/deepin-anything/tree/master/docs)

## 贡献指南

我们鼓励您报告问题并做出更改

- [开发者代码贡献指南](https://github.com/linuxdeepin/developer-center/wiki/Contribution-Guidelines-for-Developers) 

## 开源许可证

deepin-anything 5.0.15 版本及其之前，在 [GPL-2.0-only](LICENSE) 下发布。

deepin-anything 5.0.15 版本以后，在 [GPL-3.0-or-later](LICENSE) 下发布。