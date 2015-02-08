#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <pthread.h>
#include <dirent.h>
#include <getopt.h>
#include <errno.h>
#include <sys/inotify.h>

#include "string.h"

#include "list.h"
#include "logger.h"
#include "config.h"

/*udp_datapack->type*/
#define REQ_FILE		0
#define ACK_TRUE		1
#define ACK_FAIL		2
#define ACK_RECONNECT   3
#define FILE_DATE		4


/*udp_req->flag*/
#define CLINET_EMPTY			0
#define CLINET_FIRST_REQ		1
#define CLINET_TIME_OUT		 	2
#define CLINET_LINK_ACK_WAIT	3
#define CLINET_LINK_ACK_TRUE	4
#define CLINET_LINK_ACK_FAIL    5

/*send_ack flag.*/
#define MAX       1024
#define MAX_UDP_FILE_COUNT 512
#define UDP_HEAD_LEN	13
#define TIMEVAL      45
#define UDP_PACKET_SIZE 4096
//微秒时间精度
#define TIME_SCALE  (1000000)


/*udp包格式定义*/
struct udp_datapack {
    char type;          /*数据包类型*/
    long int label;     /*文件包号、请求包文件位置信息*/
    int size;           /*包大小*/
    int check;          /*CRC*/
    char data[MAX];
};

/*传输的文件信息*/
struct  file_infor {
    int   file_fd;
    char *file_path; //文件名
    unsigned long  file_len;   //文件长度
    int64_t  timestamp;  //文件开始时间戳信息
    off_t  seek_flag;
    int      timeout;    /*超时计时*/
    int      dummy_flag;
    struct list_head list;
};

struct Context {
    char *work_dir;
    char *log_dir;
    char *dummy_file_path;
    int sock_fd;
    int file_count; //当前保存的文件计数
    int send_buf_size; //默认分配1024大小保存单个文件
    char *send_buf;
    pthread_mutex_t file_list_mutex;
    pthread_cond_t  file_list_cond;
    pthread_mutex_t wait_mutex;
    pthread_cond_t  wait_cond;

    char *ip_addr;
    int   port;
    int64_t   start_wait_interval; //内部以微秒管理，开始等待start_wait_interval秒开始发送UDP数据
    int   send_dummy_interval;//等待send_dummy_interval秒 还没有从FTP收到数据，开始发送dummy数据
    int  exit;
    int64_t sent_timestamp;
    int64_t stream_start_timestamp;
    int64_t system_start_timestamp;
    struct list_head head;
};

struct Context g_ctx;

struct option long_options[] =
{
    { "ip", 1, NULL, 'i' },
    { "port", 1, NULL, 'p' },
    { "start_wait_interval", 2, NULL, 't' },
    { "work_dir", 2, NULL, 'w' },
    { 0, 0, 0, 0 },
};

static char *const short_options = "i:p:tw";
/*互斥锁保护待发文件信息改变*/


/*crc校验*/
int crc_check(struct udp_datapack check_buff) {
    return 0;
}

/*crc校验判断*/
int crc_test(struct udp_datapack check_buff) {
    return 0;
}

static  int64_t get_current_time(void)
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (int64_t)tv.tv_sec * TIME_SCALE + tv.tv_usec;
}


