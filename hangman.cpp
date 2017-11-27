/*
 * A C++ server program for the hangman program
 * This program receives two arguments:
 * Port number: that it should listen on for incoming connections
 * Document root: the directory out of which it will serve files
 * */

#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <dirent.h>
#include <vector>
#include <iostream>

#define BACKLOG(10)

using namespace std;

typedef struct {
    int connected; /* 0 - Not connected/logged in; 1 - Connected/logged in */
    int game; /* 0 - Game not in progress; 1- Game in progress */
    int commands[30]; /* 0-25 - Letters A-Z; 26 - Start; 27 - Quit
                       * 28 - Login; 29- Logout */
    string word; /* Word selected at random from the dictionary */
    vector<char> guessed; /* Contains the letters that have been guessed */
} client;

typedef struct {
    string username;
    string password; /* Could be hashed or salted or otherwise secured */
    int wins; /* 0 - not connected/logged in; 1 - connected/logged in */
    int losses; /* 0 - Game not running; 1- Game is running */
    int commands[27]; /* [0 - Start Game; 1-26: Letters A-Z; 27 - Quit] */
} user;

void *thread_function(void *argument);
void processClient(int sock);

int pathFile(FILE *file, int sock, char filetype[], char mediatype[], char code[], char header_response[]);
int pathDirIndex(FILE *file, int sock, char mediatype[], char code[], char header_response[]);
int pathDirNoIndex(char path[], int sock, char code[], char header_response[]);
int noPath(int sock, char code[], char header_response[]);

int is_dir(const char *path);
int is_file(const char *path);

int main(int argc, char **argv) {
    /* For checking return values. */
    int retval;

    /* Chcek number of arguments. */
    if (argc != 3) {
        printf("Usage: %s <port> <document root>\n", argv[0]);
        exit(1);
    }

    /* Read the port number from the first command line argument. */
    int port = atoi(argv[1]);

    printf("Webserver configuration:\n");
    printf("\tPort: %d\n", port);
    printf("\tDocument root: %s\n", argv[2]);

    /* changes working directory to document root */
    retval = chdir(argv[2]);
    if(retval != 0){
        printf("Changing working directory failed");
        exit(1);
    }

    /* Create a socket to which clients will connect. */
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(server_sock < 0) {
        perror("Creating socket failed");
        exit(1);
    }

    /* A server socket is bound to a port, which it will listen on for incoming
     * connections.  By default, when a bound socket is closed, the OS waits a
     * couple of minutes before allowing the port to be re-used.  This is
     * inconvenient when you're developing an application, since it means that
     * you have to wait a minute or two after you run to try things again, so
     * we can disable the wait time by setting a socket option called
     * SO_REUSEADDR, which tells the OS that we want to be able to immediately
     * re-bind to that same port. See the socket(7) man page ("man 7 socket")
     * and setsockopt(2) pages for more details about socket options. */
    int reuse_true = 1;
    retval = setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse_true,
                        sizeof(reuse_true));
    if (retval < 0) {
        perror("Setting socket option failed");
        exit(1);
    }

    /* Create an address structure.  This is very similar to what we saw on the
     * client side, only this time, we're not telling the OS where to connect,
     * we're telling it to bind to a particular address and port to receive
     * incoming connections.  Like the client side, we must use htons() to put
     * the port number in network byte order.  When specifying the IP address,
     * we use a special constant, INADDR_ANY, which tells the OS to bind to all
     * of the system's addresses.  If your machine has multiple network
     * interfaces, and you only wanted to accept connections from one of them,
     * you could supply the address of the interface you wanted to use here. */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    /* As its name implies, this system call asks the OS to bind the socket to
     * address and port specified above. */
    retval = bind(server_sock, (struct sockaddr *)&addr, sizeof(addr));
    if(retval < 0) {
        perror("Error binding to port");
        exit(1);
    }

    /* Now that we've bound to an address and port, we tell the OS that we're
     * ready to start listening for client connections.  This effectively
     * activates the server socket.  BACKLOG (#defined above) tells the OS how
     * much space to reserve for incoming connections that have not yet been
     * accepted. */
    retval = listen(server_sock, BACKLOG);
    if(retval < 0) {
        perror("Error listening for connections");
        exit(1);
    }

    while(1) {

        /* Setting up socket connection */

        int sock;

        struct sockaddr_in remote_addr;
        unsigned int socklen = sizeof(remote_addr);

        sock = accept(server_sock, (struct sockaddr *) &remote_addr, &socklen);
        if(sock < 0) {
            perror("Error accepting connection\n");
            exit(1);
        }


        /* Threading */

        pthread_t new_thread;

        int *argument = malloc(sizeof(int));
        if(argument == NULL){
		        perror("malloc failed\n");
		        exit(1);
	      }
        *argument = sock;

        int retval = pthread_create(&new_thread, NULL,
                                    thread_function, argument);
        if (retval) {
            printf("pthread_create() failed\n");
            exit(1);
        }

        retval = pthread_detach(new_thread);
        if (retval) {
            printf("pthread_detach() failed\n");
            exit(1);
        }
    }
}

