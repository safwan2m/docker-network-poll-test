#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <fcntl.h>
#include <errno.h>
#include <emmintrin.h>  // For _mm_pause()

#define INTERFACE "eno1"
#define NUM_THREADS 10  // Define the number of threads
#define SCHED_OAI SCHED_RR
#define OAI_PRIORITY_RT_LOW sched_get_priority_min(SCHED_OAI)
#define OAI_PRIORITY_RT ((sched_get_priority_min(SCHED_OAI)+sched_get_priority_max(SCHED_OAI))/2)
#define OAI_PRIORITY_RT_MAX sched_get_priority_max(SCHED_OAI)-2

void threadCreate(pthread_t* t, void * (*func)(void*), void * param, char* name, int affinity, int priority)
{
  int ret;
  bool set_prio = (geteuid() == 0); // Check if running as root for priority settings

  pthread_attr_t attr;
  ret = pthread_attr_init(&attr);
  if (ret != 0) {
    perror("pthread_attr_init");
    exit(EXIT_FAILURE);
  }

  if (set_prio) {
    ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    if (ret != 0) {
      perror("pthread_attr_setinheritsched");
      exit(EXIT_FAILURE);
    }

    ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO); // Replacing SCHED_OAI with SCHED_FIFO
    if (ret != 0) {
      perror("pthread_attr_setschedpolicy");
      exit(EXIT_FAILURE);
    }

    struct sched_param sparam = {0};
    sparam.sched_priority = priority;
    ret = pthread_attr_setschedparam(&attr, &sparam);
    if (ret != 0) {
      perror("pthread_attr_setschedparam");
      exit(EXIT_FAILURE);
    }
    
    printf("Creating thread %s with affinity %x, priority %d\n", name, affinity, priority);
  } else {
    affinity = -1;
    priority = -1;
    printf("Creating thread %s (no affinity, default priority)\n", name);
  }

  ret = pthread_create(t, &attr, func, param);
  if (ret != 0) {
    perror("pthread_create");
    exit(EXIT_FAILURE);
  }

  pthread_setname_np(*t, name);
  if (affinity != -1) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(affinity, &cpuset);
    ret = pthread_setaffinity_np(*t, sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
      perror("pthread_setaffinity_np");
      exit(EXIT_FAILURE);
    }
  }

  pthread_attr_destroy(&attr);
}

void* poll_network(void* arg) {
    int sockfd;
    char buffer[2048];
    struct sockaddr saddr;
    socklen_t saddr_len = sizeof(saddr);


    // Create raw socket
    sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sockfd < 0) {
        perror("Socket creation failed");
        pthread_exit(NULL);
    }

    // Set non-blocking mode
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    // Bind to network interface
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, INTERFACE, IFNAMSIZ - 1);
    if (setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr)) < 0) {
        perror("Failed to bind to network interface");
        close(sockfd);
        pthread_exit(NULL);
    }

    printf("Polling network interface: %s (100%% CPU Usage)\n", INTERFACE);

    // Continuous polling loop (no blocking)
    while (1) {
        ssize_t len = recvfrom(sockfd, buffer, sizeof(buffer), 0, &saddr, &saddr_len);
        if (len > 0) {
            printf("Received packet: %ld bytes\n", len);
        } else {
            // Keep CPU busy with artificial work to force 100% usage
            for (volatile int i = 0; i < 1000000; i++) {
                __asm__ volatile("nop");  // Prevents compiler optimizations
            }
        }
    }

    close(sockfd);
    return NULL;
}

void* poll_network_dummy(void* arg) {

    printf("Polling network interface: %s (100%% CPU Usage)\n", INTERFACE);

    // Continuous polling loop (no blocking)
    while (1) {
        // Keep CPU busy with artificial work to force 100% usage
        for (volatile int i = 0; i < 1000000; i++) {
            __asm__ volatile("nop");  // Prevents compiler optimizations
        }
    }
    return NULL;
}

int main() {
    pthread_t thread;
    pthread_attr_t attr;
    pthread_t threads[NUM_THREADS];

    // Initialize thread attributes
    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_RR);

    // Create the polling thread
    threadCreate(&thread, poll_network_dummy, NULL, "net-poll-2", 2, OAI_PRIORITY_RT_LOW);

    int cpu;
    char threadname[30];

    for (int i = 0; i < NUM_THREADS; i++) {
        cpu = 3 + (i);
        snprintf(threadname, sizeof(threadname), "net-poll-%d", cpu);
        threadCreate(&threads[i], poll_network_dummy, NULL, threadname, cpu, OAI_PRIORITY_RT_LOW);
        
    }

    // Wait for the thread
    pthread_join(thread, NULL);
    return EXIT_SUCCESS;
}