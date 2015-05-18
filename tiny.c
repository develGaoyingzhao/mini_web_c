/*
 * tiny_web - a mini simple web program.
 */
#include "csapp.h"

void sigchld_handler(int sig)
{
	while (waitpid(-1, 0, WNOHANG) > 0)
		;
	return;
}

void doit(int fd);

/* main - an C/S module. */
int main(int argc, char **argv)
{
	struct sockaddr_in cliaddr;
	int listenfd, connfd, port;
	socklen_t clilen;
	pid_t childpid;

	if(argc != 2){
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}
	port = atoi(argv[1]);

	Signal(SIGCHLD, sigchld_handler);	
	listenfd = Open_listenfd(port);
	while(1){
		clilen = sizeof(cliaddr);
		connfd = Accept(listenfd, (SA *) &cliaddr, &clilen);
		if( (childpid = Fork()) == 0){
			Close(listenfd);
			doit(connfd);
			Close(connfd);
			exit(0);
		}
		Close(connfd);
	}
}


/* doit - deal with the an http transcation. */
void serve_static(int fd, char *filename, int filesize);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *str, char *err_code, char *err_name, char *err_message);
int prase_uri(char *uri, char *filename, char *cgiargs);
void read_header(rio_t *rp);

void doit(int fd)
{
	int is_static;	/* static content or dynamic content. */
	char method[MAXLINE], uri[MAXLINE], version[MAXLINE], buf[MAXLINE];
	char filename[MAXLINE], cgiargs[MAXLINE];
	struct stat sbuf;
	rio_t rio;	/* be used by the Rio_readinitb to attach this buff to the fd file. Or we called it a read buff. */

	/*Read request line and headers */
	Rio_readinitb(&rio, fd);
	Rio_readlineb(&rio, buf, MAXLINE); /* read a line from the rio_buff to the buff. */
	sscanf(buf, "%s %s %s", method, uri, version);
	if(strcasecmp(method, "GET")){
		clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
		return;
	}
	read_header(&rio);

	/* Prase URI from GET request */
	is_static = prase_uri(uri, filename, cgiargs);	/* when static return 1, dynamic content return 0. */
	if(stat(filename, &sbuf) < 0){
		clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
		return;
	}
	if(is_static){	/* Serve static content */
		if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))	/*S_ISDEG determine a file's type is regular file or not.*/
										/* S_IRUSR determine the user does have the access right or not.*/
		{	clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read this file");
			return;
		}
		serve_static(fd, filename, sbuf.st_size);
	}
	else {	/*Serve dynamic content */
		if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
			clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
			return;
		}
		serve_dynamic(fd, filename, cgiargs);
	}
}
	
void clienterror(int fd, char *str, char *err_code, char *err_name, char *err_message)
{
	char buff[MAXLINE];

	sprintf(buff, "%s\n%s, %s, %s\n", str, err_code, err_name, err_message);
	Rio_writen(fd, buff, strlen(buff));
}

/* read_header - read the header from the read buff pointered by the rp but ignore it. */
void read_header(rio_t *rp)
{
	char buf[MAXLINE];

	Rio_readlineb(rp, buf, MAXLINE);
	while(strcmp(buf, "\r\n")) {	/* if the buf is "\r\n", return 0. */
		Rio_readlineb(rp, buf, MAXLINE);
		printf("%s", buf);
	}
	return;
}

/* prase_uri - turn the URI to the filename and CGI program's argument. */
int prase_uri(char *uri, char *filename, char *cgiargs)
{
	char *ptr;

	if(!strstr(uri, "cgi-bin")){	/* static content */ /* the strstr function find the cgi-bin 's location in the uri. */
		strcpy(cgiargs, "");	/* empty the cgiargs */
		strcpy(filename, ".");	/* line 101-102 transfer the uri to the unix style file location. */
		strcat(filename, uri);
		if(uri[strlen(uri) - 1] == '/')	/* auto add the default file if not assigned. */
			strcat(filename, "home.html");
		return 1;
	}
	else{				/* dynamic content */
		ptr = index(uri, '?');	/* find the '?' */
		if(ptr){		/* if the prt not a empty string */
			strcpy(cgiargs, ptr + 1);
			*ptr = '\0';
		}
		else
			strcpy(cgiargs, "");	/* transfer the uri to the unix style file location. */
		strcpy(filename, ".");
		strcat(filename, uri);
		return 0;
	}
}

/*
 * serve_static - serve for an request of static content.
 */
void get_filetype(char *filename, char *filetype);

void serve_static(int fd, char *filename, int filesize)
{
	int srcfd;
	char *srcp, filetype[MAXLINE], buf[MAXBUF];

	/* Send response headers to client */
	get_filetype(filename, filetype);
	sprintf(buf, "HTTP/1.0 200 OK\r\nServer: Tiny Web Server\r\nContent-length:%d\r\nContent-type:%s\r\n\r\n", filesize, filetype);
	Rio_writen(fd, buf,strlen(buf));

	/* Send response body to client */
	srcfd = Open(filename, O_RDONLY, 0);
	srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
	Close(srcfd);
	Rio_writen(fd, srcp, filesize);
	Munmap(srcp, filesize);
	exit(0);
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype)
{
	if (strstr(filename, ".html"))
		strcpy(filetype, "text/html");
	else if (strstr(filename, ".gif"))
		strcpy(filetype, "image/gif");
	else if (strstr(filename, ".jpg"))
		strcpy(filetype, "image/jpg");
	else
		strcpy(filetype, "text/plain");
}

/*
 * serve_dynamic - serve for an request of dynamic content.
 */
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
	char buf[MAXLINE], *arg[] = { NULL };

	/* Return first part of HTTP response */	
	sprintf(buf, "HTTP/1.0 200 OK\r\nServer: Tiny Web Server\r\n");
	Rio_writen(fd, buf,strlen(buf));

	/* Send response body to client */
	if (Fork() == 0) {
		/* Real server would set all CGI vars here */
		setenv("QUERY_STRING", cgiargs, 1);
		Dup2(fd, STDOUT_FILENO);
		Execve(filename, arg, environ);
	}
	Wait(NULL);
}
