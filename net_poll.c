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
#include <linux/if_ether.h>
#include <net/if.h>
#include <fcntl.h>
#include <errno.h>

#define INTERFACE "lo"

void* poll_network(void* arg) {
    int sockfd;
    char buffer[2048];
    struct sockaddr saddr;
    socklen_t saddr_len = sizeof(saddr);

    // Set thread name
    pthread_setname_np(pthread_self(), "NetPollThread");

    // Set CPU affinity to core 1
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(2, &cpuset);  // Bind to CPU core 1

    // Set the highest priority for the thread
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        perror("Failed to set thread priority");
    }

    // Set the lowest nice value (-20) for highest scheduling priority
    if (setpriority(PRIO_PROCESS, 0, -20) < 0) {
        perror("Failed to set nice value");
    }

    // Set non-blocking mode
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    // Create a raw socket to listen on the interface
    sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sockfd < 0) {
        perror("Socket creation failed");
        pthread_exit(NULL);
    }

    // Bind to the specified network interface
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, INTERFACE, IFNAMSIZ - 1);
    if (setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr)) < 0) {
        perror("Failed to bind to network interface");
        close(sockfd);
        pthread_exit(NULL);
    }

    printf("Polling network interface: %s\n", INTERFACE);

    while (1) {
        ssize_t len = recvfrom(sockfd, buffer, sizeof(buffer), 0, &saddr, &saddr_len);
        if (len > 0) {
            printf("Received packet: %ld bytes\n", len);
        } else {
            perror("Packet reception failed");
        }
    }

    close(sockfd);
    return NULL;
}

int main() {
    pthread_t thread;
    pthread_attr_t attr;
    struct sched_param param;

    // Initialize thread attributes
    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_attr_setschedparam(&attr, &param);

    // Create the polling thread
    if (pthread_create(&thread, &attr, poll_network, NULL) != 0) {
        perror("Thread creation failed");
        return EXIT_FAILURE;
    }

    // Wait for the thread to finish (it won't in this case)
    pthread_join(thread, NULL);
    
    return EXIT_SUCCESS;
}
