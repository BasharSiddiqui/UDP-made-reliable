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
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

#define SERVER_PORT "4950"
#define BUFFER_SIZE 500
#define WINDOW_SIZE 10

struct packet {
    int sequence_no;
    int packet_size;
    char data[BUFFER_SIZE];
};

int socket_fd;
struct addrinfo serv_addr, *serv_info, *ptr;
struct sockaddr_storage server_addr;
socklen_t server_addr_len = sizeof(struct sockaddr_storage);
int rv;

int data;
int no_of_bytes;
int in;
struct stat file_stat;
int fd;
off_t file_size;

struct packet packets[10];
int temp_seq_no = 1;
int no_of_acks;
int temp_ack;
int acks[10];
int no_of_packets = 10;

void *receiveAcks(void *vargp) {
    for (int i = 0; i < no_of_packets; i++) {
    RECEIVE:
        if ((no_of_bytes = recvfrom(socket_fd, &temp_ack, sizeof(int), 0, (struct sockaddr *)&server_addr, &server_addr_len)) < 0) {
            perror("UDP Client: recvfrom");
            exit(1);
        }

        if (acks[temp_ack] == 1) {
            goto RECEIVE;
        }

        // printf("Ack Received: %d\n", temp_ack);  // Commented out for faster processing
        acks[temp_ack] = 1;
        no_of_acks++;
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "UDP Client: usage: Client target_ip video_name\n");
        exit(1);
    }

    memset(&serv_addr, 0, sizeof serv_addr);
    serv_addr.ai_family = AF_UNSPEC;
    serv_addr.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(argv[1], SERVER_PORT, &serv_addr, &serv_info)) != 0) {
        fprintf(stderr, "UDP Client: getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for (ptr = serv_info; ptr != NULL; ptr = ptr->ai_next) {
        if ((socket_fd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) == -1) {
            perror("UDP Client: socket");
            continue;
        }

        break;
    }

    if (ptr == NULL) {
        fprintf(stderr, "UDP Client: Failed to create socket\n");
        return 2;
    }

    memset(&server_addr, 0, sizeof server_addr);
    server_addr_len = sizeof(struct sockaddr_storage);

    pthread_t thread_id;

    struct timespec time1, time2;
    time1.tv_sec = 0;
    time1.tv_nsec = 300000000L;

    FILE *in = fopen(argv[2], "rb");

    if (in == NULL) {
        perror("Error in opening the video file.\n");
        return 0;
    }

    fd = fileno(in);
    fstat(fd, &file_stat);
    file_size = file_stat.st_size;
    printf("Size of Video File: %d bytes\n", (int)file_size);

    FILESIZE:
    if (sendto(socket_fd, &file_size, sizeof(off_t), 0, ptr->ai_addr, ptr->ai_addrlen) < 0) {
        goto FILESIZE;
    }

    data = 1;
    int window_count = 1;

    while (data > 0) {
        temp_seq_no = 0;
        printf("Window Frame %d\n", window_count);

        for (int i = 0; i < no_of_packets; i++) {
            data = fread(packets[i].data, 1, BUFFER_SIZE, in);
            packets[i].sequence_no = temp_seq_no;
            packets[i].packet_size = data;
            temp_seq_no++;

            if (data == 0) {
                printf("End of file reached.\n");
                packets[i].packet_size = -1;
                no_of_packets = i + 1;
                break;
            }
        }

        for (int i = 0; i < no_of_packets; i++) {
            // printf("Sending packet %d\n", packets[i].sequence_no);  // Commented out for faster processing
            if (sendto(socket_fd, &packets[i], sizeof(struct packet), 0, ptr->ai_addr, ptr->ai_addrlen) < 0) {
                perror("UDP Client: sendto");
                exit(1);
            }
        }

        for (int i = 0; i < no_of_packets; i++) {
            acks[i] = 0;
        }

        no_of_acks = 0;

        pthread_create(&thread_id, NULL, receiveAcks, NULL);

        nanosleep(&time1, &time2);

        RESEND:
        for (int i = 0; i < no_of_packets; i++) {
            if (acks[i] == 0) {
                // printf("Sending missing packet: %d\n", packets[i].sequence_no);  // Commented out for faster processing
                if (sendto(socket_fd, &packets[i], sizeof(struct packet), 0, ptr->ai_addr, ptr->ai_addrlen) < 0) {
                    perror("UDP Client: sendto");
                    exit(1);
                }
            }
        }

        nanosleep(&time1, &time2);

        if (no_of_acks != no_of_packets) {
            goto RESEND;
        }

        pthread_join(thread_id, NULL);
        window_count++;
    }

    freeaddrinfo(serv_info);
    printf("\nFile transferred to receiver on specified IP!\n");

    close(socket_fd);
    return 0;
}

