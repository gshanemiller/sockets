#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/epoll.h>


#include <vector>
#include <thread>

void usageAndExit() {
  printf("Usage: server -t <port list>\n\n");
  printf("where:\n\n");
  printf("   -t <port-list> run new thread listening to ports in list. repeat as needed\n");
  printf("                  <port-list> is a ':' seperated integer ports > 1024\n");
  printf("                  each thread is pinned to a distinct CPU\n");
  printf("   -v             print payloads received\n\n");
  printf("Server threads listen for packets containing a signed 32-bit sequence number. Servers\n");
  printf("close the connection when any value <0\n");
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

void entryPoint(const std::vector<int>& portList, const int cpu) {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(cpu, &mask);

  if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1) {
    fprintf(stderr, "CPU %d invalid\n", cpu);
    return;
  }
  
  printf("Running server on CPU %d on ports ", cpu);
  for (unsigned i=0; i<portList.size(); ++i) {
    printf("%d ", portList[i]);
  }
  printf("\n");
}
  
int main(int argc, char **argv) {
  std::vector<std::thread>      thread;
  std::vector<std::vector<int>> portsPerThread;
  
  parseCommandLines(argc, argv, &portsPerThread);

  // Run threads pinned to CPU 'i'
  for (unsigned i=0; i<portsPerThread.size(); ++i) {
    thread.push_back(std::thread(entryPoint, std::ref(portsPerThread[i]), i));
  }

  // Wait for threads to stop
  for (unsigned i=0; i<thread.size(); ++i) {
    thread[i].join();
  }

  return 0;
}
