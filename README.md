Something like everything, but nothing is really like anything...

anything的开发源自于Windows下的everything，它致力于为Linux用户提供一个闪电般速度的文件名搜索功能，并提供了离线搜索功能。原来在Linux中有一个类似的rlocate的程序，但是那个程序有点太老了，而重写一个更好的也不难，因此就重写了一个。

在处理器上，我们希望此工具能覆盖x86、龙芯与申威平台，当然，平台越多越好。

具体使用测试请参见`docs/end_user_tester.md`，应用程序开发人员请参看`docs/app_developer.md`，库开发人员请参看`docs/lib_developer.md`，有关设计方面的考虑请参看`docs/design_considerations.md`。

# 编译

进入根目录，直接`make`即可编译生成对应的内核模块与用户态程序。编译所有程序需要安装`make`与`gcc`，编译内核模块需要安装内核头文件，编译用户态程序没有额外的依赖。编译生成的内核模块名为`vfs_monitor.ko`(使用了`-O3`参数优化)，在`kernelmod`目录下。编译生成的用户态共享库位于`library/bin`目录，分为调试版(`debug/libanything.so`)与发布版(`release/libanything.so`)，编译生成的用户态命令行测试程序位于`cli/bin`目录，分为调试版(`debug/anything_cli`)与发布版(`release/anything_cli`)。

其中内核模块位于`kernelmod`目录，用户态共享库位于`library`目录，命令行测试程序位于`cli`目录。当前，用户态程序支持所有平台，内核模块暂时仅支持x86与龙芯平台。

当然，你也可以进入每个目录执行`make`来分模块构建，但是因为`cli`里的测试程序依赖于`library`里的动态库，所以需要首先构建`library`，再构建`cli`。

在运行环境上，内核模块需要依赖内核的CONFIG\_KPROBES选项，用户态程序仅依赖于glibc库。

# 对比

为了说明anything的特性，先来做一个简单但仍有典型意义的文件搜索对比测试。

首先说明下测试环境，测试环境物理机为ThinkPad x230，8G内存，机械硬盘。虚拟机为运行在VirtualBox里的Deepin 15.4，内存为2G，处理器为单核2.9 GHz。

虚拟机的文件系统排除`/sys`、`/proc`、`/dev`与`/run`目录后共有目录、链接与文件数约38.7万个，其中挂载了一个虚拟机外的文件系统以方便文件交换，在虚拟机内此文件系统类型为vboxsf，大约有11.5万个文件与目录。

测试方法如下：

* 使用`sysctl -w vm.drop_caches=3`清空缓存，然后运行`find / -name "*hellfire*`，耗时约39.9秒
* 带缓存再次运行`find / -name "*hellfire*`，耗时约10.1秒，通过运行`free`命令对比cache项得知新增的文件缓存约占用260MB内存
* 使用anything的基础索引搜索hellfire，耗时约6毫秒，基础索引占用内存约7MB，测试程序占用内存约14MB
* 使用anything的二级索引搜索hellfire，耗时0毫秒，二级索引占用内存约230MB，测试程序占用内存约500MB

实际上，多次测试的结果表明，使用基础索引比使用带缓存的`find`搜索要快大约500倍甚至更多。若使用全内存式的二级索引，anything的搜索速度是使用基础索引搜索速度的20倍以上，但是占用内存将增长35倍。若将二级索引存放在磁盘上，其内存占用将减少到近乎零，在大部分情况下仍然能够取得很好的搜索速度，仍比基础索引有明显的速度加快，但是在原始字符串较多的情况下，因为需要从索引文件中读取较大量的数据，就会显得比基础索引搜索还要慢了，这个是典型的时间空间复杂度互换的问题。