#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

/* 实现一个简单的多线程 http/1.1 服务器 */

#define SERVER_PORT 80

static int debug = 1;

int get_line(int sock, char* buf, int size);
void* do_http_request(void* pclient_sock);
void do_http_response(int client_sock, const char* path, int status);
int  headers(int client_sock, FILE* resource, int status);
void cat(int client_sock, FILE* resource);


int main(void) {

	int sock;// 负责监听的socket
	struct sockaddr_in server_addr;


	//1.创建上述socket
	sock = socket(AF_INET, SOCK_STREAM, 0);

	//2.清空标签，写上地址和端口号
	bzero(&server_addr, sizeof(server_addr));

	server_addr.sin_family = AF_INET;//选择协议族IPV4
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);//监听本地所有IP地址
	server_addr.sin_port = htons(SERVER_PORT);//绑定端口号

	// 绑定上述socket与ip:port
	bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));

	// 开始监听
	listen(sock, 128);

	printf("等待客户端的连接......\n");

	int done = 1;

	while (done) {
		struct sockaddr_in client; // 客户端 ip:port
		int client_sock, len, i;
		char client_ip[64];
		char buf[256];

		pthread_t id;
		int* pclient_sock = NULL;

		socklen_t  client_addr_len;
		client_addr_len = sizeof(client);
		// 客户端socket
		client_sock = accept(sock, (struct sockaddr*)&client, &client_addr_len);

		//打印客服端IP地址和端口号
		printf("client ip: %s\t port : %d\n",
			inet_ntop(AF_INET, &client.sin_addr.s_addr, client_ip, sizeof(client_ip)),
			ntohs(client.sin_port));
		
		// 处理http 请求,读取客户端发送的数据  
		//do_http_request(client_sock);

		// 启动线程处理http请求
		pclient_sock = (int*)malloc(sizeof(int));
		*pclient_sock = client_sock;

		pthread_create(&id, NULL, do_http_request, (void*)pclient_sock);
		
		if (debug) {
			printf("\n******************\n");
			printf("\ncurrent thread id: %d\n", id);
			printf("\n******************\n");
		}
		
		pthread_detach(id);	// 将线程分离,结束后自动释放资源

		//close(client_sock);

	}
	close(sock); // 关闭服务器监听socket
	return 0;
}


void* do_http_request(void* pclient_sock) {
	int len = 0;
	char buf[256];
	char method[64];
	char url[256];
	char prot_ver[64];
	char path[256];

	int client_sock = *(int*)pclient_sock;

	struct stat  st;


	/*读取客户端发送的http 请求*/


	//1.读取报文请求行
	len = get_line(client_sock, buf, sizeof(buf));

	if (len > 0) {//读到了请求行
		int i = 0, j = 0;
		while (!isspace(buf[j]) && (i < sizeof(method) - 1)) {
			method[i] = buf[j];
			i++;
			j++;
		}

		method[i] = '\0';
		if (debug) printf("request method: %s\n", method);

		if (strncasecmp(method, "GET", i) == 0) { //只处理get请求
			if (debug) printf("method = GET\n");

			//获取url
			while (isspace(buf[j++]));//跳过空格
			i = 0;

			while (!isspace(buf[j]) && (i < sizeof(url) - 1)) {
				url[i] = buf[j];
				i++;
				j++;
			}

			url[i] = '\0';

			if (debug) printf("url: %s\n", url);

			//获取协议版本
			while (isspace(buf[j++]));//跳过空格
			i = 0;

			--j;

			while (!isspace(buf[j]) && (i < sizeof(prot_ver) - 1)) {
				prot_ver[i] = buf[j];
				i++;
				j++;
			}

			prot_ver[i] = '\0';

			if (debug) printf("protocol: %s\n", prot_ver);

			//继续读取http 头部
			do {
				len = get_line(client_sock, buf, sizeof(buf));
				if (debug) printf("read: %s\n", buf);

			} while (len > 0);

			//***定位服务器本地的html文件***

			//处理url 中的?
			{
				char* pos = strchr(url, '?');
				if (pos) {
					*pos = '\0';
					printf("real url: %s\n", url);
				}
			}

			sprintf(path, "./html_docs/%s", url);
			//path[sizeof(path)]
			if (debug) printf("path: %s\n", path);

			//执行http 响应
			//判断文件是否存在，如果存在就响应200 OK，同时发送相应的html 文件,如果不存在，就响应 404 NOT FOUND.
			if (stat(path, &st) == -1) {//文件不存在或是出错
				fprintf(stderr, "stat %s failed. reason: %s\n", path, strerror(errno));
				//not_found(client_sock);
				do_http_response(client_sock, "./html_docs/not_found.html", 404);
			}
			else {//文件存在

				// 若url为目录，则显示对应目录里的index页
				if (S_ISDIR(st.st_mode)) {
					strcat(path, "/index.html");
				}

				do_http_response(client_sock, path, 200);

			}
		}
		else {//非get请求, 读取http 头部，并响应客户端 501 	Method Not Implemented
			fprintf(stderr, "warning! other request [%s]\n", method);
			do {
				len = get_line(client_sock, buf, sizeof(buf));
				if (debug) printf("read: %s\n", buf);

			} while (len > 0);

			do_http_response(client_sock, "./html_docs/unimplemented.html", 501);

		}

	}
	else {//请求格式有问题，bad_request
		
		do_http_response(client_sock, "./html_docs/bad_request.html", 400);
	}

	close(client_sock);
	if (pclient_sock) {
		free(pclient_sock);
	}

	return NULL;

}


