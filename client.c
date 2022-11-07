#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>

#define BUFFER_SIZE 1024
#define ERROR 0
#define SUCCESS 1
#define AUTH 2
#define GET 3
#define PUT 4
#define EXIT 5


int port = 7777;
int sock_fd = -1;
char *line = NULL;
FILE *fp = NULL;


// Close file descriptors and exit
void cleanup(int n) {
  if (sock_fd > 0) close(sock_fd);
  if (line != NULL) free(line);
  if (fp != NULL) fclose(fp);

  exit(n);
}

void texit() {
  // not authenticated
  if (sock_fd < 0) return;

  uint8_t type = EXIT;
  write(sock_fd, &type, sizeof(type));
  close(sock_fd);
  sock_fd = -1;
}

void handler(int signum) {
  if (signum == SIGINT ||
      signum == SIGTERM ||
      signum == SIGQUIT) {
    texit();
    cleanup(0);
  }
}

void tconnect(char *addr_str, char *user, char *pass) {
  uint8_t type;
  uint32_t len;
  int ret;
  struct sockaddr_in server_addr;

  // set server address
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  ret = inet_pton(AF_INET, addr_str, &server_addr.sin_addr);
  if (ret == 0) return;
  
  // create socket
  sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (sock_fd < 0) {
    printf("error create socket %d <%s>\n", errno, strerror(errno));
    cleanup(1);
  }
  
  ret = connect(sock_fd, (struct sockaddr *) &server_addr, sizeof(server_addr));
  if (ret < 0) {
    printf("error connect %d <%s>\n", errno, strerror(errno));
    cleanup(1);
  }

  // auth
  type = AUTH;
  write(sock_fd, &type, sizeof(type));

  len = strlen(user);
  len = htonl(len);
  write(sock_fd, &len, sizeof(len));
  write(sock_fd, user, strlen(user));

  len = strlen(pass);
  len = htonl(len);
  write(sock_fd, &len, sizeof(len));
  write(sock_fd, pass, strlen(pass));
  
  // response
  read(sock_fd, &type, sizeof(type));
  if (type != SUCCESS) texit();
}

void tget(char *fname) {
  // not authenticated
  if (sock_fd < 0) return;

  char buffer[BUFFER_SIZE+1];
  memset(buffer, 0, BUFFER_SIZE+1);

  // send command
  uint8_t type = GET;
  uint32_t len = strlen(fname);
  len = htonl(len);
  write(sock_fd, &type, sizeof(type));
  write(sock_fd, &len, sizeof(len));
  write(sock_fd, fname, ntohl(len));

  // get response
  read(sock_fd, &type, sizeof(type));
  if (type == ERROR) {
    printf("error get file\n");
    return;
  }
  read(sock_fd, &len, sizeof(len));
  len = ntohl(len);

  // open file
  fp = fopen(fname, "wb");
  if (fp == NULL) {
    printf("error open file %d <%s>\n", errno, strerror(errno));
    while (len > 0) {
      int n = BUFFER_SIZE;
      if (len < BUFFER_SIZE) n = len;
      n = read(sock_fd, buffer, n);
      if (n == -1) printf("error read %d <%s>\n", errno, strerror(errno));
      len = len - n;
    }
    return;
  }

  // write to file
  while (len > 0) {
    int n = BUFFER_SIZE;
    if (len < BUFFER_SIZE) n = len;
    n = read(sock_fd, buffer, n);
    if (n == -1) printf("error read %d <%s>\n", errno, strerror(errno));
    fwrite(buffer, 1, n, fp);
    len = len - n;
  }

  fclose(fp);
  fp = NULL;
}

void tput(char *fname) {
  // not authenticated
  if (sock_fd < 0) return;

  uint8_t type;
  uint32_t len, fsize;

  // open file
  fp = fopen(fname, "rb");
  if (fp == NULL) {
    printf("error open file %d <%s>\n", errno, strerror(errno));
    return;
  }

  // get file size
  fseek(fp, 0, SEEK_END);
  fsize = ftell(fp);
  if (fsize < 0) return;
  fseek(fp, 0, SEEK_SET);

  // send command
  type = PUT;
  len = strlen(fname);
  len = htonl(len);
  write(sock_fd, &type, sizeof(type));
  write(sock_fd, &len, sizeof(len));
  write(sock_fd, fname, ntohl(len));
  
  // send file
  len = htonl(fsize);
  write(sock_fd, &len, sizeof(len));
  len = fsize;
  char buffer[BUFFER_SIZE+1];
  memset(buffer, 0, BUFFER_SIZE+1);

  while (len > 0) {
    int n = BUFFER_SIZE;
    if (len < BUFFER_SIZE) n = len;
    fread(buffer, 1, n, fp);
    int j = write(sock_fd, buffer, n);
    if (j == -1) printf("error write %d <%s>\n", errno, strerror(errno));
    fseek(fp, j-n, SEEK_CUR);
    len = len - j;
  }

  fclose(fp);
  fp = NULL;
}

int main(int argc, char *argv[]) {
  // Set up signal handler
  struct sigaction act;
  act.sa_handler = handler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;

  if (sigaction(SIGINT, &act, NULL) < 0)
    printf("error sigaction SIGINT %d <%s>\n", errno, strerror(errno));

  size_t len = 0;
  ssize_t nread;
  while ((nread = getline(&line, &len, stdin)) != -1) {
    line[--nread] = 0;
    
    int i;
    int args = 0;
    int offsets[3];
    for (i = 0; i < nread; i++) {
      if (line[i] == ' ') {
        line[i] = 0;
        offsets[args++] = i+1;
        if (args >= 3) break;
      }
    }
    
    if (strcmp(line, "connect") == 0 && args == 3) tconnect(line+offsets[0], line+offsets[1], line+offsets[2]);
    else if (strcmp(line, "get") == 0 && args == 1) tget(line+offsets[0]);
    else if (strcmp(line, "put") == 0 && args == 1) tput(line+offsets[0]);
    else if (strcmp(line, "exit") == 0) {
      if (sock_fd > 0) texit();
      else break;
    }
  }
  
  cleanup(0);

  return 0;
}
