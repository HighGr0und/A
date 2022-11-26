#include <stdio.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/105.0.0.0 Safari/537.36\r\n";

#include "csapp.h"
#include "cache.h"
void doit(int fd);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

void read_requesthdrs(rio_t *rp, char *headers, int clientfd, char *hostname, char *path);
void parse_uri(char *uri, char *hostname, char *port, char *path) ;
void *thread(void *vargp);

Cache cache;
int main(int argc, char **argv)
{
    int listenfd, *connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    Signal(SIGPIPE, SIG_IGN); //ignore the signal

    listenfd = Open_listenfd(argv[1]);

    while (1) {
	clientlen = sizeof(clientaddr);

    connfd = Malloc(sizeof(MAXLINE));

	*connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
	
    Pthread_create(&tid, NULL, thread, connfd);  
    
    }
    return 0;
}

/* Thread routine */
void *thread(void *vargp){
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self()); 
    Free(vargp); 
    doit(connfd);
    Close(connfd);
    return NULL;
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd) 
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE];
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE],port[MAXLINE], path[MAXLINE];
    
    rio_t rio, rp;

    int clientfd;
    char headers[MAXLINE];

    char cache_tag[MAXLINE];  


    /* Read request line*/
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    
    sscanf(buf, "%s %s %s", method, uri, version);       //line:netp:doit:parserequest

    strcpy(cache_tag,uri);

    if (strcasecmp(method, "GET")) {                     //line:netp:doit:beginrequesterr
        clienterror(fd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }                                                    //line:netp:doit:endrequesterr


    /* Parse URI from GET request */
    parse_uri(uri, hostname, port, path);

    /* Connecting to the server*/
    clientfd = Open_clientfd(hostname, port);
    if (clientfd <= 0)
    {
        printf("connection failed\n");
        Close(clientfd);
        return;
    }
    
    Rio_readinitb(&rp, clientfd);

    // Read headers and send to server
    read_requesthdrs(&rio,headers,clientfd,hostname,path);                              //line:netp:doit:readrequesthdrs
   
    char cache_buf[MAX_OBJECT_SIZE];
    int size_buf = 0;

    size_t n;
    while((n = rio_readnb(&rp, buf, MAXLINE)) != 0){
        size_buf += n;
        if(size_buf < MAX_OBJECT_SIZE){
            strcat(cache_buf, buf);
        }
        printf("proxy received %d bytes,then send\n", (int)n);
        Rio_writen(fd, buf, n);
    }
    Close(clientfd);

}

/* Read headers and send to server*/
void read_requesthdrs(rio_t *rp, char *headers, int clientfd, char *hostname, char *path)
{
    char get_hdr[MAXLINE];
    char get_host[MAXLINE];

    sprintf(get_hdr, "GET %s HTTP/1.0\r\n", path);
    sprintf(get_host, "Host: %s\r\n", hostname);

    strcpy(headers, get_hdr);
    strcat(headers, get_host);
    strcat(headers, user_agent_hdr);
    strcat(headers, "Connection: close\n");
    strcat(headers, "Proxy-Connection: close\n");
    strcat(headers, "\r\n");

    rio_writen(clientfd, headers, strlen(headers));

    return;
}

/*parse the uri- hostname, port,path*/
void parse_uri(char *uri, char *hostname, char *port, char *path) 
{
    char *hostptr= Malloc(MAXLINE);
    char *portptr = Malloc(MAXLINE);
    char *pathptr= Malloc(MAXLINE);
   
    hostptr = strstr(uri, "//")+2;  
    pathptr = strchr(hostptr, '/');  

    if (pathptr!=NULL) {
	    strcpy(path, pathptr);
        *pathptr = '\0';
	}else{
        strcpy(path, "/");
    }

    portptr = strchr(hostptr, ':');

    if(portptr != NULL){
        memmove(port,portptr + 1,sizeof(portptr + 1));
        *portptr='\0';
        strcpy(portptr, "");
    }else{
        strcpy(port, "80");
    }
    
    strcpy(hostname, hostptr);
	return;
    
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