/* thread_function
 * Input: argument
 * Purpose: runs processClient in a new thread for concurrent processing
 */
void *thread_function(void *argument) {
     int *sock = (int *) argument;
     processClient(*sock);
     /* Free the memory that was allocated to store our thread argument */
     free(sock);
     return NULL;
 }


/* processClient
 * Input: sock
 * Purpose:
 *    - receives and parses a request
 *    - Updates client and page and returns appropriate response
 */
void processClient(int sock) {
    char request[4096]; // Each request is 8 bytes long;
    memset(request,0,sizeof(request));
    char buf[1024];
    memset(buf,0,sizeof(buf));
    int total = 0;
    int recv_count = -1;
    char code[4];
    memset(code,0,sizeof(code));
    char filetype[16]; // .html, .txt, etc
    char mediatype[16]; // Content-Type
    char* pathStart;
    char* pathEnd;
    char* temp;
    char path[100]; // Assumption?
    FILE *file;

    /* Receives the request until \r\n\r\n" is found */
    while (end == false) {
       recv_count = recv(sock, buf, 1024, 0);
       if (recv_count < 0) {
          perror("recv");
          exit(1);
       }
       if (recv_count == 0) {
          close(sock);
          return;
       }
       memcpy(&request[recvOffset], buf, recv_count);
       if (strstr(request, "\r\n\r\n") != NULL) {
           end = true;
       }
       memset(buf,0,sizeof(buf));
       recvOffset += recv_count;
    }

    /* Get path from request */
    pathStart = strstr(request, "GET ");
    pathStart = pathStart + 4;
    pathEnd = strstr(pathStart, " HTTP/");
    strncpy(path, pathStart, pathEnd-pathStart);
    path[pathEnd-pathStart] = '\0';
    cout << path << endl;

    /* Covers the four cases:
       1. If the path is a directory, returns index.html if exists
       2. If the path is a directory and no index.html exists, return a
          directory listing
       3. If the path is a file, and the file exists, return the file
       4. If anything else, return 404 error page.
     */
    /*
    if (is_dir(path)) {
        char pathIndexHTML[100];
        // add index.html to end of path
        sprintf(pathIndexHTML, "%sindex.html", path);
        if (strcmp(pathIndexHTML, "/index.html") == 0) { // default directory
            //get rid of starting slash
            memmove(pathIndexHTML, pathIndexHTML+1, strlen(pathIndexHTML));
        }

        if ((file = fopen(pathIndexHTML, "r")) != NULL) {
            //path exists, directory containing index.html, respond with file
            strcpy(code, "200");
            pathDirIndex(file, sock, mediatype, code, header_response);
        }
        else {
            //path exists but no index.html, respond with directory listing
            strcpy(code, "200");
            pathDirNoIndex(path, sock, code, header_response);
        }
    }
    else if (is_file(path)) {
        //path and file exists, send back response to client
        file = fopen(path, "r");
        strcpy(code, "200");
        pathFile(file, sock, filetype, mediatype, code, header_response);
    }
    else {
        //path does not exist, respond with 404 page
        strcpy(code, "404");
        noPath(sock, code, header_response);
    }
    */
    close(sock);
}
