# 建立基础索引并搜索

```C
#include "fs_buf.h"
#include "walkdir.h"

...

/* 
创建基础索引对象，创建后索引没有实际数据。

INITIAL_BUFSIZE是应用程序指定的fsbuf内部缓存区初始大小，单位为字节。
此缓存区用来存储文件系统索引，所以它和文件系统中文件与目录数量的多少成正比。作为一个参考值，一个有38.7万个文件与目录的文件系统实际占用了约7 MB内存的缓存区，此参数至少应为1 MB + strlen(root_path)，最大为1 GB，如果在使用过程中发现缓存区不够，程序将自动扩展缓存区空间。

root_path是文件系统的根目录，例如，你只对自己的家目录感兴趣，就可以仅索引/home/deepin目录。

new_fs_buf返回NULL表示失败(参数错误、内部分配内存错误或者是初始化同步锁错误)，返回非空值表示成功。
 */
fs_buf* fsbuf = new_fs_buf(INITIAL_BUFSIZE, root_path);

/*
建立基础索引。

创建了fsbuf后，即可以调用build_fstree建立索引。

build_fstree的第一个参数为通过new_fs_buf创建的fs_buf的指针。

第二个参数则是一个0或者1的参数，表示是否按照分区建立索引。若此参数为0，则表示在构建索引时将仅在fsbuf的root_path所在的分区上建立索引，而不会对其它分区的文件系统建立索引。若此参数为1，则表示在构建索引时将对fsbuf的root_path下的所有的目录与文件建立索引，而不管其位于哪个分区。由于分区的加载与卸载将导致大量的文件加入与删除，因此我们推荐此参数设置为0，即每个分区都建立自己的基础索引，而不要将所有分区的基础索引合并在一起。

第三个参数为进度控制函数，是一个函数指针，其原型为int (uint32_t file_count, uint32_t dir_count, const char* cur_dir, void *param)。其中file_count为当前已经扫描过的文件数，dir_count为当前已经扫描过的目录数，cur_dir为当前正准备开始扫描的目录全路径，param为用户自定义的参数指针，可以传入自定义的参数。此函数将在进入每个目录开始扫描之前被调用，如果此函数的返回值为非零值，则表示需要取消整个创建索引的过程，而且build_fstree函数将返回1，否则build_fstree函数将返回0。一旦创建索引的过程被取消，则必须重新创建一个fs_buf对象再重新调用build_fstree扫描。通过此函数，也可以控制build_fstree函数的暂停，例如在此函数里进行等待，则整个build_fstree都将等待。由于此函数在每个目录都会被调用，因此此函数的处理应该非常快速，例如不要在每次调用的时候函数都打印一个日志，那就会导致创建索引慢数十倍。如果对构建索引的进度不感兴趣，也不想中途停止构建索引，可以将此参数设置为空。

第四个参数即为传给用户定义的进度控制函数的参数指针，显然若第三个参数为空，则第四个参数无意义。

建立基础索引将忽略/proc、/sys、/dev与/run目录。

由于build_fstree将对文件系统进行遍历，因此此过程会有一定耗时，其时间将试文件系统的情况而定。作为一个参考值，一个有38.7万个文件与目录的文件系统遍历耗时约36秒。
*/
build_fstree(fsbuf, 0, NULL, NULL);

/*
保存基础索引。

save_fs_buf返回0表示成功，不然表示失败。
*/
char* full_path = "/home/deepin/myfsbuf.lft";
save_fs_buf(fsbuf, full_path);

/*
搜索。

使用search_files函数进行搜索，并将获得的值传入get_path_by_name_off参数获得完整的路径，如果需要知道是文件还是目录，可以调用is_file函数判断。此外，search_files函数还支持分页(通过第二个参数)。

参见cli/src/console_test.c中的search_by_fsbuf函数，下面是一个简化版。
*/
uint32_t name_offs[MAX_RESULTS], end_off = get_tail(fsbuf);
uint32_t count = MAX_RESULTS, start_off = first_name(fsbuf);
search_files(fsbuf, &start_off, end_off, query, name_offs, &count);

char path[PATH_MAX];
for (uint32_t i = 0; i < count; i++) {
	char *p = get_path_by_name_off(fsbuf, name_offs[i], path, sizeof(path));
	printf("\t%'u: %c %s\n", i+1, is_file(fsbuf, name_offs[i]) ? 'F' : 'D', p);
}
uint32_t total = count;
while(count == MAX_RESULTS) {
	search_files(fsbuf, &start_off, end_off, query, name_offs, &count);
	total += count;
}
return total;
```

如果希望在给定目录中搜索，可以使用两种方法。

第一种是先调用`get_path_range`函数，此函数原型为`void get_path_range(fs_buf* fsbuf, char* path, uint32_t *path_off, uint32_t *start_off, uint32_t *end_off)`，然后再将得到的start\_off与end\_off设置为`search_files`中的start\_off参数的初始值与end\_off的参数值。

第二种方法是仍然仅使用`search_files`函数，但是在此函数调用得到路径后，再使用C语言的`strstr`函数判断是否是相应的目录里的路径即可。由于`search_files`给出的结果是按照路径排序的，因此，一旦发现原来有目录下的路径，但是现在没有了，即可结束对`search_files`的继续调用了。

```C
/*
载入基础索引。

使用load_fs_buf函数即可载入基础索引。一般来说，在最开始build_fstree建立好基础索引后，以后每次都只需要load_fs_buf，然后监听文件系统的变更即可。

此函数返回0表示成功，返回非零表示失败。
*/
fs_buf* fsbuf = NULL;
load_fs_buf(&fsbuf, "/home/deepin/myfsbuf.lft");

```