/*待发文件列表初始化*/
void udp_init() {
    INIT_LIST_HEAD(&g_ctx.head);
    g_ctx.file_count = 0;
    g_ctx.send_buf_size = UDP_PACKET_SIZE;
    g_ctx.send_buf = calloc(g_ctx.send_buf_size, sizeof(char));
    if ((g_ctx.sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }
    g_ctx.start_wait_interval = 0;
    g_ctx.exit = 0;
    g_ctx.sent_timestamp = 0LL;
    pthread_mutex_init(&g_ctx.file_list_mutex, NULL);
    pthread_cond_init(&g_ctx.file_list_cond, NULL);

    pthread_mutex_init(&g_ctx.wait_mutex,NULL);
    pthread_cond_init(&g_ctx.wait_cond,NULL);

    g_ctx.system_start_timestamp = get_current_time();
    g_ctx.stream_start_timestamp = -1;

    g_ctx.ip_addr = NULL;
    g_ctx.port = -1;
    g_ctx.work_dir = NULL;
    g_ctx.log_dir = NULL;
    g_ctx.dummy_file_path = NULL;
    g_ctx.send_dummy_interval = 1800;

}

void remove_list_file(struct file_infor *del_info) {
    if (del_info) {
        close(del_info->file_fd);

        if (del_info->file_path) {
            free(del_info->file_path);
            del_info->file_path = NULL;
        }
        list_del(&del_info->list);

        free(del_info);
        del_info = NULL;
    }
    g_ctx.file_count--;
}


void udp_destroy() {
    struct list_head *pos,*n;
    struct file_infor *file_item = NULL;
    list_for_each_safe(pos, n, &g_ctx.head) {

        file_item = list_entry(pos, struct file_infor, list);

        printf("to= %s from= %lld\n", file_item->file_path, file_item->timestamp);

        remove_list_file(file_item);
    }

    list_empty(&g_ctx.head);

    if (g_ctx.send_buf) {
        free(g_ctx.send_buf);
        g_ctx.send_buf = NULL;
    }

    if (g_ctx.sock_fd != -1) close(g_ctx.sock_fd);

    if (g_ctx.work_dir) {
        free(g_ctx.work_dir);
        g_ctx.work_dir = NULL;
    }
    if (g_ctx.ip_addr) {
        free(g_ctx.ip_addr);
        g_ctx.ip_addr = NULL;
    }
    g_ctx.file_count = 0;
    pthread_mutex_destroy(&g_ctx.file_list_mutex);
    pthread_cond_destroy(&g_ctx.file_list_cond);
}

/*向待发文件列表中添加新项*/
void add_file_tail(struct file_infor *new_info) {
    if (new_info)  {
        list_add_tail(&(new_info->list), &g_ctx.head);
    }
}

void add_file_after(struct file_infor *position) {
	if (!position) return;
	struct list_head *pos,*n;
	struct file_infor *file_item = NULL;
	list_for_each_prev_safe(pos, n, &g_ctx.head){
		file_item = list_entry(pos, struct file_infor, list);
        //如果列表已经存在，不插入直接退出
		if (file_item && file_item->timestamp == position->timestamp)
            return;
        //找到第一个比当前待插入项时间戳小的后面插入
		else if (file_item && 
            file_item->timestamp < position->timestamp) {
            list_add(&(position->list),&(file_item->list));
            g_ctx.file_count++;
            pthread_cond_signal(&g_ctx.wait_cond);
			log_debug("add %s into list,file_infor=%p,filename=%s,timestamp=%lld,filecount=%d",
					  position->dummy_flag ? "dummy file" : "file",position, position->file_path, 
					  position->timestamp,g_ctx.file_count); 
            return;
		} 
	}
	if (!file_item) {
        add_file_tail(position);
        g_ctx.file_count++;
        log_debug("add %s into list,file_infor=%p,filename=%s,timestamp=%lld,filecount=%d",
					  position->dummy_flag ? "dummy file" : "file",position, position->file_path, 
				  position->timestamp,g_ctx.file_count); 
    }
}


/*取得文件列表中末尾项*/
struct file_infor* get_list_tail() {
    struct list_head *pos,*n;
    struct file_infor *file_item = NULL;
    list_for_each_prev_safe(pos, n, &g_ctx.head) {
        file_item = list_entry(pos, struct file_infor, list);
		if (file_item) return file_item;
	}

    return NULL;
}


int custom_filter(const struct dirent *pDir) {
    if ((pDir->d_type != DT_DIR)
        && strcmp(pDir->d_name, ".")
        && strcmp(pDir->d_name, "..")) {
        return 1;
    }

    return 0;
}

int strtoi(const char *s) {
	if (!s) return;
	int64_t num, i; 
    char ch;
    num = 0;
    for (i = 0; i < strlen(s); i++) {
        ch = s[i];
        if (ch < '0' || ch > '9') break;
        num = num * 10 + (ch - '0');
    }
    return num;
}

void add_by_file_name(const char *filename) {
	char filepath[1024];
    int  dummy_flag = 0;
	if (!filename) return;
    
	if (strtoi(filename) == 0) return;
	pthread_mutex_lock(&g_ctx.file_list_mutex); 

	char *suffix = strrchr(filename, '.');
	if (suffix && !strcmp(suffix, ".tmp")) {
		pthread_mutex_unlock(&g_ctx.file_list_mutex);
		return;
	} else if(suffix && !strcmp(suffix,".dummy")) 
        dummy_flag = 1;

	struct file_infor *tmp = (struct file_infor *)calloc(1,sizeof(*tmp));
	if (tmp) {
		memset(filepath, 0, sizeof(filepath));
		sprintf(filepath, "%s/", g_ctx.work_dir);

		strcat(filepath, filename);
		tmp->file_path = strdup(filepath);

		tmp->seek_flag = 0;
		tmp->timestamp = ((int64_t)strtoi(filename)) * TIME_SCALE;
        tmp->dummy_flag = dummy_flag;
		while (g_ctx.file_count >= MAX_UDP_FILE_COUNT) {
			pthread_cond_wait(&g_ctx.file_list_cond, &g_ctx.file_list_mutex);
		}
		//add_file_tail(tmp);
		add_file_after(tmp);
		pthread_mutex_unlock(&g_ctx.file_list_mutex);
	}

}
int scan_dir(const char *dirpath, char *filename) {

	struct dirent **namelist;
	int n;
	int i;

	if (!dirpath && !filename) return -1;
	if (dirpath) {
		n = scandir(dirpath, &namelist, custom_filter, alphasort);
		if (n < 0) {
			perror("scandir");
		} else {
			for (i = 0; i < n; i++) {

				add_by_file_name(namelist[i]->d_name);
                free(namelist[i]);

			}
		}

		free(namelist);
	}

	if (filename) {
		add_by_file_name(filename);
	}
}


/*循环读取目录文件信息*/
int event_loop() {
	struct inotify_event *event;
	int first_scan = 1;
	int fd, wd;
	int len;
	int nread;
	char buf[BUFSIZ];

	fd = inotify_init();
	if (fd < 0) {
		log_debug("inotify_init failed,fd=%d", fd);
		return -1;
	}

	wd = inotify_add_watch(fd, g_ctx.work_dir,
						   IN_CREATE | IN_ATTRIB | IN_MODIFY |  IN_MOVE);
	if (wd < 0) {
		log_debug("inotify_add_watch %s failed,ret=%d", g_ctx.work_dir, wd);
		return -1;
	}

	buf[sizeof(buf) - 1] = 0;
	while (!g_ctx.exit) {
		if (first_scan == 1) {
			scan_dir(g_ctx.work_dir, NULL);
			first_scan = 0;
		}

		while ((len = read(fd, buf, sizeof(buf) - 1)) > 0) {
			nread = 0;
			while (len > 0) {
				event = (struct inotify_event *)&buf[nread];

				if (!(event->mask & IN_ISDIR) &&
					event->mask & IN_CREATE || event->mask & IN_ATTRIB ||
					event->mask & IN_MODIFY || event->mask &  IN_MOVE) {

					if (g_ctx.work_dir &&  (strlen(g_ctx.work_dir) > 0)) 
                        scan_dir(NULL, event->name);

				}
				nread = nread + sizeof(struct inotify_event) + event->len;
				len = len - sizeof(struct inotify_event) - event->len;
			}
		}


	}
}

void wait_time(int64_t file_timestamp) {

    int64_t current_timestamp = get_current_time();

    //初始第一个文件的时间为流开始时间
    if (g_ctx.stream_start_timestamp == -1) {
        g_ctx.stream_start_timestamp = file_timestamp;

        if (file_timestamp + g_ctx.start_wait_interval >  current_timestamp) {
            usleep(g_ctx.start_wait_interval);
        }

    }

}

ssize_t readn(int fd, void *vptr, size_t n) {
	size_t nleft;
	ssize_t nread;
	char *ptr;
	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ((nread = read(fd, ptr, nleft)) < 0) {
			if (errno == EINTR) nread = 0;
			else return -1;
		} else if (nread == 0) break;
		nleft -= nread;
		ptr += nread;
	}
	return n - nleft;
}


