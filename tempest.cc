// Tempest - An interactive learning tool for Sockets programming.
// Copyright (c) 2010, Shuo Chen.  All rights reserved.
// http://github.com/chenshuo/tempest
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//   * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//   * The name of the author may not be used to endorse or promote
// products derived from this software without specific prior written
// permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#ifdef __linux
#include <linux/tcp.h>
#else
#include <netinet/tcp.h>
#endif
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
  while ( (sockfd = accept(g_listenfd, NULL, NULL)) < 0) {
    perror("accept error");
    if (errno == EINTR) {
      exit(1);
    }
  }
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
  static string lastline;
  static vector<string> lastresult;
  char* line = ::readline("> ");
  vector<string> result;
  if (line == NULL) {
    result.push_back("q");
  } else {
    istringstream iss(line);
    istream_iterator<string> begin(iss);
    istream_iterator<string> end;
    copy(begin, end, back_inserter(result));

    if (lastline != line && !result.empty()) {
      add_history(line);
      lastline = line;
      lastresult = result;
    }
  }
  free(line);
  if (result.empty())
    result = lastresult;
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

void doRead(int sockfd, const vector<string>& line, bool waitall)
{
  vector<char> buf;
  buf.resize(1024);
  if (line.size() > 1) {
    int len = atoi(line[1].c_str());
    buf.resize(len);
  }
  if (waitall)
    printflush("reading %zu buf ... ", buf.size());
  else
    printflush("reading up to %zu ... ", buf.size());
  ssize_t n = ::recv(sockfd, &*buf.begin(), buf.size(), waitall ? MSG_WAITALL : 0);
  int saved = errno;
  printf("read %zd bytes", n);
  if (n < 0)
    printf(", %d - %s\n", saved, strerror(saved));
  else
    printf("\n");
}

void doWrite(int sockfd, const vector<string>& line, bool str)
{
  string buf = "H";
  if (line.size() > 1) {
    string arg = line[1];
    if (str) {
      buf = arg;
    } else {
      int len = atoi(arg.c_str());
      buf.assign(len, 'H');
    }
  }
  printflush("sending %zu bytes ... ", buf.size());
  ssize_t n = ::send(sockfd, buf.c_str(), buf.size(), 0);
  int saved = errno;
  printf("wrote %zd bytes", n);
  if (n < 0)
    printf(", %d - %s\n", saved, strerror(saved));
  else
    printf("\n");
}

void doPoll(int sockfd, const vector<string>& line, bool pollout)
{
  struct pollfd pfd;
  pfd.fd = sockfd;
  pfd.events = POLLIN | POLLPRI;
#ifdef POLLRDHUP
  pfd.events |= POLLRDHUP;
#endif
  if (pollout)
    pfd.events |= POLLOUT;
  pfd.revents = 0;
  int timeout = 0;
  if (line.size() > 1) {
    timeout = atoi(line[1].c_str()) * 1000;
  }
  printflush("polling %d ms ... ", timeout);
  int n = ::poll(&pfd, 1, timeout);
  if (n > 0) {
    printf("%d events\n", n);
    if (pfd.revents & POLLIN)
      printf("IN ");
    if (pfd.revents & POLLPRI)
      printf("PRI ");
    if (pfd.revents & POLLOUT)
      printf("OUT ");
    if (pfd.revents & POLLHUP)
      printf("HUP ");
#ifdef POLLRDHUP
    if (pfd.revents & POLLRDHUP)
      printf("RDHUP ");
#endif
    if (pfd.revents & POLLERR)
      printf("ERR ");
    if (pfd.revents & POLLNVAL)
      printf("NVAL ");
    printf("\n");
  } else if (n == 0)
    printf("time out\n");
  else
    perror("poll error");
}

void doShutdown(int sockfd, int how)
{
  if (::shutdown(sockfd, how) < 0)
    perror("shutdown error");
}

void doShowName(int sockfd)
{
  char host[INET_ADDRSTRLEN];

  struct sockaddr_in localaddr;
  socklen_t addrlen = sizeof(localaddr);
  if (::getsockname(sockfd, (struct sockaddr*)(&localaddr), &addrlen) < 0)
    perror("getsockname error");
  else {
    inet_ntop(AF_INET, &localaddr.sin_addr, host, sizeof host);
    printflush("local %s:%u  ", host, ntohs(localaddr.sin_port));
  }

  struct sockaddr_in remoteaddr;
  addrlen = sizeof(remoteaddr);
  if (::getpeername(sockfd, (struct sockaddr*)(&remoteaddr), &addrlen) < 0)
    perror("getpeername error");
  else {
    inet_ntop(AF_INET, &remoteaddr.sin_addr, host, sizeof host);
    printf("remote %s:%u\n", host, ntohs(remoteaddr.sin_port));
  }
}

