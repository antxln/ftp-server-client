#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>

#define BUFFER_SIZE 1024
#define ERROR 0
#define SUCCESS 1
#define AUTH 2
#define GET 3
#define PUT 4
#define EXIT 5
#define USER "user"
#define PASS "pass"


int port = 7777;
int server_fd = -1;
int n_clients = 0;
int size = 100;
pthread_t *tids = NULL;

struct key {
  int client_fd;
  FILE *fp;
};

// Cleanup process when server shuts down
void cleanup() {
  if (server_fd > 0) close(server_fd);

  if (tids != NULL) {
    int i;
    for (i = 0; i < n_clients; i++) {
      pthread_cancel(tids[i]);
    }
    for (i = 0; i < n_clients; i++) {
      pthread_join(tids[i], NULL);
    }
    free(tids);
  }

  exit(1);
}


// Cleanup process for each client
// Free memory space and close file pointers
void pcleanup(void *tmp) {
  void **argp = (void **) tmp;
  void *arg = *argp;
  if (arg != NULL) {
    struct key *key = (struct key *) arg;
  
    if (key->client_fd > 0) close(key->client_fd);
    if (key->fp != NULL) fclose(key->fp);
    free(arg);
    *argp = NULL;
  }
}


void handler(int signum) {
  if (signum == SIGINT ||
      signum == SIGTERM ||
      signum == SIGQUIT) {
    cleanup();
  }
}

int auth(struct key *key) {
  uint8_t type;
  uint32_t len;
  int client_fd = key->client_fd;
  char buffer[BUFFER_SIZE+1];
  memset(buffer, 0, BUFFER_SIZE+1);

  type = SUCCESS;

  // username
  read(client_fd, &len, sizeof(len));
  len = ntohl(len);
  
  read(client_fd, buffer, len);
  if (strcmp(buffer, USER) != 0) type = EXIT;

  // password
  read(client_fd, &len, sizeof(len));
  len = ntohl(len);
  
  read(client_fd, buffer, len);
  buffer[len] = 0;
  if (strcmp(buffer, PASS) != 0) type = EXIT;

  write(client_fd, &type, sizeof(type));
  return type;
}

void sendfile(struct key *key) {
  uint8_t type;
  uint32_t len;
  int client_fd = key->client_fd;
  char buffer[BUFFER_SIZE+1];
  memset(buffer, 0, BUFFER_SIZE+1);
  
  // file name
  read(client_fd, &len, sizeof(len));
  read(client_fd, &buffer, ntohl(len));

  // open file
  key->fp = fopen(buffer, "rb");
  FILE *fp = key->fp;
  if (fp == NULL) {
    printf("error open file '%s' for read %d <%s>\n", buffer, errno, strerror(errno));
    type = ERROR;
    write(client_fd, &type, sizeof(type));
    return;
  }

  type = PUT;
  write(client_fd, &type, sizeof(type));

  // get file size
  fseek(fp, 0, SEEK_END);
  len = ftell(fp);
  if (len < 0) {
    type = ERROR;
    write(client_fd, &type, sizeof(type));
    return;
  }
  fseek(fp, 0, SEEK_SET);

  // send file
  len = htonl(len);
  write(client_fd, &len, sizeof(len));
  len = ntohl(len);

  while (len > 0) {
    int n = BUFFER_SIZE;
    if (len < BUFFER_SIZE) n = len;
    fread(buffer, 1, n, fp);
    int j = write(client_fd, buffer, n);
    if (j == -1) printf("error write %d <%s>\n", errno, strerror(errno));
    fseek(fp, j-n, SEEK_CUR);
    len = len - j;
  }

  fclose(fp);
  key->fp = NULL;
}

