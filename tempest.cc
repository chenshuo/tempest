#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
//#include <readline/readline.h>
//#include <readline/history.h>

#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
const bool DEBUG = false;

void fatal(const char* msg) __attribute__ ((noreturn));
void fatal(const char* msg)
{
  perror(msg);
  abort();
}

int acceptOne(uint16_t port)
{
  return -1;
}

int connectHost(const char* ip, uint16_t port)
{
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
    fatal("socket error");
  struct sockaddr_in servaddr;
  bzero(&servaddr, sizeof servaddr);
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0)
    fatal("inet_pton error");
  if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof servaddr) < 0)
    fatal("connect error");
  return sockfd;
}

vector<string> getline()
{
    char* line = NULL;
    size_t dumy;
    ssize_t len = getline(&line, &dumy, stdin);
    vector<string> result;
    if (len <= 0) {
      result.push_back("q");
    } else {
      istringstream iss(string(line, len));
      istream_iterator<string> begin(iss);
      istream_iterator<string> end;
      copy(begin, end, back_inserter(result));
    }
    free(line);
    if (result.empty())
      result.push_back("");
    if (DEBUG) {
      for (size_t i = 0; i < result.size(); ++i) {
        printf("'%s', ", result[i].c_str());
      }
      puts("");
    }
    return result;
}

void doRead(int sockfd, const vector<string>& line)
{
  char buf[1024] = {0};
  size_t len = sizeof buf;
  if (line.size() > 1) {
    len = atoi(line[1].c_str());
  }
  printf("reading %zu buf ... ", len);
  fflush(stdout);
  ssize_t nread = ::read(sockfd, buf, len);
  int saved = errno;
  printf("read %zd bytes", nread);
  if (nread < 0)
    printf(", %d - %s\n", saved, strerror(saved));
  else
    printf("\n");
}

void setNonblock(int sockfd, bool on)
{
  int flags = ::fcntl(sockfd, F_GETFL, 0);
  if (on)
    flags |= O_NONBLOCK;
  else
    flags &= ~O_NONBLOCK;
  if (::fcntl(sockfd, F_SETFL, flags) < 0)
    perror("fcntl error");
}

void run(int sockfd)
{
  bool finish = false;
  while (!finish) {
    vector<string> line = getline();
    if (line[0] == "q") {
      finish = true;
    } else if (line[0] == "b") {
      setNonblock(sockfd, false);
    } else if (line[0] == "nb") {
      setNonblock(sockfd, true);
    } else if (line[0] == "r") {
      doRead(sockfd, line);
    } else if (line[0] == "c") {
      if (::close(sockfd) < 0)
        perror("close error");
    }
  }
}

int main(int argc, char* argv[])
{
  bool serverMode = false;

  if (argc > 1)
    serverMode = (strcmp(argv[1], "-s") == 0);
  else {
    printf("Usage: %s [-s] [host_ip]\n", argv[0]);
    return 0;
  }

  int sockfd;
  if (serverMode)
    sockfd = acceptOne(2000);
  else
    sockfd = connectHost(argv[1], 2000);

  run(sockfd);
}

