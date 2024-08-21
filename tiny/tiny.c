/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd; //listenfd : 서버가 클라이언트의 연결 요청을 대기하는 소켓의 파일 디스크립터, connfd : 클라이언트의 연결 요청을 수락한 후 생성되는 연결된 소켓의 파일 디스크립터.
  char hostname[MAXLINE], port[MAXLINE]; //hostname : 연결된 클라이언트의 호스트 이름을 저장할 문자열, port : 연결된 클라이언트의 포트 번호를 저장할 문자열.
  socklen_t clientlen; //클라이언트 주소 구조체의 크기를 나타낸다.
  struct sockaddr_storage clientaddr; // 클라이언트의 주소 정보를 담는 구조체.

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);

  while (1) { 
    clientlen = sizeof(clientaddr); 
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port); 
    doit(connfd);   // line:netp:tiny:doit / 
    Close(connfd);  // line:netp:tiny:close / 
  }
}
/* 오류가 있는지, 컨텐츠가 정적인지 동적인지 확인하고 읽기/실행*/
void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, fd); //읽기 버퍼를 초기화. fd는 클라이언트와 연결된 파일 디스크립터이며, 이를 사용해 데이터를 읽을 준비를 한다.
  Rio_readlineb(&rio, buf, MAXLINE); //클라이언트 요청의 첫번째 줄(요청 라인)을 읽어 buf에 저장.
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); 
  if(strcasecmp(method,"GET")) 
  { //strcasecmp는 대소문자를 구분하지 않고 문자열을 비교한다. 
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  } // 메소드가 "GET"인지 확인 후 "GET"이 아니라면 501 Not implemented 오류를 클라이언트에게 전송 후 함수 종료.
  read_requesthdrs(&rio);

  /* URI에서 데이터 추출 */
  is_static = parse_uri(uri, filename, cgiargs); // URI를 파싱하여 파일의 경로(filename)와 GCI인자(cgiargs)를 분리한다. 또한 요청된 리소스가 정적인지 동적인지를 결정한다.

  if(stat(filename, &sbuf) < 0) //파일의 상태정보를 가져오는 함수. filename으로 지정된 파일의 정보를 sbuf구조체에 저장.
  {
    clienterror(fd, filename, "404", "Not implemented", "Tiny couldn't find this file");
    return;
  }

  if (is_static) //정적 컨텐츠라면.
  {
    /*
    일반 파일인지, 실행 권한이 있는지 확인
    S_ISREG : st_mode 값이 일반 파일(regular file)인지 확인하는 매크로
    sbuf에는 파일의 메타데이터가 들어가 있음. 파일의  권한, 파일 유형에 대한 정보를 비트 플래그로 저장함.
    S_IRUSR : 파일의 소유자가 파일을 읽을 수있는 권한을 확인하기 위한 상수 */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read this file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size);
  }
  else //동적 컨텐츠 였다면
  {
    /* 일반 파일인지, 실행 권한이 있는지 확인 */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs);
  }
  // •	is_static이 참이면 정적 콘텐츠(파일)를 제공하고, 그렇지 않으면 동적 콘텐츠(CGI 프로그램)를 제공합니다.
	// •	정적 콘텐츠의 경우, 파일이 정상적인 파일인지(S_ISREG), 그리고 읽기 권한이 있는지(S_IRUSR) 확인합니다. 만약 조건을 만족하지 않으면 403 Forbidden 오류를 반환합니다.
	// •	동적 콘텐츠의 경우, 파일이 정상적인 실행 파일인지(S_ISREG), 그리고 실행 권한이 있는지(S_IXUSR) 확인합니다. 조건이 만족하지 않으면 403 Forbidden 오류를 반환합니다.
	// •	정적 콘텐츠는 serve_static 함수를 통해 클라이언트에게 전송되고, 동적 콘텐츠는 serve_dynamic 함수를 통해 실행된 후 그 결과가 클라이언트에게 전송됩니다.
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
// 서버가 클라이언트의 요청을 처리하는 중에 오류가 발생하면 클라이언트에게 오류 메시지를 전달하기 위한 함수
{
  char buf[MAXLINE], body[MAXBUF];
  /* http 응답 본문 구축*/
  sprintf(body, "<html><title> Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n",body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "content-type : text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
  //fd가뭐야? 누구의 fd일까?
}

void read_requesthdrs(rio_t *rp) //필요없다. 나는 서버니까.
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")) {
      Rio_readlineb(rp, buf, MAXLINE);
      printf("%s", buf);
    }
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  if (!strstr(uri, "cgi-bin"))//정적컨텐츠를 요청했다.
  {
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri)-1] == '/')
      strcat(filename, "home.html");
    return 1;
  }
  else //동적 컨텐츠를 요청
  {
    ptr = index(uri, '?');
    if(ptr) {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    }
    else 
      strcpy(cgiargs,"");
    strcpy(filename,".");
    strcat(filename,uri);
    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];
  //메타 데이터 , 헤더정보를 보내고
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer : Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n",buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response header:\n");
  printf("%s", buf);
  //실질적인 데이터를 보낸다
  srcfd = Open(filename, O_RDONLY, 0);
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // Mmap = filesize만큼 공간할당하고 srcfd의 데이터를 공간에 넣고 그 공간에 대한 포인터를 srcp에게 반환함.
  srcp = Malloc(filesize);
  Rio_readn(srcfd, srcp, filesize);  //
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  // Munmap(srcp, filesize);
  Free(srcp);
}

void get_filetype(char *filename,char *filetype)
{
  if(strstr(filename, ".html"))
    strcpy(filetype, "text/html");

  else if(strstr(filename, ".gif"))
    strcpy(filetype, "image/git");

  else if(strstr(filename, ".png"))
    strcpy(filetype, "image.png");

  else if(strstr(filename, ".jpg"))
    strcpy(filetype, "image.jpeg");

  else if(strstr(filename, ".MPG"))
    strcpy(filetype, "video.MPG");

  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = { NULL };
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if(Fork() == 0) 
  //프로세스가 진행중에 fork()함수를 만나면 그 지점에서 자식이 생겨 같은 시고토를 진행.

  {
    setenv("QUERY_STRING", cgiargs, 1);
    Dup2(fd, STDOUT_FILENO);
    Execve(filename, emptylist, environ);
  }
  Wait(NULL);
}
//cgi 내가 안하고 다른 프로세스한테 일을 시킴.