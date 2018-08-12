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
#include <dirent.h>
#include <vector>
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <time.h>
#include <algorithm>
#include <ctype.h>
#include <sstream>
#include <assert.h>

using namespace std;

template <typename T>
std::string NumberToString ( T Number )
{
   std::ostringstream ss;
   ss << Number;
   return ss.str();
}

/* User class. Contains each user's game state and connection to server */
class User {
    public:

    string username;
    string password; /* Could be hashed or salted or otherwise secured */
    int wins;
    int total;
    int connected; /* 0 - Not connected/logged in; 1 - Connected/logged in */
    int game; /* 0 - Game not in progress; 1 - Game in progress; 2 - Lost game*/
    string word; /* Word selected at random from the dictionary */
    int guessed[26]; /* Contains guessed letters: 0 if not guessed, 1 if guessed */
    int guesses;
    int repeat;
};

/* array of users, arbitrarily set to 10 */
User users[10];

void *thread_function(void *argument);
void processClient(int sock);

int sendPage(FILE *file, int sock, string code, string header, string filetype);
int send404(int sock, string code, string header);
int sendGame(User *curUser, int sock, string code, string header, string filetype);
string createGame(User *curUser, int sock);

int main(int argc, char **argv) {

    /* DEBUGGING/LOGIC TESTS */
    cout << "Begin debugging" << endl;
    cout << "Testing alphabet indices------------";
    // Ensure unicode index shifted to 0-25
    string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; i<26; i++) {
        assert((int)alphabet[i] - 65 == i);
    }
    cout << "OK!" << endl;
    cout << "Testing guess logic------------";
    //code taken from below function, same as game logic code
    string hangman_word = "";
    string test_word = "TESTING";
    int test_guesses[26] = {0};
    test_guesses[4] = 1; //guess E
    for(int i = 0; i < (int)test_word.length(); i++) {
        if (test_guesses[(int)test_word[i]-65] != 0) {
            hangman_word += test_word[i];
        }
        else {
          hangman_word += "_";
        }
    }
    assert(hangman_word == "_E_____");
    hangman_word = "";
    test_guesses[19] = 1; // Guess T
    for(int i = 0; i < (int)test_word.length(); i++) {
        if (test_guesses[(int)test_word[i]-65] != 0) {
            hangman_word += test_word[i];
        }
        else {
          hangman_word += "_";
        }
    }
    assert(hangman_word == "TE_T___");
    cout << "OK!" << endl;
    hangman_word = "";
    test_guesses[18] = 1; // Guess S
    test_guesses[8] = 1; // Guess I
    test_guesses[13] = 1; // Guess N
    test_guesses[6] = 1; // Guess G
    cout << "Testing win scenario-------------";
    int gamewin = 1;
    for(int i = 0; i < (int)test_word.length(); i++) {
        if (test_guesses[(int)test_word[i]-65] != 0) {
            hangman_word += test_word[i];
        }
        else {
          gamewin = 0;
          hangman_word += "_";
        }
    }
    assert(hangman_word == "TESTING");
    assert(gamewin == 1);
    cout << "OK!" << endl;
    cout << "Testing lose scenario-----------";
    int badguesses = 9;
    string guessz = "Z";
    if (hangman_word.find(guessz) == std::string::npos) {
        badguesses += 1;
    }
    assert(badguesses > 9);
    cout << "OK!" << endl;

    /* End testing */

    for (int i = 0; i < 10; i++) {
        users[i].username = "user" + NumberToString(i);
        users[i].password = "password" + NumberToString(i);
    }
    users[0].username = "admin";
    users[0].password = "password";
    users[0].wins = 0;
    users[0].total = 0;

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

    /* Socket programming */

    int reuse_true = 1;
    retval = setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse_true,
                        sizeof(reuse_true));
    if (retval < 0) {
        perror("Setting socket option failed");
        exit(1);
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    retval = bind(server_sock, (struct sockaddr *)&addr, sizeof(addr));
    if(retval < 0) {
        perror("Error binding to port");
        exit(1);
    }

    retval = listen(server_sock, 10);
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

        int *argument = new int;
        if(argument == NULL){
		        perror("Failed allocation\n");
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

    string request;
    string method;
    char buf[1024];
    memset(buf,0,sizeof(buf));
    int recv_count = -1;
    string code;
    int recvOffset = 0;
    FILE *file;
    string path;
    string header;
    string filetype;
    int pos;
    int pos2;
    int pos3;
    bool end = false;
    string currentUser = "";
    User *curUser;

    // Read in the request from the client
    while(end == false) {
        recv_count = recv(sock, buf, 1024, 0);
        if (recv_count < 0) {
           perror("recv");
           exit(1);
        }
        if (recv_count == 0) {
           close(sock);
           return;
        }
        request += buf;
        memset(buf,0,sizeof(buf));
        recvOffset += recv_count;
        if (recv_count < 1024) {
          end = true;
        }
    }

    /* Get method/path/data */
    if ((request[0] == 'G') && (request[1] == 'E') && (request[2] == 'T')) {
        method = "GET";
    }
    if ((request[0] == 'P') && (request[1] == 'O') && (request[2] == 'S') && (request[3] == 'T')) {
        method = "POST";
    }

    // If GET request, grab login page file or other requested files
    if (method == "GET") {

        pos = request.find("GET ");
        pos += 4;
        pos2 = request.find("HTTP/");
        path = request.substr(pos+1, pos2-pos-2);

        /* Get filetype if applicable (.txt, .html, .pdf) */
        if (path.find_last_of(".") != std::string::npos) {
            pos3 = path.find_last_of(".");
            filetype = path.substr(pos3+1);
        }
        if(filetype == "html"){
            filetype = "text/html";
        }
        else if(filetype == "txt") {
            filetype = "text/plain";
        }
        else if(filetype == "jpeg") {
            filetype = "image/jpeg";
        }
        else if(filetype == "jpg") {
            filetype = "image/jpg";
        }
        else if(filetype == "gif") {
            filetype = "image/gif";
        }
        else if(filetype == "png") {
            filetype = "image/png";
        }
        // If the file exists, send back that file
        if ((file = fopen(path.c_str(), "r")) != NULL) {
            code = "200";
            sendPage(file, sock, code, header, filetype);
        }
        // Otherwise, default to sending back the login page
        else if ((file = fopen("login.html", "r")) != NULL) {
            // Give login page
            code = "200";
            sendPage(file, sock, code, header, filetype);
        }
        else {
            // Give 404 if neither login page nor file are found
            code = "404";
            send404(sock, code, header);
        }
    }

    /* If request is not a GET, it must be a POST from the game page.
     * Every POST request related to changing game state (guess, new game, logout)
     * contains the curuser input in the body of the request. By default, it is set
     * to $$$, interpreted as %24%24%24. There should be no user with this username,
     * so upon seeing this we know that we are handling a login. Otherwise, we read
     * the username associated with the request and change the state on our server
     * for that particular user, marked as 'curUser'.
     */
    else if (method == "POST") {
        //There should be a currentUser= in every post request
        if (request.find("currentUser=") == std::string::npos) {
            if ((file = fopen("login.html", "r")) != NULL) {
                // Give login page
                code = "200";
                sendPage(file, sock, code, header, filetype);
            }
            else {
                // Give 404 if login page not found
                send404(sock, code, header);
            }
        }
        else { // POST contains currentUser=, handle cases
            pos = request.find("currentUser=");
            pos += 12;
            pos2 = request.find("&");
            currentUser = request.substr(pos, pos2-pos);
            cout << "current user: " << currentUser << endl;
            for (int i = 0; i < 10; i++) {
                if (currentUser == users[i].username) {
                    curUser = &users[i];
                }
            }
        }

        // Handling login. If the request also contains uname and psw in the
        // body, we try a login with those values.
        if ((method == "POST") && (request.find("uname=") != std::string::npos) &&
                 (request.find("psw=") != std::string::npos)) {
            pos = request.find("uname=");
            pos2 = request.find("psw=");
            pos3 = request.find("HTTP/");
            string username = request.substr(pos+6, pos2-pos-7);
            string password = request.substr(pos2+4, pos3-pos2-5);
            // Scan through users, give main page when correct login
            for (int i = 0; i < 10; i++) {
                if (username == users[i].username) {
                    if (password == users[i].password) {
                        curUser = &users[i];
                        currentUser = users[i].username;

                        if (users[i].connected == 0) {
                            cout << "Logging in user: " << users[i].username << endl;
                            code = "200";
                            sendGame(curUser, sock, code, header, filetype);
                            curUser->connected = 1;
                        }
                        // If the user is already logged in, don't let them login twice.
                        else {
                            if ((file = fopen("login.html", "r")) != NULL) {
                                code = "200";
                                sendPage(file, sock, code, header, filetype);
                            }
                            else {
                                code = "404";
                                send404(sock, code, header);
                            }
                        }
                    }
                }
            }
            // Give login page again if incorrect values
            if (currentUser == "%24%24%24") { //default value, did not find a user
                if ((file = fopen("login.html", "r")) != NULL) {
                    code = "200";
                    sendPage(file, sock, code, header, filetype);
                }
                else {
                    code = "404";
                    send404(sock, code, header);
                }
            }
        }

        // Handling guesses if a game is running
        else if ((method == "POST") && (request.find("guessedLetter=") != std::string::npos)
                  && (currentUser != "") && (curUser->game == 1)) {

            pos = request.find("guessedLetter=");
            pos += 14;
            char guessedLetter = request[pos];
            cout << "The letter guessed was: " << guessedLetter << endl;
            if (isalpha(guessedLetter)) {
                if (!isupper(guessedLetter)) {
                    cout << "Lowercase letter detected; converting to upper" << endl;
                    guessedLetter = toupper(guessedLetter);
                }
                if (curUser->guessed[(int)guessedLetter-65] == 1) { // Letter has already been guessed
                    curUser->repeat = 1;
                }
                else { //Mark a letter as guessed, increment guesses if letter not in word
                    curUser->guessed[(int)guessedLetter-65] = 1;
                    if (curUser->word.find(guessedLetter) == std::string::npos) {
                        curUser->guesses += 1;
                    }
                    if (curUser->guesses > 9) { // Out of guesses, game over
                        curUser->game = 2;
                    }
                }
            }
            sendGame(curUser, sock, code, header, filetype);
        }

        // Handling a request to start a new game
        else if ((method == "POST") && (request.find("startnewgame=") != std::string::npos) && (curUser->connected == 1)) {
            cout << "Starting new game!" << endl;
            // Get a random word from the dictionary text file
            ifstream File("words.txt");
            if (!File.is_open()) {
              cout << "Could not open file!" << endl;
              exit(1);
            }
            string word;
            srand ( time(NULL) );
            int random = rand() % 99154;
            int numwords = 0;
            string line;
            while(File >> word) {
              if(numwords == random) {
                break;
              }
              numwords++;
            }
            std::transform(word.begin(), word.end(), word.begin(), ::toupper);
            cout << "This game's word is: " << word << endl;
            curUser->word = word;
            curUser->game = 1;
            // Set game state to 1, reset guesses/arrays to 0;
            curUser->guesses = 0;
            fill(curUser->guessed, curUser->guessed+26, 0);
            code = "200";
            // Increment total games
            curUser->total += 1;
            sendGame(curUser, sock, code, header, filetype);
        }

        // Handling logout request
        else if ((method == "POST") && (request.find("logoutcuruser=") != std::string::npos)
                        && (curUser->connected == 1)) {
            cout << "Logging out!" << endl;
            // Clear the abandoned current game, in case one is running
            curUser->game = 0;
            fill(curUser->guessed, curUser->guessed+26, 0); //clear guessed array
            curUser->connected = 0;

            if ((file = fopen("login.html", "r")) != NULL) {
                // Give login page
                code = "200";
                sendPage(file, sock, code, header, filetype);
            }
            else {
                // Give 404 if login page not found
                send404(sock, code, header);
            }
        }
    }

    else { // Something went wrong.
        cout << "Something wrong" << endl;
        cout << request << endl;
        // Page does not exist, respond with 404 page
        code = "404";
        send404(sock, code, header);
    }

    close(sock);
}

/* sendPage:
 * Gives the requested page or file to the user if it exists, 404 otherwise
 */
int sendPage(FILE *file, int sock, string code, string header, string filetype) {

    char buf[1024];
    memset(buf,0,sizeof(buf));
    header = "HTTP/1.1 " + code + " OK\r\nServer: Zhiyuan Liu's Hangman\r\nContent-Type: " + filetype + "\r\n\r\n";
    /* Sends header_response */
    char * header_response = new char [header.length()+1];
    strcpy (header_response, header.c_str());
    int ret;
    int sent;
    int sendOffset = 0;
    while(sendOffset != (int)strlen(header_response)){
        ret = send(sock, &header_response[sendOffset], strlen(header_response) - sendOffset, 0);
        //on success, returns number of characters sent
        if(ret < 0){
            perror("send");
            exit(1);
        }
        sendOffset += ret;
    }
    delete header_response;

    /* Copies bytes from file into buffer then sends */

    while((ret = fread(buf, 1, 1024, file)) != 0){
        if(ret < 0){
            perror("send");
            exit(1);
        }
        sendOffset = 0;
        while(((sent = send(sock, &buf[sendOffset], ret - sendOffset, 0)) != 0)){
            //on success, returns number of bytes read
            if(sent < 0){
                perror("send");
                exit(1);
            }
            sendOffset += sent;
        }
    }
    fclose(file);
    return 0;
}

/* send404:
 * Returns a 404 page
 */
int send404(int sock, string code, string header) {

    string errorPage = "<html><body><h1>404: Page Not Found :(</h1></body></html>";
    char * cerrorPage = new char[errorPage.length()+1];
    strcpy (cerrorPage, errorPage.c_str());

    header = "HTTP/1.1 404 Not Found\r\nServer: Zhiyuan Liu's Hangman\r\n\r\n";

    /* Sends header_response */
    char * header_response = new char [header.length()+1];
    strcpy (header_response, header.c_str());
    int ret;
    int sendOffset = 0;
    while(sendOffset != (int)strlen(header_response)){
        ret = send(sock, &header_response[sendOffset], strlen(header_response) - sendOffset, 0);
        //on success, returns number of characters sent
        if(ret < 0){
            perror("send");
            exit(1);
        }
        sendOffset += ret;
    }

    delete header_response;

    /* Sends errorPage */
    sendOffset = 0;
    while(sendOffset != (int)strlen(cerrorPage)){
        ret = send(sock, &cerrorPage[sendOffset], strlen(cerrorPage) - sendOffset, 0);
        //on success, returns number of characters sent
        if(ret < 0){
            perror("send");
            exit(1);
        }
        sendOffset += ret;
    }

    delete cerrorPage;
    return 0;
}

/* sendGame:
 * Returns the main game page
 * Dynamically updates based on current user's game state, stored in User class
 */
int sendGame(User *curUser, int sock, string code, string header, string filetype) {
    string game;
    //Generating the page
    string top = "<!DOCTYPE html> <html>"
      "<head>"
      "<style>"
      "#title {"
        "font-family: arial;"
        "font-size: 40pt;"
        "text-align: center;"
      "}"
      "#end {"
        "font-family: sans-serif;"
        "text-align: center;"
        "font-size: 30pt;"
        "position: absolute;"
        "top: 75%;"
        "left: 50%;"
        "transform: translateX(-50%) translateY(-50%);"
      "}"
      "#newgame {"
        "position: absolute;"
        "left: 50px;"
        "top: 5px;"
        "font-family: arial;"
        "font-size: 15pt;"
        "border: 2px solid black;"
        "text-align: center;"
        "vertical-align: middle;"
        "width: 150px;"
        "line-height: 75px;"
        "height: 75px;"
        "cursor: pointer;"
        "fill: white;"
      "}"
      "#logout {"
        "position: absolute;"
        "right: 50px;"
        "top: 5px;"
        "font-family: arial;"
        "font-size: 15pt;"
        "border: 2px solid black;"
        "text-align: center;"
        "vertical-align: middle;"
        "width: 150px;"
        "line-height: 75px;"
        "height: 75px;"
        "cursor: pointer;"
        "fill: white;"
      "}"
      "#word {"
        "font-family: sans-serif;"
        "font-size: 75pt;"
        "text-align: center;"
        "letter-spacing: 50px;"
        "position: absolute;"
        "top: 60%;"
        "left: 50%;"
        "transform: translateX(-50%) translateY(-50%);"
      "}"
      "#guessform {"
        "text-align: center;"
        "position: absolute;"
        "top: 80%;"
        "left: 50%;"
        "transform: translateX(-50%) translateY(-50%);"
      "}"
      "#guessedLetter {"
        "font-family: sans-serif;"
        "width: 20px;"
      "}"
      "#repeat {"
        "text-align: center;"
        "position: absolute;"
        "top: 85%;"
        "left: 50%;"
        "transform: translateX(-50%) translateY(-50%);"
      "}"
      "#guessedNum {"
        "text-align: center;"
        "position: absolute;"
        "top: 85%;"
        "left: 20%;"
        "transform: translateX(-50%) translateY(-50%);"
      "}"
      "#info {"
        "text-align: left;"
        "position: absolute;"
        "top: 95%;"
        "left: 20%;"
      "}"
      "#picture {"
        "height: auto;"
        "width: auto;"
        "max-width: 500px;"
        "max-height: 500px;"
        "position: absolute;"
        "top: 40%;"
        "left: 50%;"
        "transform: translateX(-50%) translateY(-50%);"
      "}"
      "#user {"
        "position: absolute;"
        "top: 5%;"
        "left: 80%;"
        "transform: translateX(-50%) translateY(-50%);"
      "}"
      "</style>"
      "</head>"
      "<body>"
      "<div id='title'><b>Zhiyuan Liu's Hangman</b></div>"
      "<form id = 'newgameform' method='POST'>"
      "<div class='container'>"
      "<input type='hidden' name='currentUser' value='" + curUser->username + "'>"
      "<input type='hidden' name='startnewgame'>"
      "<button id='newgame' type='submit'>New Game</button>"
      "</div>"
      "</form>"
      "<form id='logoutform' method='POST'>"
      "<div class='container'>"
      "<input type='hidden' name='currentUser' value='" + curUser->username + "'>"
      "<input type='hidden' name='logoutcuruser'>"
      "<button id='logout' type='submit'>Log Out</button>"
      "</div>"
      "<div id='user'>Logged in as: <b>" + curUser->username + "</b></div>"
      "</form>";

    //If no game is running, no graphics necessary
    if (curUser->game == 0) {
      game = "";
    }

    // Game is running, create graphics
    else {
      game = createGame(curUser, sock);
    }

    // Add info/statistics at bottom
    string close = "";
    stringstream ss;
    ss << curUser->wins;
    string temp = ss.str();
    ss.str("");
    close = close + "<div id='info'> Wins: " + temp + "<br>" + "Total Games: ";
    ss << curUser->total;
    temp = ss.str();

    close = close + temp + "</div></body></html>";

    string page = top + game + close;

    char * fullPage = new char[page.length()+1];
    strcpy (fullPage, page.c_str());

    header = "HTTP/1.1 " + code + " OK\r\nServer: Zhiyuan Liu's Hangman\r\nContent-Type: " + filetype + "\r\n\r\n";

    /* Sends header_response */
    char * header_response = new char [header.length()+1];
    strcpy (header_response, header.c_str());
    int ret;
    int sendOffset = 0;
    while(sendOffset != (int)strlen(header_response)){
        ret = send(sock, &header_response[sendOffset], strlen(header_response) - sendOffset, 0);
        //on success, returns number of characters sent
        if(ret < 0){
            perror("send");
            exit(1);
        }
        sendOffset += ret;
    }

    delete header_response;

    /* Sends fullPage */
    sendOffset = 0;
    while(sendOffset != (int)strlen(fullPage)){
        ret = send(sock, &fullPage[sendOffset], strlen(fullPage) - sendOffset, 0);
        //on success, returns number of characters sent
        if(ret < 0){
            perror("send");
            exit(1);
        }
        sendOffset += ret;
    }

    delete fullPage;
    return 0;
}

/* Creates the game page if a game is running */
string createGame(User *curUser, int sock) {
  string game;
  cout << "Creating gamepage" << endl;
  if (curUser->game == 2) { // Game has been lost
      return "<img id='picture' src='gallows" + NumberToString(10-curUser->guesses) + ".png'>"
             "<div id='end'>Out of guesses! You Lose! <br> The word was " + curUser->word + "</div>";
  }

  /* Generate the word to display to the user */
  string hangman_word;
  int won = 1;
  for(int i = 0; i < (int)curUser->word.length(); i++) {
      if (curUser->guessed[(int)curUser->word[i]-65] != 0) {
          hangman_word += curUser->word[i];
      }
      else {
        // If any underlines, some letters still not guessed, so haven't won
        hangman_word += "_";
        won = 0;
      }
  }
  game = "<div id='word'>" + hangman_word + "</div>";

  if (won == 1) {
      curUser->wins += 1;
      game += "<div id='end'>You Win!</div>";
      return game;
  }

  // If game has not been lost, we display the guess form to the user
  if (curUser->game == 1) {
      game += "<img id='picture' src='gallows" + NumberToString(10-curUser->guesses) + ".png'>"
              "<form id='guessform' method='POST'> <div>"
              "<label>Guess a letter: </label>"
              "<input type='hidden' name='currentUser' value='" + curUser->username + "'>"
              "<input id='guessedLetter' pattern='[A-Za-z]{1}' required='required' maxlength='1' type='text' name='guessedLetter' autofocus>"
              "</div>"
              "<div>"
              "<input type='submit' value='Send'>"
              "</div>"
              "</form>";
  }
  // If user just guessed a letter they guessed previously
  if (curUser->repeat == 1) {
      curUser->repeat = 0;
      game += "<div id='repeat'>You've already guessed this letter!</div>";
  }

  // Show user how many incorrect guesses they have remaining
  stringstream ss;
  ss << 10-curUser->guesses;
  string temp = ss.str();
  game += "<div id='guessedNum'>" + temp + " " + "incorrect guesses remaining. </div>";
  return game;
}
