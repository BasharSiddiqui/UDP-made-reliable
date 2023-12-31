#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <fcntl.h>
#include <time.h>

#define BUFFER_SIZE 500

struct packet {
    int sequence_no;
    int packet_size;
    char data[BUFFER_SIZE];
};

int socket_fd;
struct addrinfo serv_addr, *serv_info, *ptr;
struct sockaddr_storage cli_addr;
socklen_t cli_addr_len = sizeof(struct sockaddr_storage);
char ip_addr[INET6_ADDRSTRLEN];
int rv;

int no_of_bytes = 0;
int out;
int file_size;
int remaining = 0;
int received = 0;

int no_of_packets = 10;
struct packet temp_packet;
struct packet packets[10];
int no_of_acks;
int acks[10];
int temp_ack;

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

void *receivePackets(void *vargp) {
    for (int i = 0; i < no_of_packets; i++) {
    RECEIVE:
        if ((no_of_bytes = recvfrom(socket_fd, &temp_packet, sizeof(struct packet), 0, (struct sockaddr *)&cli_addr, &cli_addr_len)) < 0) {
            perror("UDP Server: recvfrom");
            exit(1);
        }

        if (packets[temp_packet.sequence_no].packet_size != 0) {
            // Process duplicate packet
            packets[temp_packet.sequence_no] = temp_packet;
            temp_ack = temp_packet.sequence_no;
            acks[temp_ack] = 1;

            if (sendto(socket_fd, &temp_ack, sizeof(int), 0, (struct sockaddr *)&cli_addr, cli_addr_len) < 0) {
                perror("UDP Server: sendto");
                exit(1);
            }
            // printf("Duplicate Ack Sent:%d\n", temp_ack);  // Commented out for faster processing

            goto RECEIVE;
        }

        if (temp_packet.packet_size == -1) {
            printf("END OF FILE REACHED\n");
            no_of_packets = temp_packet.sequence_no + 1;
             // Exit loop when end of file is reached
        }

        if (no_of_bytes > 0) {
            // printf("Packet Received:%d\n", temp_packet.sequence_no);  // Commented out for faster processing
            packets[temp_packet.sequence_no] = temp_packet;
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    memset(&serv_addr, 0, sizeof serv_addr);
    serv_addr.ai_family = AF_UNSPEC;
    serv_addr.ai_socktype = SOCK_DGRAM;
    serv_addr.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, argv[1], &serv_addr, &serv_info)) != 0) {
        fprintf(stderr, "UDP Server: getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for (ptr = serv_info; ptr != NULL; ptr = ptr->ai_next) {
        if ((socket_fd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) == -1) {
            perror("UDP Server: socket");
            continue;
        }

        if (bind(socket_fd, ptr->ai_addr, ptr->ai_addrlen) == -1) {
            close(socket_fd);
            perror("UDP Server: bind");
            continue;
        }

        break;
    }

    if (ptr == NULL) {
        fprintf(stderr, "UDP Server: Failed to bind socket\n");
        return 2;
    }

    freeaddrinfo(serv_info);

    printf("Server is listening at specified port...\n");

    pthread_t thread_id;

    struct timespec time1, time2;
    time1.tv_sec = 0;
    time1.tv_nsec = 30000000L;

    FILE *out = fopen("output_video.mp4", "wb");

    if ((no_of_bytes = recvfrom(socket_fd, &file_size, sizeof(off_t), 0, (struct sockaddr *)&cli_addr, &cli_addr_len)) < 0) {
        perror("UDP Server: recvfrom");
        exit(1);
    }
    printf("Size of Video File to be received: %d bytes\n", file_size);

    no_of_bytes = 1;
    remaining = file_size;
    int window_count = 1;

    while (remaining > 0 || (no_of_packets == 10)) {
        memset(packets, 0, sizeof(packets));
        for (int i = 0; i < 5; i++) {
            packets[i].packet_size = 0;
        }

        for (int i = 0; i < 10; i++) {
            acks[i] = 0;
        }

        pthread_create(&thread_id, NULL, receivePackets, NULL);

        nanosleep(&time1, &time2);

        no_of_acks = 0;

        RESEND_ACK:
        for (int i = 0; i < no_of_packets; i++) {
            temp_ack = packets[i].sequence_no;
            if (acks[temp_ack] != 1) {
                if (packets[i].packet_size != 0) {
                    acks[temp_ack] = 1;

                    if (sendto(socket_fd, &temp_ack, sizeof(int), 0, (struct sockaddr *)&cli_addr, cli_addr_len) > 0) {
                        no_of_acks++;
                        // printf("Ack sent: %d\n", temp_ack);  // Commented out for faster processing
                    }
                }
            }
        }

        nanosleep(&time1, &time2);
        nanosleep(&time1, &time2);

        if (no_of_acks < no_of_packets) {
            goto RESEND_ACK;
        }

        pthread_join(thread_id, NULL);

        printf("Window Frame %d Transferred: %d%%\n", window_count++, (received * 100) / file_size);

        for (int i = 0; i < no_of_packets; i++) {
            if (packets[i].packet_size != 0 && packets[i].packet_size != -1) {
                fwrite(packets[i].data, 1, packets[i].packet_size, out);
                remaining = remaining - packets[i].packet_size;
                received = received + packets[i].packet_size;
            }
        }
    }

    printf("File transfer completed successfully!\n");

    close(socket_fd);

    return 0;
}

