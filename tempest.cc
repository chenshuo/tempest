#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
const bool DEBUG = false;
const char* g_host;
uint16_t g_port = 2000;
int g_listenfd;
bool g_serverMode = false;

void fatal(const char* msg) __attribute__ ((noreturn));
void fatal(const char* msg)
{
  perror(msg);
  abort();
}

void printflush(const char* fmt, ...) __attribute__ ((__format__ (__printf__, 1, 2)));
void printflush(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  fflush(stdout);
}

void brokenPipe(int /*sig*/)
{
  write(1, "EPIPE\n", 6);
}

void ctrl_c(int /*sig*/)
{
  write(1, "\n", 1);
}

void initServer(uint16_t port)
{
  g_listenfd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (g_listenfd < 0)
    fatal("socket error");
  int one = 1;
  ::setsockopt(g_listenfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in servaddr;
  bzero(&servaddr, sizeof servaddr);
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(port);
  servaddr.sin_addr.s_addr = INADDR_ANY;
  if (::bind(g_listenfd, (struct sockaddr*)&servaddr, sizeof servaddr) < 0)
    fatal("bind error");
  if (::listen(g_listenfd, 5) < 0)
    fatal("listen error");
}

int acceptOne()
{
  printflush("accepting ... ");
  int sockfd;
  while ( (sockfd = accept(g_listenfd, NULL, NULL)) < 0)
    perror("accept error");
  printf("accepted\n");
  return sockfd;
}

void connectHost(int sockfd)
{
  struct sockaddr_in servaddr;
  bzero(&servaddr, sizeof servaddr);
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(g_port);
  if (inet_pton(AF_INET, g_host, &servaddr.sin_addr) <= 0)
    fatal("inet_pton error");
  printflush("connecting ... ");
  if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof servaddr) < 0)
    perror("connect error");
  else
    printf("connected\n");
}

vector<string> getline()
{
  char* line = ::readline("> ");
  vector<string> result;
  if (line == NULL) {
    result.push_back("q");
  } else {
    istringstream iss(line);
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
  vector<char> buf;
  buf.resize(1024);
  if (line.size() > 1) {
    int len = atoi(line[1].c_str());
    buf.resize(len);
  }
  printflush("reading %zu buf ... ", buf.size());
  ssize_t n = ::read(sockfd, &*buf.begin(), buf.size());
  int saved = errno;
  printf("read %zd bytes", n);
  if (n < 0)
    printf(", %d - %s\n", saved, strerror(saved));
  else
    printf("\n");
}

void doWrite(int sockfd, const vector<string>& line)
{
  string buf = "H";
  if (line.size() > 1) {
    string arg = line[1];
    if (isdigit(arg[0])) {
      int len = atoi(arg.c_str());
      buf.assign(len, 'H');
    } else {
      buf = arg;
    }
  }
  printflush("writing %zu bytes ... ", buf.size());
  ssize_t n = ::write(sockfd, buf.c_str(), buf.size());
  int saved = errno;
  printf("wrote %zd bytes", n);
  if (n < 0)
    printf(", %d - %s\n", saved, strerror(saved));
  else
    printf("\n");
}

void doPoll(int sockfd, const vector<string>& line)
{
  struct pollfd pfd;
  pfd.fd = sockfd;
  pfd.events = POLLIN | POLLPRI | POLLRDHUP | POLLOUT;
  pfd.revents = 0;
  int timeout = 0;
  if (line.size() > 1) {
    timeout = atoi(line[1].c_str()) * 1000;
  }
  printflush("polling %d ms ... ", timeout);
  int n = ::poll(&pfd, 1, timeout);
  if (n > 0) {
    printf("%d events\n", n);
  } else if (n == 0)
    printf("time out\n");
  else
    perror("poll error");
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

void setsignal(int sig, void (*func)(int))
{
  struct sigaction act;
  act.sa_handler = func;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  if (sigaction(sig, &act, NULL) < 0)
    fatal("sigaction");
}

void help()
{
  puts(
      " ?     - help\n"
      " c     - close\n"
      " rc    - re-connect/re-accept\n"
      " r     - read\n"
      " r N   - read N bytes\n"
      " w     - write 1 byte\n"
      " w N   - write N bytes\n"
      " w str - write string str\n"
      " p     - poll\n"
      " st    - status\n"
      " b     - set blocking\n"
      " nb    - set non-blocking\n"
      " d     - set delay\n"
      " nd    - set no-delay\n"
      );
}

void run(int sockfd)
{
  bool finish = false;
  while (!finish) {
    vector<string> line = getline();
    string cmd = line[0];
    if (cmd == "?") {
      help();
    } else if (cmd == "q") {
      finish = true;
    } else if (cmd == "b") {
      setNonblock(sockfd, false);
    } else if (cmd == "nb") {
      setNonblock(sockfd, true);
    } else if (cmd == "c") {
      if (::close(sockfd) < 0)
        perror("close error");
    } else if (cmd == "rc") {
      if (g_serverMode)
        sockfd = acceptOne();
      else
        connectHost(sockfd);
    } else if (cmd == "r") {
      doRead(sockfd, line);
    } else if (cmd == "w") {
      doWrite(sockfd, line);
    } else if (cmd == "p") {
      doPoll(sockfd, line);
    }
  }
  puts("");
}

int main(int argc, char* argv[])
{
  setsignal(SIGINT, ctrl_c);
  setsignal(SIGPIPE, brokenPipe);

  if (argc > 1)
    g_serverMode = (strcmp(argv[1], "-s") == 0);
  else {
    printf("Usage: %s [-s] [host_ip]\n", argv[0]);
    return 0;
  }

  int sockfd;
  if (g_serverMode) {
    initServer(g_port);
    sockfd = acceptOne();
  } else {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
      fatal("socket error");
    g_host = argv[1];
    connectHost(sockfd);
  }

  run(sockfd);
}

