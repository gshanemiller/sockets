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
#include <sys/ioctl.h>
#include <net/if.h>

#include <vector>
#include <string>
#include <thread>

#include <ringbuffer.h>

int verbose = 0;
struct ifreq ifr;
std::string linkName;
int listenTimeoutMs = 60000;
std::vector<std::vector<int>> portsPerThread;

void usageAndExit() {
  printf("Accept TCP connect requests then echo all data received to stdout.\n\n");

  printf("usage: server -l <name> -t <portList> [-T <timeout> -v]\n\n");

  printf("where:\n\n");

  printf("   -l <linkName>  listen for packets only on named <linkName>\n");
  printf("                  run 'ip link show' or 'ifconfig' for choices\n"); 

  printf("   -t <port-list> run new thread listening for client connect on ports\n");
  printf("                  then read packets containing only integers. list is\n");
  printf("                  ':' delimited integers > 1024. each thread is pinned\n");
  printf("                  to a distinct CPU. repeat -t to add more threads w/ports\n");

  printf("   -T <timeout>   the listener waits for at most <timeout> milliseconds for\n");
  printf("                  connect requests. Specify a value>0. default %d\n", listenTimeoutMs);

  printf("   -v             increase verbosity. repeat for more detail\n\n");

  printf("For each -t argument provided, the server runs 1 thread to listen for\n");
  printf("TCP connections on its specified ports and one thread to read incoming\n");
  printf("data on those same ports once connected. TCP packet payloads are assumed\n");
  printf("to be sprintf'd integers.\n\n");
  
  printf("To stop the server press CTRL-C.\n\n");

  printf("Example: ./server.tsk -t 2000 -v -l enp1s0f1vf0\n");
  printf("Listen for TCP connections on port 2000 through OS linkname enp1s0f1vf0 then echo\n");
  printf("each TCP packet's content (assumed to be a sprintf'd integer) to stdout\n\n");

  printf("Warning: code does not check for duplicate ports\n");
  exit(2);
}