///*向客户端发送文件*/
int send_file(struct file_infor *file_item) {
    int buf_len = 0;
    int file_block_length = 0;
    static int first_send = 1;

    struct sockaddr_in     serv_addr;
	memset(&serv_addr,0,sizeof(serv_addr));  
    serv_addr.sin_family = AF_INET;
    inet_pton(AF_INET, g_ctx.ip_addr, &serv_addr.sin_addr);
    serv_addr.sin_port = htons(g_ctx.port);

    wait_time(file_item->timestamp);


    file_item->file_fd = open(file_item->file_path, O_SYNC | O_RDONLY);

    struct stat fs;
    if (-1 != fstat(file_item->file_fd, &fs)) 
        file_item->file_len = fs.st_size;


	int total_send_bytes = 0, read_bytes = 0, send_bytes = 0;
	while (total_send_bytes < file_item->file_len) {
		read_bytes = readn(file_item->file_fd, g_ctx.send_buf, g_ctx.send_buf_size);
		if (read_bytes <= 0)  
            break;
		send_bytes = 0; 
		while (send_bytes < read_bytes) {
			int block_len = sendto(g_ctx.sock_fd, g_ctx.send_buf + send_bytes,
								   read_bytes - send_bytes, 0, (struct sockaddr *)&(serv_addr),
								   sizeof(struct sockaddr_in));
			if (block_len > 0) {
				send_bytes += block_len;
			} else {
				log_error("Send File:%s failed,timestamp=%lld Failed\n,truncted size=%d",
						  file_item->file_path, file_item->timestamp, file_item->file_len - total_send_bytes);
			}
		}
		total_send_bytes += send_bytes;
		memset(g_ctx.send_buf, 0, g_ctx.send_buf_size);

	}
}