# 建立二级索引并搜索

TODO。请先参见`new_allmem_index`、`add_index`、`save_allmem_index`、`load_fs_index`、`get_index_keyword`等函数在`cli/src/main.c`与`cli/src/console_test.c`里面的使用。

值得注意的是`load_fs_index`有两种策略，每种策略的二级索引载入花费的时间、程序占用的内存以及搜索的快慢是不一样的。

此外，基础索引现在是支持文件系统变更修改的，但是二级索引还没有变更修改的功能，所以如果使用二级索引，暂时只能支持离线搜索。

# 文件系统更新

anything开发了一个内核模块用来监听文件系统的文件创建、文件删除与文件改名事件，并将其保存下来，等待上层用户态应用程序的提取。

在使用此功能之前，需要载入内核模块，需要通过root用户进入`kernelmod`目录使用`insmod vfs_monitor.ko`载入内核模块，卸载内核模块使用`rmmod vfs_monitor`即可。

由于文件系统更新完全是异步的，与程序逻辑无关，因此用户态应用程序一般的做法是开发一个守护进程或守护线程(daemon)，对给定的接口进行循环查询得到变更信息，并调用相应的函数将变更信息传递给基础索引，让其更新其内部的数据。

此daemon的一般流程如下(可参考`cli/src/monitor_vfs.c`的相关代码)：

1. daemon启动时需要得到一个fs\_buf的指针，要么是创建时外界构建好(即成功调用`build_fstree`后)传给它的，要么是它自行创建并调用`build_fstree`构建好的
2. daemon应调用`ioctl`函数访问`/proc/vfs_changes`文件，这个文件是内核模块创建的接口文件，用来给上层用户态应用程序访问的。`ioctl`传入的第一个参数应是打开`/proc/vfs_changes`文件的文件描述符，第二个参数应是`VC_IOCTL_READDATA`(参见`kernelmod/vfs_change_uapi.h`文件)，第三个参数应是一个`ioctl_rd_args`类型的指针，此结构体中的`count`成员应该初始化为`data`成员指向的内存块的大小(单位为字节)
3. `ioctl`返回零则表示调用成功，此时第三个参数将会被修改，其中的`count`变量表示的是一共有多少条文件系统的变更，`data`则保存了具体的变更。`data`中数据的格式是第一个字节为变更的类型，其值参见`kernelmod/vfs_change_consts.h`文件，接着是一个C风格的字符串，表示变更的文件或目录名，以空字符结尾，如果是变更是一个改名变更，则接下来还会是一个C风格的字符串，表示改名后的文件或目录名，以空字符结尾。接下来又是一个字节的变更类型，依次类推。
4. daemon在得到文件变更信息后，应该分别调用`rename_path`、`remove_path`与`insert_path`函数处理文件(目录)的改名、文件(目录)的删除以及文件(目录)的添加，以修改原fs\_buf的内部文件系统结构
5. 在所有的文件变更都修改完毕后，可以调用`save_fs_buf`保存基础索引数据。如果此fs\_buf是与外部搜索使用的fs\_buf是同一个对象，则不需要额外的同步处理，基础索引会自行在内部处理多线程同步的问题，但是对于非共享对象来说(例如daemon是一个单独的守护进程)，则需要使用额外的手段通知被搜索的基础索引更新数据，如果数据量不大(38.7万个文件与目录仅需要7 MB，载入仅需要不到10毫秒)，建议调用`load_fs_buf`直接重新载入基础索引数据即可

在实现上，文件系统更新同步当然可以采用多种方式，例如daemon也可以将这些文件更新保存到一个文件里，等待搜索程序将这些变更及时应用到正在被使用的基础索引对象上。

此外，在载入了内核模块(`insmod vfs_monitor.ko`)之后，用户还可以通过`cat /proc/vfs_changes`来直观地看到文件系统的变更情况，但是这些变更在`ioctl`被成功调用之后就会被删除，而且一旦保存这些变更的内存超过了1 MB，最老的文件系统变更就将被删除。

# 其它

出于安全方面的考虑，我们可能不希望让用户看到他/她不能看到的文件/目录，这一点索引系统并不负责，而且为了能获得所有的文件系统数据，索引系统要求在建立索引的时候使用root用户的权限，或者至少能有相应的能力(capabilities，一般是DAC相关的能力)能得到所有目录与文件的文件名以及它们的从属关系。

因此，调用方应该自行处理权限相关的问题，例如通过`search_files`得到的某个路径是否应该被当前搜索的用户看到。注意，这个问题不是简单地使用`access`函数就可以搞定的，因为不能读取并不表示不能看到。

此外，也许用户会想要忽略调用某些目录下的文件，例如`/var/log`、`/tmp`、`.git`、`.svn`或者干脆是所有`.`开头目录与文件，同样索引系统也不会提供这种设置，这些配置管理需要由调用者负责，即根据用户在应用程序里的配置由应用程序自行对`search_files`得到的结果进行过滤。

对于外置存储，建议不要对其进行自动索引，因为对于IO速度较慢或者文件较多的外置存储设备，索引时间会比较长，干扰设备的使用。最好让用户手工明确对外置存储设备的索引，将索引数据保存在本机上，以提供离线搜索功能，同时也可以避免外设的只读权限问题。而且由于外置存储设备有可能被离线修改，因此，其索引数据无法保证与实际的文件系统数据同步，最好每次都全部重新检索。