void do_http_response(int client_sock, const char* path, int status) {
	int ret = 0;
	FILE* resource = NULL;

	resource = fopen(path, "r"); //打开文件

	if (resource == NULL) {
		//404
		do_http_response(client_sock, "./html_docs/not_found.html", 404);
		return;
	}

	//1.发送http 头部
	ret = headers(client_sock, resource, status);

	//2.发送http body .
	if (!ret) {
		cat(client_sock, resource);
	}

	fclose(resource);
}

/****************************
 *返回关于响应文件信息的http 头部
 *输入：
 *     client_sock - 客服端socket 句柄
 *     resource    - 文件的句柄
 *     status      - 相应状态 200 400 404 500 501
 *返回值： 成功返回0 ，失败返回-1
******************************/
int  headers(int client_sock, FILE* resource, int status) {
	struct stat st;
	int fileid = 0;
	char tmp[64];
	char buf[1024] = { 0 };

	switch (status)
	{
	case 200:
		strcpy(buf, "HTTP/1.1 200 OK\r\n");
		break;
	case 400:
		strcpy(buf, "HTTP/1.1 400 Bad Request\r\n");
		break;
	case 404:
		strcpy(buf, "HTTP/1.1 404 NOT FOUND\r\n");
		break;
	case 500:
		strcpy(buf, "HTTP/1.1 500 Internal Sever Error\r\n");
		break;
	case 501:
		strcpy(buf, "HTTP/1.1 501 NOT IMPLEMENTED\r\n");
		break;
	default:
		strcpy(buf, "HTTP/1.1 defualt\r\n");
		break;
	}

	strcat(buf, "Server: AgNO2 Server\r\n");
	strcat(buf, "Content-Type: text/html\r\n");
	strcat(buf, "Connection: keep-alive\r\n");

	fileid = fileno(resource);

	if (fstat(fileid, &st) == -1) {
		//inner_error(client_sock);
		do_http_response(client_sock, "./html_docs/inner_error.html", 500);
		return -1;
	}

	snprintf(tmp, 64, "Content-Length: %ld\r\n\r\n", st.st_size); // \r\n\r\n 表示报头结束
	strcat(buf, tmp);

	if (debug) fprintf(stdout, "header: %s\n", buf);

	if (send(client_sock, buf, strlen(buf), 0) < 0) {
		fprintf(stderr, "send header failed. data: %s, reason: %s\n", buf, strerror(errno));
		return -1;
	}

	return 0;
}

/****************************
 *说明：实现将html文件的内容按行
		读取并送给客户端
 ****************************/
void cat(int client_sock, FILE* resource) {
	char buf[1024];

	fgets(buf, sizeof(buf), resource);

	while (!feof(resource)) {
		int len = write(client_sock, buf, strlen(buf)); //本质为连续发送char(1 Byte)，故无需换大端序

		if (len < 0) {//发送body 的过程中出现问题,怎么办？1.重试？ 2.
			fprintf(stderr, "send body error. reason: %s\n", strerror(errno));
			break;
		}

		if (debug) fprintf(stdout, "%s", buf);
		fgets(buf, sizeof(buf), resource);

	}
}

//返回值： -1 表示读取出错， 等于0表示读到一个空行， 大于0 表示成功读取一行
int get_line(int sock, char* buf, int size) {
	int count = 0;
	char ch = '\0';
	int len = 0;


	while ((count < size - 1) && ch != '\n') {
		len = read(sock, &ch, 1); // 读取一个字符

		if (len == 1) {
			if (ch == '\r') {
				continue;
			}
			else if (ch == '\n') {
				//buf[count] = '\0';
				break;
			}

			//这里处理一般的字符
			buf[count] = ch;
			count++;

		}
		else if (len == -1) {//读取出错
			perror("read failed");
			count = -1;
			break;
		}
		else {// read 返回0,客户端关闭sock 连接.
			fprintf(stderr, "client close.\n");
			count = -1;
			break;
		}
	}

	if (count >= 0) buf[count] = '\0';

	return count;
}