void printstatus(int sockfd, const char* name, int level, int optname)
{
  int optval;
  socklen_t optlen = sizeof optval;

  if (::getsockopt(sockfd, level, optname, &optval, &optlen) < 0)
    perror("getsockopt error");
  else
    printf("%-14s %d\n", name, optval);
}

void doStatus(int sockfd, bool detail)
{
  if (detail) {
    printstatus(sockfd, "SO_DEBUG"     , SOL_SOCKET, SO_DEBUG);
    printstatus(sockfd, "SO_ERROR"     , SOL_SOCKET, SO_ERROR);
    printstatus(sockfd, "SO_KEEPALIVE" , SOL_SOCKET, SO_KEEPALIVE);
  }
  printstatus(sockfd, "SO_RCVBUF"    , SOL_SOCKET, SO_RCVBUF);
  printstatus(sockfd, "SO_SNDBUF"    , SOL_SOCKET, SO_SNDBUF);
  if (detail) {
    printstatus(sockfd, "SO_RCVLOWAT"  , SOL_SOCKET, SO_RCVLOWAT);
    printstatus(sockfd, "SO_SNDLOWAT"  , SOL_SOCKET, SO_SNDLOWAT);
    printstatus(sockfd, "TCP_MAXSEG"   , IPPROTO_TCP, TCP_MAXSEG);
    printstatus(sockfd, "TCP_NODELAY"  , IPPROTO_TCP, TCP_NODELAY);

    int flags = ::fcntl(sockfd, F_GETFL, 0);
    printf("%-14s %d\n", "O_NONBLOCK", (flags & O_NONBLOCK) ? 1 : 0);
  }

  int nread;
  if (::ioctl(sockfd, FIONREAD, &nread) < 0)
    perror("ioctl error");
  else
    printf("%-14s %d\n", "FIONREAD", nread);

  struct tcp_info tcpi;
  socklen_t len = sizeof(tcpi);
  bzero(&tcpi, len);
  // printf("len tcpi %d\n", len);
  if (getsockopt(sockfd, IPPROTO_TCP, TCP_INFO, &tcpi, &len) == 0)
  {
#define PT(FIELD) printf("%-14s %u\n", #FIELD, tcpi.tcpi_##FIELD)
#ifdef __linux
#define PU(FIELD) printf("%-14s %u\n", #FIELD, tcpi.tcpi_##FIELD)
#else
#define PU(FIELD) printf("%-14s %u\n", #FIELD, tcpi.__tcpi_##FIELD)
#endif
#define PL(FIELD) printf("%-14s %llu\n", #FIELD, tcpi.tcpi_##FIELD)
    if (detail) {
      printf("\n");
      PT(state);
      PU(ca_state);
      PU(retransmits);
      PU(probes);
      PU(backoff);
      PT(options);
      PT(snd_wscale);
      PT(rcv_wscale);
#ifdef __linux
      PT(delivery_rate_app_limited);
      PT(fastopen_client_fail);
#endif
    }

    printf("\n");
    PT(rto);
    PU(ato);
    PT(snd_mss);
    PT(rcv_mss);

    if (detail) {
      printf("\n");
      PU(unacked);
      PU(sacked);
      PU(lost);
      PU(retrans);
      PU(fackets);

      printf("\n");
      PU(last_data_sent);
      PU(last_ack_sent);     /* Not remembered, sorry. */
      PT(last_data_recv);
      PU(last_ack_recv);
    }

    printf("\n");
    PU(pmtu);
    PU(rcv_ssthresh);
    PT(rtt);
    PT(rttvar);
    PT(snd_ssthresh);
    PT(snd_cwnd);
    PU(advmss);
    PU(reordering);

    if (detail) {
      PU(rcv_rtt);
      PT(rcv_space);

#ifdef __linux
      PT(total_retrans);
#endif
    }

#ifdef __linux
    printf("\n");
    PL(pacing_rate);
    PL(max_pacing_rate);
    PL(bytes_acked);    /* RFC4898 tcpEStatsAppHCThruOctetsAcked */
    PL(bytes_received); /* RFC4898 tcpEStatsAppHCThruOctetsReceived */
    PT(segs_out);	/* RFC4898 tcpEStatsPerfSegsOut */
    PT(segs_in);	/* RFC4898 tcpEStatsPerfSegsIn */

    PT(notsent_bytes);
    PT(min_rtt);
    PT(data_segs_in);	/* RFC4898 tcpEStatsDataSegsIn */
    PT(data_segs_out);	/* RFC4898 tcpEStatsDataSegsOut */

    PL(delivery_rate);

    printf("\n");
    PL(busy_time);      /* Time (usec) busy sending data */
    PL(rwnd_limited);   /* Time (usec) limited by receive window */
    PL(sndbuf_limited); /* Time (usec) limited by send buffer */

    PT(delivered);
    if (detail) {
      PT(delivered_ce);
    }

    PL(bytes_sent);     /* RFC4898 tcpEStatsPerfHCDataOctetsOut */
    PL(bytes_retrans);  /* RFC4898 tcpEStatsPerfOctetsRetrans */
    if (detail) {
      PT(dsack_dups);     /* RFC4898 tcpEStatsStackDSACKDups */
      PT(reord_seen);     /* reordering events seen */

      PT(rcv_ooopack);    /* Out-of-order packets received */
    }

    PT(snd_wnd);	/* peer's advertised receive window after
                         * scaling (bytes)
                         */
#elif __FreeBSD__
    PT(snd_wnd);		/* Advertised send window. */
    PT(snd_bwnd);		/* No longer used. */
    PT(snd_nxt);		/* Next egress seqno */
    PT(rcv_nxt);		/* Next ingress seqno */
    PT(toe_tid);		/* HWTID for TOE endpoints */
    PT(snd_rexmitpack);		/* Retransmitted packets */
    PT(rcv_ooopack);		/* Out-of-order packets */
    PT(snd_zerowin);		/* Zero-sized windows sent */
#endif
  }
}