static int copy_dummy_file(const char *dummy_file_path,char*file_name) {
	int from_fd, to_fd;
	int bytes_read, bytes_write;
	char buffer[BUFFER_SIZE];
	char *ptr;
	int ret = 0;
	char path[1024];
	memset(path, 0, sizeof(path));
	/* 打开源文件 */
	if ((from_fd = open(dummy_file_path, O_RDONLY)) == -1) {
		ret = -1;
		/*open file readonly,返回-1表示出错，否则返回文件描述符*/
		log_debug("Open %s Error:%s\n", dummy_file_path, strerror(errno));
		return ret;
	}

	/* 创建目的文件 */
	/* 使用了O_CREAT选项-创建文件,open()函数需要第3个参数,
	   mode=S_IRUSR|S_IWUSR表示S_IRUSR 用户可以读 S_IWUSR 用户可以写*/

	//把文件copy到工作目录，并且重命名文件为最后发送文件时间戳+1
    struct file_infor* file_info = get_list_tail();
	if (file_info) {
        sprintf(path, "%s/%lld.dummy", g_ctx.work_dir, (file_info->timestamp + TIME_SCALE) / TIME_SCALE); 
	} else
        sprintf(path, "%s/1.dummy",g_ctx.work_dir); 
	
	if ((to_fd = open(path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR)) == -1) {
		ret = -1;
		log_debug( "Open %s Error:%s\n", path, strerror(errno));
		return ret;

	}

	while (bytes_read = read(from_fd, buffer, BUFFER_SIZE)) {

		/* 一个致命的错误发生了 */
		if ((bytes_read == -1) && (errno != EINTR)) {
			ret = -1;
			break;
		} else if (bytes_read > 0) {
			ptr = buffer;
			while (bytes_write = write(to_fd, ptr, bytes_read)) {
				/* 一个致命错误发生了 */
				if ((bytes_write == -1) && (errno != EINTR)) {
					ret = -1;
					break;
				}
				/* 写完了所有读的字节 */
				else if (bytes_write == bytes_read) break;
				/* 只写了一部分,继续写 */
				else if (bytes_write > 0) {
					ptr += bytes_write;
					bytes_read -= bytes_write;
				}
			}
			/* 写的时候发生的致命错误 */
			if (bytes_write == -1) {
				ret = -1;
				break;
			}
		}
	}
    strcpy(file_name,path);
	close(from_fd);
	close(to_fd);
	return ret;
}


