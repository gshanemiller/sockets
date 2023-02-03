#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sched.h>
#include <netdb.h>
#include <errno.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <vector>
#include <thread>

void usageAndExit() {
  printf("Usage: server -t <port list>\n\n");
  printf("where:\n\n");
  printf("   -t <port-list> run new thread listening to ports in list. repeat as needed\n");
  printf("                  <port-list> is a ':' seperated integer ports > 1024\n");
  printf("                  each thread is pinned to a distinct CPU\n");
  printf("   -v             print payloads received\n\n");
  printf("Server threads listen for packets containing only a single signed 32-bit sequence.\n");
  printf("The connection is closed whenever a value < 0 is seen.\n\n");
  printf("Warning: code does check for duplicate ports\n");
  exit(2);
}

void parseCommandLines(int argc, char **argv, std::vector<std::vector<int>> *portsPerThread) {
  int c;

  while ((c = getopt (argc, argv, "t:")) != -1) {
    switch(c) {
      case 't':
      {
        std::vector<int> portList;
        char *tok = strtok(optarg, ":");
        while(tok) {
          int port = atoi(tok);
          if (port<=1024) {
            fprintf(stderr, "port %d invalid\n", port);
            usageAndExit();
          } else {
            portList.push_back(port);
          }
          tok = strtok(0, ":");
        }
        portsPerThread->push_back(portList);
        break;
      }
      default:
        usageAndExit();
    }
  }
  if (portsPerThread->empty()) {
    usageAndExit();
  }
}

void eventLoop(int epolFid, int epioFid, const std::vector<int>& fid) {
  const int k_EVENT_MAX = 10;
  struct epoll_event ev[k_EVENT_MAX];

  while(1) {
    int eventCount = epoll_wait(epolFid, ev, k_EVENT_MAX, -1);
  }
}

void serverSetup(const std::vector<int>& portList, const int cpu) {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(cpu, &mask);

  // Pin thread to CPU
  if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1) {
    fprintf(stderr, "invalid CPU %d: %s\n", cpu, strerror(errno));
    return;
  }

  // Create epoll descriptior for listening
  int epolFid = epoll_create1(0);
  if (epolFid == -1) {
    fprintf(stderr, "CPU %d: failed to make epoll descriptor: %s\n", cpu, strerror(errno));
    return;
  }

  // Create epoll descriptor for reading
  int epioFid = epoll_create1(0);
  if (epioFid == -1) {
    fprintf(stderr, "CPU %d: failed to make reader descriptor: %s\n", cpu, strerror(errno));
    return;
  }

  std::vector<int> fid;
  std::vector<struct sockaddr_in> socketAddr;

  for (unsigned i=0; i<portList.size(); ++i) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd==-1) {
      fprintf(stderr, "CPU %d unable to create socket: %s\n", cpu, strerror(errno));
      return;
    }

    // Record fid
    fid.push_back(fd);

    // Bind socket to fid
    struct sockaddr_in sock;
    memset(&sock, 0, sizeof(sock));
    sock.sin_family = AF_INET;
    sock.sin_addr.s_addr = INADDR_ANY;
    sock.sin_port = htons(portList[i]);

    if (bind(fd, (struct sockaddr *)&sock, sizeof(sock)) < 0) {
      fprintf(stderr, "CPU %d unable to create socket: %s\n", cpu, strerror(errno));
      return;
    }

    socketAddr.push_back(sock);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;

    if (epoll_ctl(epolFid, EPOLL_CTL_ADD, fd, &ev) == -1) {
      fprintf(stderr, "CPU %d unable to add port %d to listener epoll: %s\n", cpu, portList[i], strerror(errno));
      return;
    }

    printf("CPU %02d socket for port %d created\n", cpu, portList[i]);
  }

  // We're setup: hand off to event loop
  eventLoop(epolFid, epioFid, fid);
}
  
int main(int argc, char **argv) {
  std::vector<std::thread>      thread;
  std::vector<std::vector<int>> portsPerThread;
  
  parseCommandLines(argc, argv, &portsPerThread);

  // Run threads pinned to CPU 'i'
  for (unsigned i=0; i<portsPerThread.size(); ++i) {
    thread.push_back(std::thread(serverSetup, std::ref(portsPerThread[i]), i));
  }

  // Wait for threads to stop
  for (unsigned i=0; i<thread.size(); ++i) {
    thread[i].join();
  }

  return 0;
}