double gettime()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

void doResolve(const vector<string>& line)
{
  double start = gettime();
  const char* host = line.size() > 1 ? line[1].c_str() : "www.chenshuo.com";
  struct hostent* he = gethostbyname(host);
  double end = gettime();
  printf("resolve domain name %s took %f seconds\n", host, end-start);
  if (he)
  {
    printf("h_name: %s\n", he->h_name);
    for (char** alias = he->h_aliases; *alias != NULL; ++alias)
    {
      printf("alias: %s\n", *alias);
    }
    for (char** addr = he->h_addr_list; *addr != NULL; ++addr)
    {
      char buf[32] = "unknown";
      inet_ntop(he->h_addrtype, *addr, buf, sizeof buf);
      printf("addr: %s\n", buf);
    }
  }
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

void setNodelay(int sockfd, bool on)
{
  int optval = on ? 1 : 0;
  if (::setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof optval) < 0)
    perror("setsockopt error");
}

void setDebug(int sockfd, bool on)
{
  int optval = on ? 1 : 0;
  if (::setsockopt(sockfd, SOL_SOCKET, SO_DEBUG, &optval, sizeof optval) < 0)
    perror("setsockopt error");
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
      " r     - read 1024 bytes or less\n"
      " r N   - read N bytes or less\n"
      " rn N  - read N bytes\n"
      " w     - write 1 byte\n"
      " w N   - write N bytes\n"
      " ws x  - write string x\n"
      " p     - poll w/o POLLOUT\n"
      " pw    - poll w/ POLLOUT\n"
      " n     - show socket names\n"
      " st    - status\n"
      " sta   - status details\n"
      " str   - shutdown RD\n"
      " stw   - shutdown WR\n"
      " strw  - shutdown RDWR\n"
      " b     - set blocking\n"
      " nb    - set non-blocking\n"
      " d     - set delay\n"
      " nd    - set no-delay\n"
      " res d - resolve domain name\n"
      " ENTER - repeat last cmd\n"
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
    } else if (cmd == "d") {
      setNodelay(sockfd, false);
    } else if (cmd == "nd") {
      setNodelay(sockfd, true);
    } else if (cmd == "dbg") {
      setDebug(sockfd, true);
    } else if (cmd == "ndbg") {
      setDebug(sockfd, false);
    } else if (cmd == "c") {
      if (::close(sockfd) < 0)
        perror("close error");
    } else if (cmd == "rc") {
      if (g_serverMode)
        sockfd = acceptOne();
      else
        connectHost(sockfd);
    } else if (cmd == "r") {
      doRead(sockfd, line, false);
    } else if (cmd == "rn") {
      doRead(sockfd, line, true);
    } else if (cmd == "w") {
      doWrite(sockfd, line, false);
    } else if (cmd == "ws") {
      doWrite(sockfd, line, true);
    } else if (cmd == "pw") {
      doPoll(sockfd, line, true);
    } else if (cmd == "p") {
      doPoll(sockfd, line, false);
    } else if (cmd == "n") {
      doShowName(sockfd);
    } else if (cmd == "st") {
      doStatus(sockfd, false);
    } else if (cmd == "sta") {
      doStatus(sockfd, true);
    } else if (cmd == "str") {
      doShutdown(sockfd, SHUT_RD);
    } else if (cmd == "stw") {
      doShutdown(sockfd, SHUT_WR);
    } else if (cmd == "strw") {
      doShutdown(sockfd, SHUT_RDWR);
    } else if (cmd == "res") {
      doResolve(line);
    } else {
      puts("unknown command - ? for help");
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

  printf("pid = %d\n", getpid());

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