void parseCommandLines(int argc, char **argv) {
  int c;

  while ((c = getopt (argc, argv, "l:t:T:v")) != -1) {
    switch(c) {
      case 'l':
      {
        linkName = optarg;
        break;
      }

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
        portsPerThread.push_back(portList);
        break;
      }
      case 'T':
      {
        int val = atoi(optarg);
        if (val<=0) {
          fprintf(stderr, "timeout %d invalid\n", val);
          usageAndExit();
        } else {
          listenTimeoutMs = val; 
        }
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
  if (linkName.empty() || portsPerThread.empty()) {
    usageAndExit();
  }
}

int pinThread(int cpu) {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(cpu, &mask);

  // Pin thread to CPU
  if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1) {
    printf("invalid CPU %d: %s\n", cpu, strerror(errno));
    return -1;
  }
  return 0;
}

void readEventLoop(int cpu, int eprdFid, const unsigned connectionCount, RingBuffer::SPSC& ringBuffer) {
  // Pin this thread to 'cpu'
  if (pinThread(cpu)!=0) {
    return;
  }

  // Spend this amount of time (milliseconds) wait for read-ready
  const int timeoutMs = 5;

  // epoll events go here
  const int kEVENT_MAX = 15;
  struct epoll_event event[kEVENT_MAX];

  // Return result of read call
  int readRc;

  // The number of connections made and closed by client
  unsigned closedConnections = 0;
  // The number of possible remaining connections
  unsigned remainingConnections = connectionCount;

  // Run read event loop until all connections answered and closed
  while(closedConnections!=connectionCount) {
    // Check if new connections are possible
    if (remainingConnections) {
      int newFid;
      if (ringBuffer.read(&newFid)) {
        if (newFid==-1) {
          // -1 mean listener stopped; no more connections are possible
          closedConnections += remainingConnections;
          remainingConnections = 0; 
          continue;
        }
        
        // Got a new connection: decrement connections possible
        --remainingConnections; 
        if (verbose) {
          printf("CPU %02d connection received for fid %d with %d new connections remaining\n", cpu, newFid, remainingConnections);
        }

        // Make socket non-blocking
        // setnonblocking(newFid);

        // Tell OS to listen for read-ready edge triggered
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = newFid;
        if (epoll_ctl(eprdFid, EPOLL_CTL_ADD, newFid, &ev) == -1) {
          printf("CPU %02d unable to poll for read ready fid %d: %s\n", cpu, newFid, strerror(errno));
        }
      }
    }

    // Check for sockets read-ready
    int count = epoll_wait(eprdFid, event, kEVENT_MAX, timeoutMs); 
    if (count==-1) {
      printf("CPU %02d epoll read error: %s\n", cpu, strerror(errno));
    } 

    // process read ready events; count==0 --> timeout
    u_int64_t sequence;
    for (int i=0; i<count; ++i) {
      if (event[i].events!=0) {
        readRc = read(event[i].data.fd, &sequence, sizeof(sequence));
        if (readRc==sizeof(sequence)) {
          printf("CPU %02d read %lu\n", cpu, sequence);
        }
      }
    }
  }
}

void listenEventLoop(int cpu, int epolFid, const std::vector<int>& fid, RingBuffer::SPSC& ringBuffer) {
  // Tell the OS we're listening for connect events
  for (unsigned i=0; i<fid.size(); ++i) {
    int rc = listen(fid[i], 1);
    if (rc==-1) {
      printf("CPU %02d unable to listen on socket fid %d: %s\n", cpu, fid[i], strerror(errno));
    }
  }

  // Tell epoll to listen for connection events
  for (unsigned i=0; i<fid.size(); ++i) {
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fid[i];

    if (epoll_ctl(epolFid, EPOLL_CTL_ADD, fid[i], &ev) == -1) {
      printf("CPU %02d unable to add port %d to listener epoll: %s\n", cpu, fid[i], strerror(errno));
      return;
    }
  }

  // Number of connections we're looking for
  unsigned notConnectedCount = fid.size();

  // Elapsed time spent looking for connections (milliseconds)
  int elapsedTimeMs = 0;

  // Spend this much waiting per epoll call 
  const int timeoutMs = 5;

  // epoll events go here
  const int kEVENT_MAX = 15;
  struct epoll_event event[kEVENT_MAX];

  // back logged connected sockets not sent to client
  std::vector<int> backlog;

  // Run the listen event loop
  while(notConnectedCount && elapsedTimeMs <= listenTimeoutMs) {
    int count = epoll_wait(epolFid, event, kEVENT_MAX, timeoutMs); 
    if (count==-1) {
      printf("CPU %02d epoll wait error: %s\n", cpu, strerror(errno));
      break;
    } 

    // Track elapsed time
    elapsedTimeMs += timeoutMs;

    // Process each connection event from epoll; count==0 -> timeout
    for (int i=0; i<count; ++i) {
      if (event[i].events!=0) {
        struct sockaddr addr;
        socklen_t addrLen = sizeof(addr);
        if (verbose>1) {
          printf("CPU %02d received connect request for fid %d\n", cpu, event[i].data.fd);
        }
        int sock = accept(event[i].data.fd, &addr, &addrLen);
        if (sock==-1) {
          printf("CPU %02d socket accept error: %s\n", cpu, strerror(errno));
          break;
        } else if (verbose>1) {
          printf("CPU %02d read socket fid %d created\n", cpu, sock);
        }
        
        // Tell reader we have new connection
        // or add to backlog if queue full
        if (!ringBuffer.append(sock)) {
          backlog.push_back(sock);
        }

        // Reduce not connected count
        --notConnectedCount;
      }
    }

    // try to send any connected sockets not sent before
    for (unsigned i=0; i<backlog.size(); ++i) {
      if (backlog[i]>=0) {
        if (ringBuffer.append(backlog[i])) {
          backlog[i]=-1;
        }
      }
    }
  }

  // send reader -1 meaning all done listening
  while (!ringBuffer.append(-1));

  if (verbose>1) {
    printf("CPU %02d listener stopped\n", cpu);
  }
}

void serverSetup(const std::vector<int>& portList, const int cpu) {
  // Pin this thread to 'cpu'
  if (pinThread(cpu)!=0) {
    return;
  }

  // Create epoll descriptior for listening
  int epolFid = epoll_create1(0);
  if (epolFid == -1) {
    printf("CPU %d: failed to make epoll descriptor: %s\n", cpu, strerror(errno));
    return;
  }

  // Create epoll descriptor for reading
  int eprdFid = epoll_create1(0);
  if (eprdFid == -1) {
    printf("CPU %d: failed to make reader descriptor: %s\n", cpu, strerror(errno));
    return;
  }

  std::vector<int> fid;

  for (unsigned i=0; i<portList.size(); ++i) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd==-1) {
      printf("CPU %d unable to create socket: %s\n", cpu, strerror(errno));
      return;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof (ifr)) < 0) {
      printf("setsockopt() failed to bind socket to interface '%s': %s", linkName.c_str(), strerror(errno));
      exit(1);
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
      printf("CPU %d unable to create socket: %s\n", cpu, strerror(errno));
      return;
    }

    printf("CPU %02d socket fid %d for port %d created\n", cpu, fd, portList[i]);
  }

  // Start a reader thread on 'cpu' handling at most 'fid.size()' ports. As
  // connections are made in the listener (below), those fids are transferred
  // to this reader over a SPCP ringbuffer
  RingBuffer::SPSC ringbuffer;
  std::thread readThread(readEventLoop, cpu, eprdFid, fid.size(), std::ref(ringbuffer));

  // Run listener event loop on this thread. As connections made, they are
  // are transferred to reader (above) over SPSC queue. And once all the
  // connections are made, the listener returns
  listenEventLoop(cpu, epolFid, std::ref(fid), std::ref(ringbuffer));
  // Close listener's epoll fid
  close(epolFid);

  // Join with reader thread
  readThread.join();
  // Close reader's epoll fid
  close(eprdFid);

  // Cleanup socket fids
  for (unsigned i=0; i<fid.size(); ++i) {
    close(fid[i]);
  }
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
  std::vector<std::thread> thread;
  
  parseCommandLines(argc, argv);

  findLinkNameIndex();

  // Run server threads pinned to CPU 'i'
  for (unsigned i=0; i<portsPerThread.size(); ++i) {
    thread.push_back(std::thread(serverSetup, std::ref(portsPerThread[i]), i));
  }

  // Wait for threads to stop
  for (unsigned i=0; i<thread.size(); ++i) {
    thread[i].join();
  }

  return 0;
}
