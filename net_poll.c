#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
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

void* poll_network(void* arg) {
    int sockfd;
    char buffer[2048];
    struct sockaddr saddr;
    socklen_t saddr_len = sizeof(saddr);

    // Set thread name
    pthread_setname_np(pthread_self(), "NetPollThread");

    // Set CPU affinity to core 2
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(2, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    // Set highest real-time priority
    struct sched_param param;
    param.sched_priority = 99;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    // Set lowest nice value (-20)
    setpriority(PRIO_PROCESS, 0, -20);

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
    int *cpu = (int *)arg;

    char thread_name[30];
    snprintf(thread_name, sizeof(thread_name), "NetPollThread%d", *cpu);
    // Set thread name
    pthread_setname_np(pthread_self(), thread_name);

    // Set CPU affinity to core 2
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(*cpu, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    // Set highest real-time priority
    struct sched_param param;
    param.sched_priority = 99;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    // Set lowest nice value (-20)
    setpriority(PRIO_PROCESS, 0, -20);

    printf("Polling on CPU: %d\n", *cpu);
    free(cpu);

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
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

    // Create the polling thread
    if (pthread_create(&thread, &attr, poll_network, NULL) != 0) {
        perror("Thread creation failed");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        int *cpu = malloc(sizeof(int));  // Allocate memory for each thread
        *cpu = 3 + (i);
        if (pthread_create(&threads[i], &attr, poll_network_dummy, (void *)cpu) != 0) {
            perror("Thread creation failed");
            free(cpu);
            return EXIT_FAILURE;
        }
    }

    // Wait for the thread
    pthread_join(thread, NULL);
    return EXIT_SUCCESS;
}