/*发送文件处理*/
void* on_request() {
    int tmp;
    struct list_head *pos,*n;
    struct file_infor *file_item;
    while (!g_ctx.exit) {
        //test_
        //当文件不够发送的时候 ，等待超时时间，并且发送dummy文件
        //if (g_ctx.file_count == 0) {
		while (g_ctx.file_count <= 0 && g_ctx.dummy_file_path) {
            pthread_mutex_lock(&g_ctx.wait_mutex);
        //test file_cout正常情况下应该设置成0
			struct timeval now;
			struct timespec outtime;
			gettimeofday(&now, NULL);
			outtime.tv_sec = now.tv_sec + g_ctx.send_dummy_interval;
			outtime.tv_nsec = now.tv_usec * 1000;
			if (ETIMEDOUT == pthread_cond_timedwait(&g_ctx.wait_cond, &g_ctx.wait_mutex, &outtime)) {
                    char file_name[1024];
                    memset(file_name,0,sizeof(file_name));

                    copy_dummy_file(g_ctx.dummy_file_path,(char*)file_name);
                    log_debug("wait more file to send,dummy file:%s add to list.",file_name);
            }
            pthread_mutex_unlock(&g_ctx.wait_mutex);
		}

        pthread_mutex_lock(&g_ctx.file_list_mutex);

        list_for_each_safe(pos, n, &g_ctx.head) {

            file_item = list_entry(pos, struct file_infor, list);
            send_file(file_item);
			if (file_item->dummy_flag == 0) 
                g_ctx.sent_timestamp = file_item->timestamp; 
            else 
                remove(file_item->file_path);////dummy数据，删除copy的文件
            log_debug("sendfile file_item=%p,filename=%s,timestamp=%lld,"
                      "sent_timestamp=%lld,fd=%d", file_item, file_item->file_path, 
                       file_item->timestamp,g_ctx.sent_timestamp,file_item->file_fd);

            //remove(file_item->file_path);
            remove_list_file(file_item);
            pthread_cond_signal(&g_ctx.file_list_cond);
            break;
        }
        pthread_mutex_unlock(&g_ctx.file_list_mutex);
    }

}