void recvfile(struct key *key) {
  uint32_t len;
  int client_fd = key->client_fd;
  char buffer[BUFFER_SIZE+1];
  memset(buffer, 0, BUFFER_SIZE+1);

  // file name
  read(client_fd, &len, sizeof(len));
  read(client_fd, &buffer, ntohl(len));
  
  // file length
  read(client_fd, &len, sizeof(len));
  len = ntohl(len);

  // open file
  key->fp = fopen(buffer, "wb");
  FILE *fp = key->fp;
  if (fp == NULL) {
    printf("error open file '%s' for write %d <%s>\n", buffer, errno, strerror(errno));
    while (len > 0) {
      int n = BUFFER_SIZE;
      if (len < BUFFER_SIZE) n = len;
      n = read(client_fd, buffer, n);
      if (n == -1) printf("error read %d <%s>\n", errno, strerror(errno));
      len = len - n;
    }
    return;
  }

  while (len > 0) {
    int n = BUFFER_SIZE;
    if (len < BUFFER_SIZE) n = len;
    n = read(client_fd, buffer, n);
    if (n == -1) printf("error read %d <%s>\n", errno, strerror(errno));
    fwrite(buffer, 1, n, fp);
    len = len - n;
  }

  fclose(fp);
  key->fp = NULL;
}

void *handle_client(void *arg) {
  pthread_cleanup_push(pcleanup, &arg);
  struct key *key = (struct key *) arg;
  int client_fd = key->client_fd;
  
  // auth
  int flag = EXIT;
  uint8_t type;
  while (flag != SUCCESS) {
    read(client_fd, &type, sizeof(type));
    switch (type) {
      case AUTH:
        flag = auth(key);
        if (flag != SUCCESS) pthread_cancel(pthread_self());
        break;
      default:
        pthread_cancel(pthread_self());
    }
  }

  while (1) {
    read(client_fd, &type, sizeof(type));
    switch (type) {
      case AUTH:
        break;
      case GET:
        sendfile(key);
        break;
      case PUT:
        recvfile(key);
        break;
      case EXIT:
        pthread_cancel(pthread_self());
        break;
      default:
        pthread_cancel(pthread_self());
    }
  }
  
  pthread_cleanup_pop(0);
  return NULL;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Usage: program ipv4_address\n");
    cleanup();
  }

  struct sockaddr_in server_addr, client_addr;

  char *addr_str = argv[1];

  // Set up signal handler
  struct sigaction act;
  act.sa_handler = handler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;

  if (sigaction(SIGINT, &act, NULL) < 0)
    printf("error sigaction SIGINT %d <%s>\n", errno, strerror(errno));

  // create socket
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    printf("error create socket %d <%s>\n", errno, strerror(errno));
    cleanup();
  }

  // set addr
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  int ret = inet_pton(AF_INET, addr_str, &server_addr.sin_addr);
  if (ret == 0) {
    printf("invalid network address\n");
    cleanup();
  }

  // socket options
  int enable = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
    printf("error set socket options %d <%s>\n", errno, strerror(errno));
    cleanup();
  }

  // bind socket to address
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
    printf("error bind %d <%s>\n", errno, strerror(errno));
    cleanup();
  }

  // listen with a backlog of 128 connections
  if (listen(server_fd, 128) < 0) {
    printf("error listen %d <%s>\n", errno, strerror(errno));
    cleanup();
  }


  tids = malloc(size * sizeof(pthread_t));
  if (tids == NULL) {
    cleanup();
  }
  
  // accept clients
  while (1) {
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr *) &client_fd, &addr_len);

    if (client_fd < 0) {
      printf("error accept client %d <%s>\n", errno, strerror(errno));
      continue;
    }

    struct key *arg = malloc(sizeof(struct key));
    arg->client_fd = client_fd;
    arg->fp = NULL;

    if (n_clients >= size) {
      size = size * 1.5;
      pthread_t *tmp = realloc(tids, size * sizeof(pthread_t));
      if (tmp == NULL) {
        cleanup();
      }
      tids = tmp;
    }
    
    pthread_create(tids+n_clients++, NULL, handle_client, arg);
  }

  cleanup();

  return 0;
}