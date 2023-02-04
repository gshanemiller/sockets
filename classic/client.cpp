#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/epoll.h>

#include <vector>
#include <thread>

int max = 10;
std::vector<int> portPerThread;

void usageAndExit() {
  printf("Usage: client -t <port>\n\n");
  printf("where:\n\n");

  printf("   -t <port>      run new thread sending data to local port <port>\n");
  printf("                  which must be > 1024. see -N. thread is pinned to\n");
  printf("                  distinct CPU. repeat as needed\n");
  
  printf("   -N <max>       send max>0 integers sequenced [0..max) to server\n");
  printf("                  default %d\n", max);

  printf("   -v             print payloads sent\n\n");

  printf("Warning: code does not check for duplicate ports\n");
  exit(2);
}

void parseCommandLines(int argc, char **argv) {
  int c;

  while ((c = getopt (argc, argv, "t:N:v")) != -1) {
    switch(c) {
      case 't':
      {
        int port = atoi(optarg);
        if (port<=1024) {
          fprintf(stderr, "port %d invalid\n", port);
          usageAndExit();
        } else {
          portPerThread.push_back(port);
        }
        break;
      }
      case 'N':
      {
        int val = atoi(optarg);
        if (val<=1024) {
          printf("invalid port %d\n", val);
          usageAndExit();
        }
        max = val;
        break;
      }
      case 'v':
      {
        ++verbose;
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

void entryPoint(const int port, const int cpu) {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(cpu, &mask);

  if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1) {
    printf("CPU %02d invalid\n", cpu);
    return;
  }

  // Create a socket to send data on
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd==-1) {
    printf("CPU %02d unable to create socket: %s\n", cpu, strerror(errno));
    return;
  }

  // Connect to server
  struct sockaddr addr;
  memset(&addr, sizeof(addr), 0);
  addr.sun_family = AF_UNIX;
  sprintf(addr.sun_path, "/tmp/client_cpu%d_port%d.socket", cpu, port);
  unlink(addr.sun_path);

  if (connect(fd, &addr, strlen(addr.sun_path) + sizeof(addr.sun_family)) == -1) {
        perror("connect");
        exit(1);
  
  
  
 
  printf("CPU %02d client running for port %d\n", cpu, port);
}
  
int main(int argc, char **argv) {
  parseCommandLines(argc, argv);

  // Run threads pinned to CPU 'i'
  for (unsigned i=0; i<portPerThread.size(); ++i) {
    thread.push_back(std::thread(entryPoint, portPerThread[i], i));
  }

  // Wait for threads to stop
  for (unsigned i=0; i<thread.size(); ++i) {
    thread[i].join();
  }

  return 0;
}
