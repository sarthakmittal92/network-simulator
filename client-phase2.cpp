/*
-------------------------------------- PHASE 2 -------------------------------------------
Phase 1 followed by searching of files.
Sends request for files to all adjacent neighbors.
Accepts replies from the neighbors.
Ties broken based on smaller unique ID.
------------------------------------------------------------------------------------------
*/

#include <iostream>
#include <string>
#include <dirent.h>
#include <regex>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cmath>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/ip.h> 
#include <fstream>
#include <sys/mman.h>
#include <fcntl.h>
#include <openssl/md5.h>
#include <sstream>
#include <iomanip>
using namespace std;

#define BACKLOG 10

// function to convert string to char array
void strtocharstar (string s, char* &c) {
    int n = s.length();
    c = new char[n + 1];
    strcpy(c,s.c_str());
}

// function to pad string with '#'
string padder (string s, int l) {
    int n = s.length();
    int padL = l - n;
    string pad(padL,'#');
    return s + pad;
}

// function to cut off trailing '#'
string cutString (string s) {
    return s.substr(0,s.find('#'));
}

int main (int argc, char* argv[]) {

    // open configuration file of client
    fstream configFile;
    configFile.open(argc > 1? argv[1] : "",ios::in);

    if (configFile.is_open()) {
        
        string s;

        // client data
        configFile >> s;
        string clientID = s;
        configFile >> s;
        string publicPort = s;
        configFile >> s;
        string uniqueID = s;

        // setting up client socket descriptor
        int status;

        struct sockaddr_storage their_addr;
        socklen_t addr_size;
        struct addrinfo hints;
        struct addrinfo* servinfo;
        int sockfd, out_fd = -1, in_fd = -1;

        memset(&hints,0,sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        // hints.ai_flags = AI_PASSIVE;

        char* port;
        strtocharstar(publicPort,port);

        if ((status = getaddrinfo("127.0.0.1",port,&hints,&servinfo)) != 0) {
            fprintf(stderr, "get addrinfo error: %s\n", gai_strerror(status));
            exit(1);
        }

        sockfd = socket(servinfo -> ai_family, servinfo -> ai_socktype, servinfo -> ai_protocol);
 
        // associating client to port
        bind(sockfd,servinfo -> ai_addr,servinfo -> ai_addrlen);
        listen(sockfd,BACKLOG);

        // neighbors
        configFile >> s;
        const int n = stoi(s);
        string *neighborIDs = new string [n], *neighborPorts = new string [n], *uniqueIDs = new string [n];

        // neighbor data
        for (int i = 0; i < n; i++) {
            string t;
            configFile >> t;
            neighborIDs[i] = t;
            configFile >> t;
            neighborPorts[i] = t;
        }

        // files to search
        configFile >> s;
        string searchFileCount = s;
        const int m = stoi(s);
        vector<string> searchFiles;

        // search files data
        for (int i = 0; i < m; i++) {
            string u;
            configFile >> u;
            searchFiles.push_back(u);
        }

        configFile.close();

        // directory variables
        struct dirent* entry = nullptr;
        DIR* dp = nullptr;

        // own file data
        vector<string> ownFiles;
        dp = opendir(argc > 2 ? argv[2] : "/");
        int fileCount = 0;
        if (dp != nullptr) {
            while (entry = readdir(dp)) {
                if (entry -> d_type != 4) {
                    ownFiles.push_back(entry -> d_name);
                    fileCount++;
                }
            }
        }

        // close directory
        closedir(dp);

        // making directory "Downloaded"
        string dirname(argv[2]);
        mkdir((dirname+"Downloaded/").c_str(),0777);

        // sort file names
        sort(ownFiles.begin(),ownFiles.end());
        sort(searchFiles.begin(),searchFiles.end());

        // print files
        for (int i = 0; i < fileCount; i++) {
            cout << ownFiles[i] << endl;
        }

        // connecting to neighbors

        int i = 0;
        int* outConnections = new int [n];

        // sending connections
        
        while (i < n) {

            struct addrinfo hints, *res;
            memset(&hints,0,sizeof hints);
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            // hints.ai_flags = AI_PASSIVE;
  
            strtocharstar(neighborPorts[i],port);

            if ((status = getaddrinfo("127.0.0.1",port,&hints,&res)) != 0) {
                fprintf(stderr, "get addrinfo error: %s\n", gai_strerror(status));
                exit(1);
            }

            out_fd = socket(res -> ai_family, res -> ai_socktype, res -> ai_protocol);
            while (connect(out_fd,res -> ai_addr,res -> ai_addrlen) < 0) {};

            outConnections[i] = out_fd;
            i++;

        }

        // accepting connections

        int* unsInConnections = new int [n], *inConnections = new int [n];

        i = 0;
        while (i < n) {

            addr_size = sizeof their_addr;
            in_fd = accept(sockfd,(struct sockaddr*)&their_addr,&addr_size);
            if (in_fd != -1) {
                unsInConnections[i] = in_fd;
                i++;
            }

        }

        // sending messages
        
        int l = 80;

        for (int i = 0; i < n; i++) {

            char* msg;
            strtocharstar(padder(clientID + '-' + uniqueID,l),msg);
            send(outConnections[i],msg,l,0);

        }

        // receiving messages

        for (int i = 0; i < n; i++) {

            char buf[100];
            recv(unsInConnections[i],buf,l,0);
            string data(buf,l);
            data = cutString(data);
            char* temp;
            strtocharstar(data,temp);
            char* ptr = strtok(temp,"-");
            string cID = ptr;
            ptr = strtok(NULL,"-");
            for (int k = 0; k < n; k++) {
                if (neighborIDs[k] == cID) {
                    uniqueIDs[k] = ptr;
                    inConnections[k] = unsInConnections[i];
                }
            }

        }

        // print connections

        for (int i = 0; i < n; i++) {

            cout << "Connected to " << neighborIDs[i] << " with unique-ID " << uniqueIDs[i] << " on port " << neighborPorts[i] << endl;

        }

        // communicating for files

        bool* fileFound = new bool [m];
        for (int i = 0; i < m; i++) {
            fileFound[i] = false;
        }

        // sending counts and requests

        for (int i = 0; i < n; i++) {

            char* msg;
            strtocharstar(padder(to_string(m),l),msg);
            send(outConnections[i],msg,l,0);

            for (int j = 0; j < m; j++) {

                char* msg;
                strtocharstar(padder(searchFiles[j],l),msg);
                send(outConnections[i],msg,l,0);

            }

        }

        int* fileCounts = new int [n];

        // accepting counts and requests and sending response

        for (int i = 0; i < n; i++) {

            char buf[100];
            recv(inConnections[i],buf,l,0);
            string data(buf,l);
            data = cutString(data);
            fileCounts[i] = stoi(data);

        }

        for (int i = 0; i < n; i++) {

            for (int j = 0; j < fileCounts[i]; j++) {

                char buf[100];
                recv(inConnections[i],buf,l,0);
                string data(buf,l);
                string askedFile = cutString(data);
                int u;
                for (u = 0; u < fileCount; u++) {
                    if (ownFiles[u] == askedFile) {
                        char* msg;
                        strtocharstar(padder(askedFile + '-' + uniqueID,l),msg);
                        send(outConnections[i],msg,l,0);
                        break;

                    }
                }
                if (u == fileCount) {
                    char* msg;
                    strtocharstar(padder("not-found",l),msg);
                    send(outConnections[i],msg,l,0);
                }

            }

        }

        // receiving response and printing

        int *senders = new int [m], *fileDepth = new int [m];
        string* MD5values = new string [m];
        for (int i = 0; i < m; i++) {
            senders[i] = 0;
            fileDepth[i] = 0;
            MD5values[i] = "0";
        }

        for (int i = 0; i < m; i++) {

            for (int j = 0; j < n; j++) {

                if (!fileFound[i]) {

                    char buf[100];
                    recv(inConnections[j],buf,l,0);
                    string data(buf,l);
                    data = cutString(data);
                    char* temp;
                    strtocharstar(data,temp);
                    char* ptr = strtok(temp,"-");
                    string file = ptr;
                    if (file == "not") {
                        continue;
                    }
                    ptr = strtok(NULL,"-");
                    string uid = ptr;
                    senders[i] = stoi(uid);
                    fileFound[i] = true;
                    fileDepth[i] = 1;

                }
                else {

                    char buf[100];
                    recv(inConnections[j],buf,l,0);
                    string data(buf,l);
                    data = cutString(data);
                    char* temp;
                    strtocharstar(data,temp);
                    char* ptr = strtok(temp,"-");
                    string file = ptr;
                    if (file == "not") {
                        continue;
                    }
                    
                }

            }

        }

        // print results for phase 2

        for (int i = 0; i < m; i++) {

            cout << "Found " << searchFiles[i] << " at " << senders[i] << " with MD5 0 at depth " << fileDepth[i] << endl;

        }

        shutdown(sockfd,2);

        freeaddrinfo(servinfo);

    }

}