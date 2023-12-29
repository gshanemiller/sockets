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
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <string>
#include <vector>
#include <thread>

int verbose = 0;
struct ifreq ifr;
std::string linkName;                                                                                                   
u_int64_t sequence = 0;
unsigned sleepSecs = 1;
std::vector<int> portPerThread;
std::vector<std::string> ipAddrPerThread;

void usageAndExit() {
  printf("Create a TCP connection then send packets with payloads consisting of a sequence numbers 1,2,...\n");
  printf("A sleep time (default %u sec) is expended between transmitted packets. Set 0 to disable.\n\n", sleepSecs);

  printf("usage: client -i <linkName> -t <port> [-s <sleepSec>]\n\n");
  printf("where:\n\n");

  printf("   -l <linkName>  send packets only through named <linkName>\n");                                            
  printf("                  run 'ip link show' or 'ifconfig' for choices\n"); 

  printf("   -t <ip>:<port> run thread sending data to <ip>:<port> where\n");
  printf("                  port>1024. thread is pinned to distinct CPU.\n");
  printf("                  <ip> should be in xx.xx.xx.xx format. repeat\n");
  printf("                  -t as needed to add more ports on their threads.\n");
  
  printf("   -s <n>         sleep n>=0 seconds between transmitted packets. default\n");
  printf("                  is %u seconds. Set 0 to disable\n", sleepSecs);

  printf("   -v             print payload\n\n");

  printf("To stop the client press CTRL-C.\n\n");

  printf("Example: ./client.tsk -t 192.168.0.5:2000 -v -l enp1s0f1vf0\n");
  printf("Through the link 'enp1s0f1vf0' create a TCP connection to 192.168.0.5\n");
  printf("port 2000 then send a TCP packet with payload 0, sleep 1 second,\n");
  printf("then send the next TCP packet with sequence 2, and so on. A server\n");
  printf("must be listening to on port 2000 prior to running client.\n\n");

  printf("Warning: code does not check for duplicate ports\n");
  exit(2);
}

void parseCommandLines(int argc, char **argv) {
  int c;

  while ((c = getopt (argc, argv, "l:t:s:v")) != -1) {
    switch(c) {
      case 'l':
      {
        linkName = optarg;
        break;
      }

      case 's':
      {
        int i = atoi(optarg);
        if (i<0) {
          usageAndExit();
        }
        sleepSecs = (unsigned)i;
        break;
      }

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

      case 'v':
      {
        ++verbose;
        break;
      }
      default:
      {
        usageAndExit();
      }
    }
  }
  if (portPerThread.empty() || linkName.empty()) {
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

  if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof (ifr)) < 0) {                                          
    printf("setsockopt() failed to bind socket to interface '%s': %s", linkName.c_str(), strerror(errno));            
    exit(1);                                                                                                          
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
  int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
  if (rc==-1) {
    printf("CPU %02d connect to '%s:%d' failed: %s\n", cpu, ipAddr.c_str(), port, strerror(errno));
    return;
  } 

  if (verbose) {
    printf("CPU %02d connected to '%s:%d'\n", cpu, ipAddr.c_str(), port);
  }

  while (1) {
    ++sequence;
    rc = write(fd, &sequence, sizeof(sequence)); 
    if (rc!=sizeof(sequence)) {
      printf("CPU %02d '%s:%d' write failed: %s\n", cpu, ipAddr.c_str(), port, strerror(errno));
    } else if (verbose) {
      printf("CPU %02d '%s:%d' sent %lu\n", cpu, ipAddr.c_str(), port, sequence);
    }
    if (sleepSecs) {
      sleep(sleepSecs);
    }
  }

  // Close socket
  close(fd);
}

void findLinkNameIndex() {                                                                                              
  int sd;                                                                                                               
                                                                                                                        
  if ((sd = socket (AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {                                                             
    printf("socket() failed to get socket descriptor for ioctl(): %s\n", strerror(errno));                              
    exit(1);                                                                                                            
  }                                                                                                                     
                                                                                                                        
  memset (&ifr, 0, sizeof (ifr));                                                                                       
  snprintf(ifr.ifr_name, linkName.length()+1, "%s", linkName.c_str());                                                  
  if (ioctl (sd, SIOCGIFINDEX, &ifr) < 0) {                                                                             
    printf("ioctl() failed to find interface '%s': %s\n", linkName.c_str(), strerror(errno));                           
    exit(1);                                                                                                            
  }                                                                                                                     
                                                                                                                        
  close(sd);                                                                                                            
                                                                                                                        
  if (verbose) {
    printf ("ioctl index for interface '%s' is %i\n", linkName.c_str(), ifr.ifr_ifindex);
  }
}
  
int main(int argc, char **argv) {
  parseCommandLines(argc, argv);

  findLinkNameIndex();                                                                                                  

  // Run threads pinned to CPU 'i'
  std::vector<std::thread> thread;
  for (unsigned i=0; i<portPerThread.size(); ++i) {
    thread.push_back(std::thread(entryPoint, std::ref(ipAddrPerThread[i]), portPerThread[i], i));
  }

  // Wait for threads to stop
  for (unsigned i=0; i<thread.size(); ++i) {
    thread[i].join();
  }

  return 0;
}
