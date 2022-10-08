/*
-------------------------------------- PHASE 5 -------------------------------------------
Phase 3 & 4 followed by transfer of two-hop files.
Sends and accepts files via new connections between two-hop neighbors.
Computed MD5 hash for the files received.
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
#define CHUNK_SIZE 4 * 1024
#define FILE_CHUNK 64 * 1024

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

// convert string to lowercase
void toLowerCase (string &data) {
    transform(data.begin(), data.end(), data.begin(),
        [](unsigned char c){
            return std::tolower(c);
        }
    );
}

// find file size using file path
long getFileSize(const string &fileName) {
    FILE* f = fopen(fileName.c_str(),"rb");
    if (!f) {
        return -1;
    }
    fseek(f,0,SEEK_END);
    long len = ftell(f);
    fclose(f);
    return len;
}

// receiving file in parts from buffer
int receiverBuffer(int sockfd, char* buffer, int bufferSize) {
    int i = 0, l;
    while (i < bufferSize) {
        if ((l = recv(sockfd, &buffer[i], min(CHUNK_SIZE, bufferSize - i), 0)) < 0) {
            return l;
        }
        i += l;
    }
    return i;
}

// sending file in parts through buffer
int senderBuffer(int sockfd, char* buffer, int bufferSize) {
    int i = 0, l;
    while (i < bufferSize) {
        if ((l = send(sockfd, &buffer[i], min(CHUNK_SIZE, bufferSize - i), 0)) < 0) {
            return l;
        }
        i += l;
    }
    return i;
}

// wrapper function to send file
long FileSender(int sockfd, const string& fileName) {
    long fileSize;
    if ((fileSize = getFileSize(fileName)) < 0) {
        return -1;
    }
    ifstream file(fileName, ifstream::binary);
    if (file.fail()) {
        return -1;
    }
    if (senderBuffer(sockfd, reinterpret_cast<char*>(&fileSize),sizeof(fileSize)) != sizeof(fileSize)) {
        return -2;
    }
    char* buffer = new char[FILE_CHUNK];
    bool errored = false;
    int64_t i = fileSize;
    while (i != 0) {
        int64_t ssize = min(i, (int64_t)FILE_CHUNK);
        if (!file.read(buffer, ssize)) {
            errored = true;
            break;
        }
        int l;
        if ((l = senderBuffer(sockfd, buffer, (int)ssize)) < 0) {
            errored = true;
            break;
        }
        i -= l;
    }
    delete[] buffer;
    file.close();
    if (errored) {
        return -3;
    }
    return fileSize;
}

// wrapper function to receive file
long FileReceiver(int sockfd, const string& fileName) {
    ofstream file(fileName, ofstream::binary);
    if (file.fail()) {
        return -1;
    }
    long fileSize;
    if (receiverBuffer(sockfd, reinterpret_cast<char*>(&fileSize),sizeof(fileSize)) != sizeof(fileSize)) {
        return -2;
    }

    char* buffer = new char[FILE_CHUNK];
    bool errored = false;
    long i = fileSize;
    while (i != 0) {
        int r = receiverBuffer(sockfd, buffer, (int)min(i, (int64_t)FILE_CHUNK));
        if ((r < 0) || !file.write(buffer, r)) {
            errored = true;
            break;
        }
        i -= r;
    }
    delete[] buffer;
    file.close();
    if (errored) {
        return -3;
    }
    return fileSize;
}

// storing MD5 value
unsigned char result[MD5_DIGEST_LENGTH];

// get file size from descriptor
unsigned long getFileSizeByDesc(int fd) {
    struct stat statbuf;
    if (fstat(fd, &statbuf) < 0) {
        exit(-1);
    }
    return statbuf.st_size;
}

int main (int argc, char* argv[]) {

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
                        
                        string dirname(argv[2]);
                        FileSender(outConnections[i],"./" + dirname + askedFile);
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

                    string dirname(argv[2]);
                    FileReceiver(inConnections[j],"./" + dirname + "Downloaded/" + searchFiles[i]);

                    // finding MD5
                    int file_descript;
                    unsigned long file_size;
                    unsigned char* file_buffer;

                    char* filePath;
                    strtocharstar("./" + dirname + "Downloaded/" + searchFiles[i],filePath);

                    file_descript = open(filePath, O_RDONLY);
                    if(file_descript < 0) exit(-1);

                    file_size = getFileSizeByDesc(file_descript);

                    file_buffer = ((unsigned char*) mmap(0, file_size, PROT_READ, MAP_SHARED, file_descript, 0));
                    MD5(file_buffer, file_size, result);
                    munmap(file_buffer, file_size);

                    std::stringstream md5string;
                    md5string << std::hex << std::uppercase << std::setfill('0');
                    for (const auto &byte: result) {
                        md5string << std::setw(2) << (int)byte;
                    }
                    MD5values[i] = md5string.str();
                    toLowerCase(MD5values[i]);

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
                    ptr = strtok(NULL,"-");
                    string uid = ptr;
                    if (senders[i] > stoi(uid)) {

                        senders[i] = stoi(uid);
                        FileReceiver(inConnections[j],"./" + dirname + "Downloaded/" + searchFiles[i]);
                        
                        // finding MD5
                        int file_descript;
                        unsigned long file_size;
                        unsigned char* file_buffer;

                        char* filePath;
                        strtocharstar("./" + dirname + "Downloaded/" + searchFiles[i],filePath);

                        file_descript = open(filePath, O_RDONLY);
                        if(file_descript < 0) exit(-1);

                        file_size = getFileSizeByDesc(file_descript);

                        file_buffer = ((unsigned char*) mmap(0, file_size, PROT_READ, MAP_SHARED, file_descript, 0));
                        MD5(file_buffer, file_size, result);
                        munmap(file_buffer, file_size);

                        std::stringstream md5string;
                        md5string << std::hex << std::uppercase << std::setfill('0');
                        for (const auto &byte: result) {
                            md5string << std::setw(2) << (int)byte;
                        }
                        MD5values[i] = md5string.str();
                        toLowerCase(MD5values[i]);

                    }
                    else {

                        mkdir((dirname+"temp/").c_str(),0777);
                        FileReceiver(inConnections[j],"./" + dirname + "temp/" + searchFiles[i]);

                    }

                }

            }

        }

        // removing rejected duplicates

        system(("rm -rf " + dirname + "temp").c_str());

        int p = 0;
        for (int i = 0; i < m; i++) {
            if (!fileFound[i]) {
                p++;
            }
        }

        // sending counts and requests

        for (int i = 0; i < n; i++) {

            char* msg;
            strtocharstar(padder(to_string(p),l),msg);
            send(outConnections[i],msg,l,0);

            for (int j = 0; j < m; j++) {

                if (!fileFound[j]) {

                    strtocharstar(padder(publicPort + '-' + searchFiles[j],l),msg);
                    send(outConnections[i],msg,l,0);

                }

            }

            strtocharstar(padder(to_string(n - 1),l),msg);
            send(outConnections[i],msg,l,0);

        }

        // accepting counts and forwarding

        int *filesLeft = new int [n];

        for (int i = 0; i < n; i++) {

            char buf[100];
            recv(inConnections[i],buf,l,0);
            string data(buf,l);
            data = cutString(data);
            filesLeft[i] = stoi(data);

            for (int j = 0; j < n; j++) {

                char* msg;
                strtocharstar(padder(to_string(filesLeft[i]),l),msg);
                if (j != i) {
                    send(outConnections[j],msg,l,0);
                }

            }

        }

        // accepting two-hop requests and forwarding

        for (int i = 0; i < n; i++) {

            for (int j = 0; j < filesLeft[i]; j++) {

                char buf[100];
                recv(inConnections[i],buf,l,0);
                string data(buf,l);

                for (int k = 0; k < n; k++) {

                    char* msg;
                    strtocharstar(data,msg);
                    if (k != i) {
                        send(outConnections[k],msg,l,0);
                    }

                }
                
            }

        }

        // accepting two-hop counts and replying to two-hop requests

        int total_2hop = 0;

        for (int i = 0; i < n; i++) {

            char buf[100];
            recv(inConnections[i],buf,l,0);
            string data(buf,l);
            data = cutString(data);
            int twoHops = stoi(data);
            total_2hop += twoHops;

            int *twoHopRequests = new int [twoHops];
                
            for (int j = 0; j < twoHops; j++) {

                recv(inConnections[i],buf,l,0);
                string data(buf,l);
                data = cutString(data);
                twoHopRequests[j] = stoi(data);

            }

            for (int j = 0; j < twoHops; j++) {

                for (int k = 0; k < twoHopRequests[j]; k++) {

                    recv(inConnections[i],buf,l,0);
                    string data(buf,l);
                    data = cutString(data);

                    char* temp;
                    strtocharstar(data,temp);
                    char* ptr = strtok(temp,"-");
                    string askingPort = ptr;

                    struct addrinfo hints, *res;
                    memset(&hints,0,sizeof hints);
                    hints.ai_family = AF_UNSPEC;
                    hints.ai_socktype = SOCK_STREAM;
                    // hints.ai_flags = AI_PASSIVE;
        
                    strtocharstar(askingPort,port);

                    if ((status = getaddrinfo("127.0.0.1",port,&hints,&res)) != 0) {
                        fprintf(stderr, "get addrinfo error: %s\n", gai_strerror(status));
                        exit(1);
                    }

                    out_fd = socket(res -> ai_family, res -> ai_socktype, res -> ai_protocol);
                    while (connect(out_fd,res -> ai_addr,res -> ai_addrlen) < 0) {};
                    
                    ptr = strtok(NULL,"-");
                    string askedFile = ptr;
                    
                    int u;
                    for (u = 0; u < fileCount; u++) {
                        if (ownFiles[u] == askedFile) {
                            char* msg;
                            strtocharstar(padder(askedFile + '-' + uniqueID,l),msg);
                            send(out_fd,msg,l,0);
                            string dirname(argv[2]);
                            FileSender(out_fd,"./" + dirname + askedFile);
                            break;
                        }
                    }
                    if (u == fileCount) {
                        char* msg;
                        strtocharstar(padder("not-found",l),msg);
                        send(out_fd,msg,l,0);
                    }

                }

            }

        }

        // receiving responses

        for (int i = 0; i < p * (total_2hop); i++) {

            addr_size = sizeof their_addr;
            in_fd = accept(sockfd,(struct sockaddr*)&their_addr,&addr_size);

            int connec;
            char buf[100];

            if (in_fd != -1) {

                connec = in_fd;
                char buf[100];
                recv(connec,buf,l,0);
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
                for (int k = 0; k < m; k++) {

                    if (searchFiles[k] == file) {
                        if (!fileFound[k]) {

                            senders[k] = stoi(uid);
                            fileFound[k] = true;
                            fileDepth[k] = 2;
                            string dirname(argv[2]);
                            FileReceiver(connec,"./" + dirname + "Downloaded/" + searchFiles[k]);

                            // finding MD5
                            int file_descript;
                            unsigned long file_size;
                            unsigned char* file_buffer;

                            char* filePath;
                            strtocharstar("./" + dirname + "Downloaded/" + searchFiles[k],filePath);

                            file_descript = open(filePath, O_RDONLY);
                            if(file_descript < 0) exit(-1);

                            file_size = getFileSizeByDesc(file_descript);

                            file_buffer = ((unsigned char*) mmap(0, file_size, PROT_READ, MAP_SHARED, file_descript, 0));
                            MD5(file_buffer, file_size, result);
                            munmap(file_buffer, file_size);

                            std::stringstream md5string;
                            md5string << std::hex << std::uppercase << std::setfill('0');
                            for (const auto &byte: result) {
                                md5string << std::setw(2) << (int)byte;
                            }
                            MD5values[k] = md5string.str();
                            toLowerCase(MD5values[k]);

                        }
                        else if (fileFound[k] && fileDepth[k] == 2) {
                            if (senders[k] > stoi(uid)){

                                senders[k] = stoi(uid);
                                string dirname(argv[2]);
                                FileReceiver(connec,"./" + dirname + "Downloaded/" + searchFiles[k]);

                                // finding MD5
                                int file_descript;
                                unsigned long file_size;
                                unsigned char* file_buffer;

                                char* filePath;
                                strtocharstar("./" + dirname + "Downloaded/" + searchFiles[k],filePath);

                                file_descript = open(filePath, O_RDONLY);
                                if(file_descript < 0) exit(-1);

                                file_size = getFileSizeByDesc(file_descript);

                                file_buffer = ((unsigned char*) mmap(0, file_size, PROT_READ, MAP_SHARED, file_descript, 0));
                                MD5(file_buffer, file_size, result);
                                munmap(file_buffer, file_size);

                                std::stringstream md5string;
                                md5string << std::hex << std::uppercase << std::setfill('0');
                                for (const auto &byte: result) {
                                    md5string << std::setw(2) << (int)byte;
                                }
                                MD5values[k] = md5string.str();
                                toLowerCase(MD5values[k]);

                            }
                            else {

                                string dirname(argv[2]);
                                mkdir((dirname+"temp/").c_str(),0777);
                                FileReceiver(connec,"./" + dirname + "temp/" + searchFiles[k]);

                            }
                        }
                    }

                } 

            }

        }

        // removing rejected duplicates

        system(("rm -rf " + dirname + "temp").c_str());

        // print results for phase 5

        for (int i = 0; i < m; i++) {

            cout << "Found " << searchFiles[i] << " at " << senders[i] << " with MD5 " << MD5values[i] << " at depth " << fileDepth[i] << endl;

        }

        shutdown(sockfd,2);

        freeaddrinfo(servinfo);

    }

}