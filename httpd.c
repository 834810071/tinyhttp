/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
/**
 * 较新版本的 Linux 已经内置了对线程的支持，只需要在编译时添加 -lpthread
 * 参数即可 调整 Makefile 的编译选项，使 $(LIBS) 在第 4 行的最后即可
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ISspace(x) isspace((int)(x))
//函数说明：检查参数c是否为空格字符，
//也就是判断是否为空格(' ')、定位字符(' \t ')、CR(' \r ')、换行(' \n ')、垂直定位字符(' \v ')或翻页(' \f ')的情况。
//返回值：若参数c 为空白字符，则返回非 0，否则返回 0。


#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n" //定义server名称
#define STDIN 0
#define STDOUT 1
#define STDERR 2

#define u_short unsigned short int

void accept_request(void *);    // 处理从套接字上监听到的一个 HTTP 请求
void bad_request(int);          // 返回给客户端这是个错误请求，400响应码
void cat(int, FILE *);          // 读取服务器上某个文件写到 socket 套接字
void cannot_execute(int);       // 处理发生在执行 cgi 程序时出现的错误
void error_die(const char *);   // 把错误信息写到 perror
void execute_cgi(int, const char *, const char *, const char *);    // 运行cgi脚本，这个非常重要，涉及动态解析
int get_line(int, char *, int);     // 读取一行HTTP报文
void headers(int, const char *);    // 返回HTTP响应头
void not_found(int);                // 返回找不到请求文件
void serve_file(int, const char *); // 调用 cat 把服务器文件内容返回给浏览器。
int startup(u_short *);             // 开启http服务，包括绑定端口，监听，开启线程处理链接
void unimplemented(int);            // 返回给浏览器表明收到的 HTTP 请求所用的 method 不被支持。

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void accept_request(void *arg) {
    int client = (intptr_t)arg; // clientfd
    char buf[1024];
    size_t numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0; /* becomes true if server decides this is a CGI
                * program */
    char *query_string = NULL;

    /* 读取第一行，格式类似 "GET / HTTP/1.1" */
    numchars = get_line(client, buf, sizeof(buf));
    i = 0;
    j = 0;
    /* 首先解析 method (GET / POST) */
    // 对于HTTP报文来说，第一行的内容即为报文的起始行，格式为<method> <request-URL> <version>，
    // 每个字段用空白字符相连
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1)) {
        // 提取其中的请求方式是GET还是POST
        method[i] = buf[i];
        i++;
    }
    j = i;
    method[i] = '\0';
    printf("method = %s\n", method);
    /* 不能处理 GET 和 POST 之外的请求类型 */
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        /* 直接返回 501 Method Not Implemented */
        unimplemented(client);
        return;
    }

    /* 若为 POST 请求类型，则认为送给 cgi 程序处理 */
    // cgi为标志位，置1说明开启cgi解析
    if (strcasecmp(method, "POST") == 0) {
        cgi = 1;
    }

    i = 0;
    // 将method后面的后边的空白字符略过
    while (ISspace(buf[j]) && (j < numchars)) {
        j++;
    }
    // 继续读取request-URL
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars)) {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';
    // 如果是GET请求，url可能会带有?,有查询参数
    if (strcasecmp(method, "GET") == 0) {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0')) {
            query_string++;
        }
        if (*query_string == '?') {
            // 如果带有查询参数，需要执行cgi，解析参数，设置标志位为1
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    // url中的路径格式化到path
    sprintf(path, "/home/jxq/CLionProjects/tinyhttp/htdocs%s", url);
    printf("%s\n", path);
    // 如果path只是一个目录，默认设置为首页index.html
    if (path[strlen(path) - 1] == '/') {
        strcat(path, "index.html");
    }

    // 函数定义:    int stat(const char *file_name, struct stat *buf);
    // 函数说明:    通过文件名filename获取文件信息，并保存在buf所指的结构体stat中
    // 返回值:     执行成功则返回0，失败返回-1，错误代码存于errno（需要include <errno.h>）
    /* 判断请求的文件是否存在 */
    if (stat(path, &st) == -1) {
        // 不存在
        /* read & discard headers */
        while ((numchars > 0) && strcmp("\n", buf)) {
            numchars = get_line(client, buf, sizeof(buf));
        }
        not_found(client);
    } else {
        /* 如果为一个目录，则自动寻找 index.html */
        // S_IFMT   0170000    文件类型的位遮罩 // S_IFDIR代表目录
        if ((st.st_mode & S_IFMT) == S_IFDIR) {
            strcat(path, "/index.html");
        }
        // S_IXUSR(S_IEXEC) 00100     文件所有者具可执行权限
        // S_IXGRP 00010             用户组具可执行权限
        // S_IXOTH 00001             其他用户具可执行权限
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) ||
            (st.st_mode & S_IXOTH)) {
            cgi = 1;
        }
        printf("cgi = %d\n", cgi);
        if (!cgi) {
            serve_file(client, path);   // 静态页面请求
        } else {
            execute_cgi(client, path, method, query_string);  // 执行cgi动态解析
        }
    }

    close(client);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client) {
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource) {
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    while (!feof(resource)) {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client) {
    char buf[1024];
    // 500 服务器内部错误
    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc) {
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
// 执行cgi动态解析
void execute_cgi(int client, const char *path, const char *method,
                 const char *query_string) {
    printf("execute_cgi path = %s\n", path);
    char buf[1024];
    int cgi_output[2];  // 声明的读写管道，切莫被名称给忽悠，会给出图进行说明
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A';
    buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0) {
        /* read & discard headers */
        while ((numchars > 0) && strcmp("\n", buf)) {
            numchars = get_line(client, buf, sizeof(buf));  // Host: localhost:42823
        }
        printf("get buf = %s\n", buf);
    } else if (strcasecmp(method, "POST") == 0) { /* POST */
        numchars = get_line(client, buf, sizeof(buf));      // Host: localhost:42823
        //printf("post0 buf = %s\n", buf);
        // 循环读取请求头
        while ((numchars > 0) && strcmp("\n", buf)) {
            buf[15] = '\0';
            //printf("post1 buf = %s\n", buf);
            if (strcasecmp(buf, "Content-Length:") == 0) {
                content_length = atoi(&(buf[16]));
            }
            numchars = get_line(client, buf, sizeof(buf));
            //printf("post2 buf = %s\n", buf);
        }
        if (content_length == -1) {
            bad_request(client);
            return;
        }
    } else { /*HEAD or other*/
    }


    if (pipe(cgi_output) < 0) {
        cannot_execute(client);
        return;
    }
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }

    if ((pid = fork()) < 0) {
        cannot_execute(client);
        return;
    }
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    // #include<unistd.h>
    // int pipe(int filedes[2]);
    // 返回值：成功，返回0，否则返回-1。参数数组包含pipe使用的两个文件的描述符。fd[0]:读管道，fd[1]:写管道。
    // 必须在fork()中调用pipe()，否则子进程不会继承文件描述符。
    // 两个进程不共享祖先进程，就不能使用pipe。但是可以使用命名管道。
    // pipe(cgi_output)执行成功后，cgi_output[0]:读通道 cgi_output[1]:写通道，这就是为什么说不要被名称所迷惑

    /* child: CGI script */
    /**
     * 子进程去执行 cgi 程序
     * 父子进程通过 pipe 管道通信
     * */
    if (pid == 0) {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], STDOUT);    // 复制一个现存的文件描述符。
        dup2(cgi_input[0], STDIN);
        close(cgi_output[0]);
        close(cgi_input[1]);
        // CGI标准需要将请求的方法存储环境变量中，然后和cgi脚本进行交互
        // 存储REQUEST_METHOD
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);   // 配置系统环境变量
        if (strcasecmp(method, "GET") == 0) {
            // 对于 GET 请求，将 GET 请求的 Query String 存入 env 的 QUERY_STRING 字段，
            // 然后 exec 执行 CGI 程序
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            //存储QUERY_STRING
            putenv(query_env);
        } else { /* POST */
            // 对于 POST 请求，将 POST 请求的 Content-Length 存入 env 的 CONTENT_LENGTH 字段，
            // exec 执行 CGI 程序，
            // 然后父进程将 POST 的数据由标准输入传递给 CGI 程序
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        // 表头文件#include<unistd.h>
        // 定义函数
        // int execl(const char * path,const char * arg,....);
        // 函数说明
        // execl()用来执行参数path字符串所代表的文件路径，接下来的参数代表执行该文件时传递过去的argv(0)、argv[1]……，最后一个参数必须用空指针(NULL)作结束。
        // 返回值
        // 如果执行成功则函数不会返回，执行失败则直接返回-1，失败原因存于errno中。
        execl(path, path, NULL);  // /home/jxq/CLionProjects/tinyhttp/htdocs/color.cgi
        exit(0);
    } else { /* parent */
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0) {
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        }
        /* 将 cgi 程序的输出返回到网页 */
        while (read(cgi_output[0], &c, 1) > 0) {
            send(client, &c, 1, 0);
        }

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);
    }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size) {
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n')) {
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0) {
            if (c == '\r') {
                n = recv(sock, &c, 1, MSG_PEEK);    // 仅把tcp buffer中的数据读取到buf中，并不把已读取的数据从tcp buffer中移除
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n')) {
                    recv(sock, &c, 1, 0);
                } else {
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        } else {
            c = '\n';
        }
    }
    buf[i] = '\0';

    return (i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename) {
    char buf[1024];
    (void)filename; /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client) {
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
// 读取文件内容， send 返回给请求的服务端。
void serve_file(int client, const char *filename) {
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A';
    buf[1] = '\0';
    /* read & discard headers */
    while ((numchars > 0) && strcmp("\n", buf)) {
        numchars = get_line(client, buf, sizeof(buf));
    }

    resource = fopen(filename, "r");
    if (resource == NULL) {
        not_found(client);
    } else {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
// 创建套接字连接，绑定端口，返回 socket 连接的描述符。
int startup(u_short *port) {
    int httpd = 0;
    int on = 1;
    struct sockaddr_in name;

    /**
     * socker 创建套接字连接
     * PF_INET \ AF_INET IPv4 因特网域
     * SOCK_STREAM 有序的、可靠的、双向的、面向连接的字节流
     * */
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1) {
        error_die("socket");
    }

    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    /* htons: Host to Network Short */
    name.sin_port = htons(*port);
    /* htonl: Host to Network Long */
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if ((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0) {
        error_die("setsockopt failed");
    }
    /* 如果没有指定端口，则系统会自动指定一个 */
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0) {
        error_die("bind");
    }
    /* 如果没有指定端口，则将系统指定的端口赋值给端口参数 */
    if (*port == 0) {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1) {
            error_die("getsockname");
        }
        *port = ntohs(name.sin_port);
    }
    /* listen 的第二个参数指定了可以排队的请求的个数，多余的会被拒绝 */
    if (listen(httpd, 5) < 0) {
        error_die("listen");
    }
    return (httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client) {
    char buf[1024];
    // 发送501说明相应方法没有实现
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
// 调用 startup 函数初始化连接， while(1) 接收请求，将接收到的信号多线程分发给 accept_request 函数处理
int main(void) {
    int server_sock = -1;
    u_short port = 0;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);
    pthread_t newthread;

    server_sock = startup(&port);   // 初始化连接  socket  bind  listen
    printf("httpd running on port %d\n", port);

    while (1) {
        /* accept 函数获得连接请求并建立连接，返回套接字描述符 */
        client_sock =
                accept(server_sock, (struct sockaddr *)&client_name, &client_name_len);
        if (client_sock == -1) {
            error_die("accept");
        }

        /* 创建新线程处理此次请求 */
        if (pthread_create(&newthread, NULL, (void *)accept_request,
                           (void *)(intptr_t)client_sock) != 0) {
            perror("pthread_create");
        }
    }

    close(server_sock);

    return (0);
}