int main(int argc, char *argv[]) {
    int i;
    struct timeval t_new;
    pthread_t req_tid;
    pthread_t res_tid;
    struct sockaddr_in     serv_addr;

    //命令行参数解析
    int c;
    char *l_opt_arg;

    //config
	char    *config_file, *error, *value;
	char    **hkp, **hash_keys, **values, **keywords, **sections;
	char    **sp, **kp, **vp, *hash_key, *type_s;
	char    *keyword, *section;
	int     cfg_index, type; 


    udp_init();
    while ((c = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {

        switch (c) {
        case 'i':
            l_opt_arg = optarg;
            g_ctx.ip_addr = strdup(l_opt_arg);
            break;
        case 'p':
            l_opt_arg = optarg;
            g_ctx.port = strtoi(l_opt_arg);
            break;
        case 't':
            l_opt_arg = optarg;
            g_ctx.start_wait_interval = ((int64_t)strtoi(l_opt_arg)) * TIME_SCALE;
            break;
        case 'w':
            l_opt_arg = optarg;
            g_ctx.work_dir = strdup(l_opt_arg);
            break;
        default:
            break;
        }

    }

    //config 解析

    cfg_index = cfg_read_config_file( "udpproxy.conf" ) ;

    if ( cfg_error_msg( cfg_index )) {
        error = cfg_error_msg( cfg_index ) ;
    }

    sections = cfg_get_sections( cfg_index ) ;
    for ( sp = sections ; *sp ; sp++ ) {
        section = *sp ;
        //printf( "%s\n", section ) ;
        keywords = cfg_get_keywords( cfg_index, *sp ) ;
        for ( kp = keywords ; *kp ; kp++ ) {
            keyword = *kp ;
            type = cfg_get_type( cfg_index, section, keyword ) ;
            type_s = cfg_get_type_str( cfg_index, section, keyword ) ;
            switch ( type ) {
            case TYPE_SCALAR:
                value = cfg_get_value( cfg_index, section, keyword ) ;
                break ;
            case TYPE_ARRAY:
                values = cfg_get_values( cfg_index, section, keyword ) ;
                for ( vp = values ; *vp ; vp++ ) {
                    //printf( "\t\t\'%s\'\n", *vp ) ;
                }
                break ;
            case TYPE_HASH:
                hash_keys = cfg_get_hash_keys( cfg_index, section, keyword ) ;
                for ( hkp = hash_keys ; *hkp ; hkp++ ) {
                    hash_key = *hkp ;
                    value = cfg_get_hash_value( cfg_index, section, keyword, hash_key ) ;
                    //printf( "\t\t%s = \'%s\'\n", hash_key, value ) ;
                }
                break ;
            }

			if (keyword && value) {
				//如果命令行没有指定,就读取配置文件的值
				if (!strcmp(keyword, "ip") && !g_ctx.ip_addr) 
                    g_ctx.ip_addr = strdup(value);
				else if (!strcmp(keyword, "port") && g_ctx.port == -1) 
                    g_ctx.port = strtoi(value);
				else if (!strcmp(keyword, "work_dir") && !g_ctx.work_dir) 
					g_ctx.work_dir = strdup(value);
				else if (!strcmp(keyword, "log_dir") && !g_ctx.log_dir) {
					char path[1024];
					if (value[strlen(value)] != '/') {
						sprintf(path, "%s/", value);
						path[strlen(path)] = '\0';
					}
					g_ctx.log_dir = strdup(path);
				} else if (!strcmp(keyword, "dummy_file") && !g_ctx.dummy_file_path) 
                    g_ctx.dummy_file_path = strdup(value); 
				else if (!strcmp(keyword,"send_dummy_interval")) 
                    g_ctx.send_dummy_interval = strtoi(value);
				else if (!strcmp(keyword,"start_wait_interval") && 
										(g_ctx.start_wait_interval != 0)) 
					g_ctx.start_wait_interval = strtoi(value);
			}
		}
	}
	if (g_ctx.ip_addr == NULL ||
		g_ctx.port == -1 || g_ctx.work_dir == NULL) {
		printf("ERROR! usage sample:\n");
		printf("./udp -i 192.168.10.18 -p 8888 -t 10000 -w /home \n");
	}
	if (g_ctx.log_dir) {
		log_init(g_ctx.log_dir, LOGGER_ROTATE_BY_SIZE | LOGGER_ROTATE_PER_HOUR, 64);
		log_debug("init log succeed in %s", g_ctx.log_dir);
	} else 
        log_init(".udpproxy", LOGGER_ROTATE_BY_SIZE | LOGGER_ROTATE_PER_HOUR, 64); 
        
    //dupm info
	log_debug("[-----------------dump config begin-----------------------------]");
	if (g_ctx.work_dir) log_debug("work_dir：%s", g_ctx.work_dir);
	if (g_ctx.log_dir) log_debug("log_dir：%s", g_ctx.log_dir);
	if (g_ctx.ip_addr && g_ctx.port)
         log_debug("send to ip：%s,port:%d", g_ctx.ip_addr, g_ctx.port); 
	log_debug("start_wait_interval=%d",g_ctx.start_wait_interval);
	log_debug("send_dummy_interval=%d",g_ctx.send_dummy_interval);
	log_debug("[-----------------dump config end-----------------------------]");
    if (pthread_create(&req_tid, NULL, (void *)event_loop, NULL) != 0) {
        close(g_ctx.sock_fd);
        return -2;
    }

    if (pthread_create(&res_tid, NULL, (void *)on_request, NULL) != 0) {
        close(g_ctx.sock_fd);
        return -2;
    }

    pthread_join(req_tid, NULL);
    pthread_join(res_tid, NULL);

    udp_destroy();
}

