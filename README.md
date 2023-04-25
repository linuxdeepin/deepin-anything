# deepin-anything

Something like everything, but nothing is really like anything...

The development of anything is derived from everything under Windows, and it is committed to providing a lightning-fast filename search function for Linux users, and provides offline search functions. Originally there was a similar rlocate program in Linux, but it was too old, so it was rewritten.

### Build dependencies

*The current development branch is **master**, build dependencies may change without updating this description, please refer to `./debian/control` for a list of build dependencies*.

- debhelper (>= 9)
- dkms
- qtbase5-dev
- pkg-config
- libudisks2-qt5-dev
- libmount-dev
- libglib2.0-dev
- libpcre3-dev

## Installation

### Build from source code

1. Make sure all dependencies are installed.

   *Package names may be different for different distributions, if your distribution provides deepin-anything, please check the packaging scripts provided by your distribution.*

If you are using [Deepin](https://distrowatch.com/table.php?distribution=deepin) or any other Debian based distribution that provides deepin-anything：

```shell
# apt build-dep deepin-anything
```

2. Build：

```shell
$ cd deepin-anything
$ make
```

3. Installation：

```shell
# make install
```

## Documentations

## Getting help

- [Official Forum](https://bbs.deepin.org/) for generic discussion and help.
- [Developer Center](https://github.com/linuxdeepin/developer-center) for BUG report and suggestions.
- [Wiki](https://wiki.deepin.org/)
- [docs](https://github.com/linuxdeepin/deepin-anything/tree/master/docs)

## Getting involved

We encourage you to report issues and contribute changes

- [Contribution guide for developers](https://github.com/linuxdeepin/developer-center/wiki/Contribution-Guidelines-for-Developers-en)

## License

deepin-anything prior to version 5.0.15 (included) is licensed under  [GPL-2.0-only](LICENSE).

deepin-anything after version 5.0.15 is licensed under [GPL-3.0-or-later](LICENSE).