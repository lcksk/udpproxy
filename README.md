# udpproxy
UDP转发程序，把FFMPEG按时间片切割的HLS序列发送到指定的IP及端口地址，程序包含了友好的log及config读取功能，利用linux内核提供的list_head双链表高效完成功能，也包含了对inotify对文件夹监控的功能。
