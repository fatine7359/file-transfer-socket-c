#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <dirent.h>

void bzero(void *a, size_t n)
{
    memset(a, 0, n);
}

void bcopy(const void *src, void *dest, size_t n)
{
    memmove(dest, src, n);
}

struct sockaddr_in *init_sockaddr_in(uint16_t port_number)
{
    struct sockaddr_in *socket_address = malloc(sizeof(struct sockaddr_in));
    memset(socket_address, 0, sizeof(*socket_address));
    socket_address->sin_family = AF_INET;
    socket_address->sin_addr.s_addr = htonl(INADDR_ANY);
    socket_address->sin_port = htons(port_number);
    return socket_address;
}

char *process_operation(char *input)
{
    size_t n = strlen(input) * sizeof(char);
    char *output = malloc(n);
    memcpy(output, input, n);
    return output;
}

void send_file(FILE *fp, int sockfd)
{
    int n;
    char data[1024] = {0};

    while (fgets(data, 1024, fp) != NULL)
    {
        printf("data %s", data);
        if (send(sockfd, data, sizeof(data), 0) == -1)
        {
            perror("[-]Error in sending file.");
            exit(1);
        }
        bzero(data, 1024);
    }
}

int main(int argc, char *argv[])
{
    const uint16_t port_number = 5002;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in *server_sockaddr = init_sockaddr_in(port_number);
    struct sockaddr_in *client_sockaddr = malloc(sizeof(struct sockaddr_in));
    socklen_t server_socklen = sizeof(*server_sockaddr);
    socklen_t client_socklen = sizeof(*client_sockaddr);

    if (bind(server_fd, (const struct sockaddr *)server_sockaddr, server_socklen) < 0)
    {
        printf("Error! Bind has failed\n");
        exit(0);
    }
    if (listen(server_fd, 3) < 0)
    {
        printf("Error! Can't listen\n");
        exit(0);
    }

    const size_t buffer_len = 256;
    char *buffer = malloc(buffer_len * sizeof(char));
    char *response = NULL;
    time_t last_operation;
    __pid_t pid = -1;

    while (1)
    {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_sockaddr, &client_socklen);

        pid = fork();

        if (pid == 0)
        {
            close(server_fd);

            if (client_fd == -1)
            {
                exit(0);
            }

            printf("Connection with `%d` has been established and delegated to the process %d.\nWaiting for a query...\n", client_fd, getpid());

            last_operation = clock();

            while (1)
            {
                bzero(buffer, buffer_len * sizeof(char));
                read(client_fd, buffer, buffer_len);

                if (strcmp(buffer, "close\n") == 0)
                {
                    printf("Process %d: ", getpid());
                    send(client_fd, "quit", strlen("quit"), 0);
                    close(client_fd);
                    printf("Closing session with `%d`. Bye!\n", client_fd);
                    break;
                }

                if (strcmp(buffer, "files\n") == 0)
                {
                    char declaration[] = "Files:";
                    send(client_fd, declaration, strlen(declaration), 0);
                    DIR *d;
                    struct dirent *dir;
                    d = opendir("./server_files");
                    if (d)
                    {
                        while ((dir = readdir(d)) != NULL)
                        {
                            printf("%s\n", dir->d_name);
                            send(client_fd, dir->d_name, strlen(dir->d_name), 0);
                        }
                        closedir(d);
                    }
                    send(client_fd, "end", strlen("end"), 0);
                    continue;
                }

                if (strcmp(buffer, "download\n") == 0)
                {
                    char question[] = "choice:";
                    send(client_fd, question, strlen(question), 0);
                    bzero(buffer, buffer_len * sizeof(char));
                    read(client_fd, buffer, buffer_len);
                    char filename[64] = "./server_files/";
                    strcat(filename, buffer);
                    filename[strlen(filename) - 1] = '\0';
                    printf("the file is: %s", filename);
                    FILE *fp = fopen(filename, "r");
                    if (fp == NULL)
                    {
                        perror("[-]Error in reading file.");
                        exit(1);
                    }

                    send_file(fp, client_fd);
                    printf("[+]File data sent successfully.\n");
                }

                if (strlen(buffer) == 0)
                {
                    clock_t d = clock() - last_operation;
                    double dif = 1.0 * d / CLOCKS_PER_SEC;

                    if (dif > 5.0)
                    {
                        printf("Process %d: ", getpid());
                        close(client_fd);
                        printf("Connection timed out after %.3lf seconds. ", dif);
                        printf("Closing session with `%d`. Bye!\n", client_fd);
                        break;
                    }

                    continue;
                }

                printf("Process %d: ", getpid());
                printf("Received `%s`. Processing... ", buffer);

                free(response);
                response = process_operation(buffer);
                bzero(buffer, buffer_len * sizeof(char));

                send(client_fd, response, strlen(response), 0);
                printf("Responded with `%s`. Waiting for a new query...\n", response);

                last_operation = clock();
            }
            exit(0);
        }
        else
        {
            close(client_fd);
        }
    }
}