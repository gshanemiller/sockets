#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/epoll.h>

#include <string>
#include <vector>
#include <thread>

int max = 10;
std::vector<int> portPerThread;
std::vector<std::string> ipAddrPerThread;

void usageAndExit() {
  printf("Usage: client -t <port>\n\n");
  printf("where:\n\n");

  printf("   -t <ip>:<port> run thread sending data to <ip>:<port> where\n");
  printf("                  port>1024. thread is pinned to distinct CPU.\n");
  printf("                  <ip> should be in xx.xx.xx.xx format. repeat\n");
  printf("                  -t as needed. see -N.\n");
  
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
        char *del = strchr(optarg, ':');
        if (del!=0 && del!=optarg) {
          int port = atoi(del+1);
          if (port>1024) {
            portPerThread.push_back(port);
            *del = 0;
            ipAddrPerThread.push_back(optarg);
            break;
          }
        }
        printf("invalid port or ip address '%s'\n", optarg);
        usageAndExit();
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

void entryPoint(const std::string& ipAddr, const int port, const int cpu) {
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

  // Setup IP address to connect to
  struct sockaddr_in addr; 
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port); 
  if (inet_pton(AF_INET, ipAddr.c_str(), &addr.sin_addr)<=0)
  {
    printf("CPU %02d IP address '%s' invalid: %s\n", cpu, ipAddr.c_str(), strerror(errno));
    return;
  }

  // Attempt to connect to server
  int rc = connect(fd, &addr, sizeof(addr));
  {
    printf("CPU %02d connect to '%s:%d' failed: %s\n", cpu, ipAddr.c_str(), port, strerror(errno));
    return;
  } 

  if (verbose) {
    printf("CPU %02d IP address connected to '%s:%d'\n", cpu, ipAddr.c_str(), port);
  }

  // send max integers
  int sequence(0);
  while (++sequence<=max) {
    rc = write(fd, &sequence, sizeof(sequence)); 
    if (rc<=0) {
      printf("CPU %02d '%s:%d' write failed: %s\n", cpu, ipAddr.c_str(), port, strerror(errno));
    } else if (verbose) {
      printf("CPU %02d '%s:%d' sent %d\n", cpu, ipAddr.c_str(), port, sequence);
    }
  }

  // Send -1 so server closes connection
  sequence = -1;
  rc = write(fd, &sequence, sizeof(sequence)); 
  if (rc<=0) {
    printf("CPU %02d '%s:%d' write failed: %s\n", cpu, ipAddr.c_str(), port, strerror(errno));
  }

  // Close socket
  close(fd);
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
