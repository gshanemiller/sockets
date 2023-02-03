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

#include <ringbuffer.h>

void usageAndExit() {
  printf("Usage: server -t <port list>\n\n");
  printf("where:\n\n");
  printf("   -t <port-list> run new thread listening for client connect on por list\n");
  printf("                  <port-list> is a ':' delimited integer > 1024 list\n");
  printf("                  each thread is pinned to a distinct CPU\n");
  printf("   -v             print payloads received. repeat as needed\n\n");
  printf("Server threads listen for packets containing a one signed 32-bit integer.\n");
  printf("The connection is closed whenever a value < 0 is encountered.\n");
  printf("No responses are produced.\n\n");
  printf("Warning: code does not check for duplicate ports\n");
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

int pinThread(int cpu) {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(cpu, &mask);

  // Pin thread to CPU
  if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1) {
    fprintf(stderr, "invalid CPU %d: %s\n", cpu, strerror(errno));
    return -1;
  }

  return 0;
}

void readEventLoop(int cpu, int eprdFid, unsigned connectionCout, RingBuffer::SPSC& ringBuffer) {
  return;
}

void listenEventLoop(int epolFid, const std::vector<int>& fid, RingBuffer::SPSC& ringBuffer) {
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;

    if (epoll_ctl(epolFid, EPOLL_CTL_ADD, fd, &ev) == -1) {
      fprintf(stderr, "CPU %d unable to add port %d to listener epoll: %s\n", cpu, portList[i], strerror(errno));
      return;
    }
  return;
}

void serverSetup(const std::vector<int>& portList, const int cpu) {
  // Pin this thread to 'cpu'
  if (pinThread(cpu)!=0) {
    return;
  }

  // Create epoll descriptior for listening
  int epolFid = epoll_create1(0);
  if (epolFid == -1) {
    fprintf(stderr, "CPU %d: failed to make epoll descriptor: %s\n", cpu, strerror(errno));
    return;
  }

  // Create epoll descriptor for reading
  int eprdFid = epoll_create1(0);
  if (eprdFid == -1) {
    fprintf(stderr, "CPU %d: failed to make reader descriptor: %s\n", cpu, strerror(errno));
    return;
  }

  std::vector<int> fid;

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

    printf("CPU %02d socket for port %d created\n", cpu, portList[i]);
  }

  // Start a reader thread on 'cpu' handling at most 'fid.size()' ports. As
  // connections are made in the listener (below), those fids are transferred
  // to this reader over a SPCP ringbuffer
  RingBuffer::SPSC ringbuffer;
  std::thread readThread(readEventLoop, cpu, eprdFid, fid.size(), std::ref(ringbuffer));

  // Run listener event loop on this thread. As connections made, they are
  // are transferred to reader (above) over SPSC queue. And once all the
  // connections are made, the listener returns
  listenEventLoop(epolFid, std::ref(fid), std::ref(ringbuffer));
  // Close listener's epoll fid
  close(epolFid);

  // Join with reader thread
  readThread.join()
  // Close reader's epoll fid
  close(eprdFid);

  // Cleanup socket fids
  for (unsigned i=0; i<fid.size(); ++i) {
    close(fid[i]);
  }
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
