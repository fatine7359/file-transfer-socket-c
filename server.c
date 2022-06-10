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

    // Lire le contenu du fichier ligne par ligne pointé par fp et le mettre dans la variable data
    while (fgets(data, 1024, fp) != NULL)
    {
        printf("========> data %s", data);

        // Envoyer le contenu lu à travers la socket sockfd
        if (send(sockfd, data, sizeof(data), 0) == -1)
        {
            perror("[-]Error in sending file.");
            exit(1);
        }
        bzero(data, 1024);
    }

    // Indiquer la fin de l'envoie (vu qu'on envoie ligne par ligne donc à un moment donné, il faut
    // indiquer au client la fin de l'envoie)
    if (send(sockfd, "end", sizeof("end"), 0) == -1)
    {
        perror("[-]Error in sending file.");
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    const uint16_t port_number = 5001;
    // Creation de la socket coté serveur: AF_INET (domaine), SOCK_STREAM (Mode connecté), choix du protocole de transport (0==par défaut)
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in *server_sockaddr = init_sockaddr_in(port_number);
    struct sockaddr_in *client_sockaddr = malloc(sizeof(struct sockaddr_in));

    socklen_t server_socklen = sizeof(*server_sockaddr);
    socklen_t client_socklen = sizeof(*client_sockaddr);

    // bind du socket serveur
    if (bind(server_fd, (const struct sockaddr *)server_sockaddr, server_socklen) < 0)
    {
        printf("Error! Bind has failed\n");
        exit(0);
    }
    // listen du socket server
    // Le serveur est pret pour recevoir les requetes du client
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
        // permet à un serveur de recevoir la communication entrante (client),
        // Creer une nouvelle socket (socket de service)
        // Le srveur garde l'ancienne socket pour recevoir les prochaines requetes
        int client_fd = accept(server_fd, (struct sockaddr *)&client_sockaddr, &client_socklen);

        pid = fork();

        if (pid == 0)
        {
            // Le fils travaille avec la socket de service (client_fd)
            // Le père travaille avec la socket d'écoute (server_fd)
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

                // Lire à partir de la requete
                read(client_fd, buffer, buffer_len);

                // Si le client envoie une demande "close"
                if (strcmp(buffer, "close\n") == 0)
                {
                    printf("Process %d: ", getpid());
                    send(client_fd, "quit", strlen("quit"), 0);
                    close(client_fd);
                    printf("Closing session with `%d`. Bye!\n", client_fd);
                    break;
                }

                // Si le client demande la liste des fichiers disponibles au niveau serveur
                if (strcmp(buffer, "files\n") == 0)
                {
                    char declaration[] = "Files:";
                    // Le serveur envoie au client à travers la socket de service (client_fd)
                    send(client_fd, declaration, strlen(declaration), 0);
                    DIR *d;
                    struct dirent *dir;

                    // On accède au répertoire server_files
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

                // Si le client demande le download
                else if (strcmp(buffer, "download\n") == 0)
                {
                    char question[] = "choice:";
                    // Le seveur envoie au client choice pour demander le choix du fichier à transferer
                    send(client_fd, question, strlen(question), 0);
                    bzero(buffer, buffer_len * sizeof(char));
                    // Le serveur lit la requete du client
                    read(client_fd, buffer, buffer_len);
                    char filename[64] = "./server_files/";
                    strcat(filename, buffer);
                    filename[strlen(filename) - 1] = '\0';
                    printf("the file is: %s", filename);
                    // Le serveur ouvre le fichier demander par le client
                    FILE *fp = fopen(filename, "r");
                    if (fp == NULL)
                    {
                        perror("[-]Error in reading file.");
                        exit(1);
                    }
                    bzero(buffer, buffer_len * sizeof(char));
                    // Il fait appel à la fonction send_file() pour envoyer le fichier demander
                    send_file(fp, client_fd);
                    printf("[+]File data sent successfully.\n");
                }

                // Si rien n'est envoyé par le client
                else if (strlen(buffer) == 0)
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
                else
                {
                    printf("Process %d: ", getpid());
                    printf("Received `%s`. Processing... ", buffer);

                    free(response);
                    response = process_operation(buffer);
                    bzero(buffer, buffer_len * sizeof(char));

                    send(client_fd, response, strlen(response), 0);
                    printf("Responded with `%s`. Waiting for a new query...\n", response);
                }
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