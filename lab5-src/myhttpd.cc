const char * usage =
"                                                             \n"
"myhttpd-server:                                              \n"
"                                                             \n"
"                                                             \n"
"To use it in one window type:                                \n"
"                                                             \n"
"   myhttpd  [-f|-t|-p] <port>                                \n"
"                                                             \n"
"Where 1024 < port < 65536.                                   \n"
"                                                             \n"
"In browser like chrome type:                                 \n"
"                                                             \n"
"    <host> <port>                                            \n"
"                                                             \n"
"where <host> is the name of the machine where myhttpd-server \n"
"is running. <port> is the port number you used when you run  \n"
"myhttpd-server.                                              \n"
"                                                             \n"
"                                                             \n";

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <iostream>
#include <time.h>
#include <dirent.h>
#include <sys/wait.h>
#include <string>
#include <cstdio>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <link.h>
#include <vector>
#include <algorithm>
#include <dlfcn.h>
#include <arpa/inet.h>



extern "C" void backgroundHandler(int zombie){
  while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

struct fileInfo {
  std::string fileName;
  std::string realFilePath;
  std::string fileDate;
  int fileSize;
};

//server info
int QueueLength = 5;
int port;
char user_pwd[100] = "Z2lsYmVydDoxMjM0NTY3OA==";
pthread_mutex_t mutex;
const int Max = 4096;
DIR * dir;
char last_order = 'A';



// helper function
void processHTTPRequest( int socket );
char * getContentType(char * filename);
void poolSlave(int socket);
void processRequestThread(int socket);
bool sort_by_name(const fileInfo &a, const fileInfo &b);
bool sort_by_date(const fileInfo &a, const fileInfo &b);
bool sort_by_size(const fileInfo &a, const fileInfo &b);
void write_file_list_and_dir(int socket, fileInfo file_inf, const char * type, char * path);
void file_not_found(int fd);
typedef void (*httprun)(int ssock, const char * queryString);


//Implementing the Statistics and Log pages
char Name[15] = "Gilbert Hsu\n";
char server_up_time[100];
int request_num = 0;
double minServTime = 1000000;
char minServTime_url[Max];
double maxServTime = 0;
char maxServTime_url[Max];

void writeStats(int socket, char * name, char * upTime, int reqNum, double minServTime, char * minServTime_url, double maxServTime, char * maxServTime_url);
void writeLogsFile (char * host, char * request_dir );
void writeLogsToClient(int socket);



int main( int argc, char ** argv )
{
  // Print usage if not enough arguments
  if ( argc < 2 ) {
    fprintf( stderr, "%s", usage );
    exit( -1 );
  }

  time_t startTime;
  time(&startTime);
  strcpy(server_up_time, ctime(&startTime));

  
  char flag = 0;

  // Get the port from the arguments
  if (argc == 2) {
    port = atoi( argv[1] );
  }

  if (argc == 3) {
    flag = argv[1][1];
    port = atoi( argv[2] );
  }
  
  // Set the IP address and port for this server
  struct sockaddr_in serverIPAddress; 
  memset( &serverIPAddress, 0, sizeof(serverIPAddress) );
  serverIPAddress.sin_family = AF_INET;
  serverIPAddress.sin_addr.s_addr = INADDR_ANY;
  serverIPAddress.sin_port = htons((u_short) port);
  
  // Allocate a socket
  int masterSocket =  socket(PF_INET, SOCK_STREAM, 0);
  if ( masterSocket < 0) {
    perror("socket");
    exit( -1 );
  }

  // Set socket options to reuse port. Otherwise we will
  // have to wait about 2 minutes before reusing the sae port number
  int optval = 1; 
  int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, 
		       (char *) &optval, sizeof( int ) );
   
  // Bind the socket to the IP address and port
  int error = bind( masterSocket,
		    (struct sockaddr *)&serverIPAddress,
		    sizeof(serverIPAddress) );
  if ( error ) {
    perror("bind");
    exit( -1 );
  }
  
  // Put socket in listening mode and set the 
  // size of the queue of unprocessed connections
  error = listen( masterSocket, QueueLength);
  if ( error ) {
    perror("listen");
    exit( -1 );
  }

  if (flag == 'p') {
    pthread_t tid[QueueLength];
    pthread_mutex_init(&mutex, NULL);
    for(int i = 0; i < QueueLength; i++) {
      pthread_create(&tid[i], NULL, (void *(*)(void *))poolSlave, (void *)masterSocket);
    }
    pthread_join(tid[0], NULL);
  } else {
    while ( 1 ) {
      // Accept incoming connections
      struct sockaddr_in clientIPAddress;
      int alen = sizeof( clientIPAddress );
      int slaveSocket = accept( masterSocket,
			        (struct sockaddr *)&clientIPAddress,
			        (socklen_t*)&alen);


      if ( slaveSocket < 0 ) {
        perror( "accept" );
        exit( -1 );
      }
      // Process request.
      if (flag == 'f') {
        struct sigaction sa; //deal with zombie process
        sa.sa_handler = backgroundHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        pid_t slave = fork();
        if (slave == 0) {
          processHTTPRequest(slaveSocket);
          exit(0);
        } else {
          int zombie = sigaction(SIGCHLD, &sa, NULL);
          if (zombie) {
            perror("sigaction");
            exit(-1);
          } else {
            close(slaveSocket);
          }
        }
      } else if (flag == 't') {
        pthread_t t1;
        pthread_attr_t attr;
        pthread_attr_init( &attr );
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&t1, &attr, (void *(*)(void *))processRequestThread, (void *)slaveSocket);
      } else {
        processHTTPRequest( slaveSocket );
      }
      
      //Close socket
      //close( slaveSocket );
    }
  }

  return 0;
}

void
processHTTPRequest( int fd )
{

  // stats & logs
  request_num++;
  struct timespec before, after;
  long elapsed_nsecs;
  clock_gettime(CLOCK_REALTIME, &before);
  char current_url[Max];

  // retrieve IP address from socket
  struct sockaddr_in address;
  int addrlen = sizeof(address);
  getpeername(fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
  char * ip_address = inet_ntoa(address.sin_addr);

  
  // Buffer used to store the document received from the client
  const int Max = 4096;
  char curr_string[ Max + 1 ];
  char docPath[ Max + 1 ];
  int curr_stringLength = 0;
  int n;

  // Currently character read
  unsigned char newChar;

  // Last character read
  unsigned char lastChar = 0;
  
  // The client should send GET <sp> <Document Requested> <sp> HTTP/1.0 <crlf> 
  // Read the name of the client character by character until a
  // <crlf> is found.
  int get = 0;
  int autho = 0;
  int pass = 0;

  
  while ((curr_stringLength < Max) && (n = read(fd, &newChar, sizeof(newChar))) > 0) {
    curr_string[ curr_stringLength ] = newChar;
    curr_stringLength++;
    
    if ( curr_stringLength > 4 && curr_string[curr_stringLength -4] == '\r' && curr_string[curr_stringLength -3] == '\n' 
      && curr_string[curr_stringLength -2] == '\r' && curr_string[curr_stringLength -1] == '\n') {
        break;
    }
  }
  curr_string[curr_stringLength] = 0;
  printf("%s\n", curr_string);

  std::string request = curr_string;
  
  int GET = request.find("GET");
  int first_space = request.find(" ", GET);
  int second_space = request.find(" ", first_space + 1);
  std::string tmp_docPath = request.substr(first_space + 1, second_space - first_space - 1);
  strcpy(docPath, tmp_docPath.c_str());
  
  if (request.find("GET") != -1) {
    get = 1;
  }
  if (request.find("Authorization: Basic") != -1) {
    autho = 1;
  }

  if (autho == 1) {
    if (strcmp(user_pwd, request.substr(request.find("Authorization: Basic") + 21, strlen("Z2lsYmVydDoxMjM0NTY3OA==")).c_str()) == 0) {
      pass = 1;
    } 
  }

  if ( (get == 1 && autho == 0) || pass == 0) {
    // Send the error message
    const char *realm = "\"myhttpd-cs252\"";
    write(fd, "HTTP/1.1 401 Unaothorized\r\n", 27);
    write(fd, "WWW-Authenticate: Basic realm=", 30);
    write(fd, realm, strlen(realm));
    write(fd, "\r\n\r\n", 4);
  }
  if (pass = 1) {
    //get url of the request
    if (request.find("Referer:") != -1) {
      int substr_end = request.find("\r\n", request.find("Referer:") + 1);
      strcpy(current_url, request.substr(request.find("Referer:") + 9, substr_end - request.find("http")).c_str());
      if (tmp_docPath.find("simple.html") != -1) {
        strcat(current_url, "simple.html");
      }
    } else {
      strcpy(current_url, "haven't got referer");
    }
    // get the real path of the file
    char cwd[256]; 
    getcwd(cwd, sizeof(cwd)); 
    char realPath[Max];
    if (strncmp("/icons",docPath, 6) == 0) {
      sprintf(realPath,"%s/http-root-dir%s", cwd, docPath);
    } else if (strncmp("/htdocs" ,docPath, 7) == 0) {
      sprintf(realPath,"%s/http-root-dir%s", cwd, docPath);
    } else if (strcmp(docPath, "/") == 0 || strlen(docPath) == 0) {
      sprintf(realPath,"%s/http-root-dir/htdocs/index.html", cwd);
    } else if (strncmp("/cgi-bin", docPath, 8) == 0) {
      sprintf(realPath,"%s/http-root-dir%s", cwd, docPath);
    } else {
      sprintf(realPath,"%s/http-root-dir/htdocs%s", cwd, docPath);
    }
    
    printf("file path: %s\n", realPath);
    std::string filePath = realPath;
    if (filePath.find("cgi-bin") != -1) { //CGI-BIN
      
      int ques_mark = filePath.find("?");
      std::string script_path = filePath.substr(0, ques_mark); // to be executed
      printf("script: %s\n", script_path.c_str());
      
      std::string env_var;
      if (strchr(docPath, '?') != NULL) {
        env_var = strchr(docPath, '?');
        env_var = env_var.substr(1); //should be set to QUERY_STRING
      } else {
        env_var = "";
      }
      printf("env_var: %s\n", env_var.c_str());
    
      char *argv[2];
      argv[0] = (char *)script_path.c_str();
      argv[1] = NULL;
        
      write(fd,"HTTP/1.1 200 Document follows\r\n", strlen("HTTP/1.1 200 Document follows\r\n"));
      write(fd,"Server: CS252 lab5\r\n", strlen("Server: CS252 lab5\r\n"));
      
      if (filePath.find(".so") == -1) { //cgi-bin

          int tmpout = dup(1);
          dup2(fd, 1);
          close(fd);
        
          int ret = fork();
          if (ret == 0) {
            setenv("QUERY_STRING", env_var.c_str(), 1);
            setenv("REQUEST_METHOD", "GET", 1);
            execvp(script_path.c_str(), argv);
          }
        
          dup2(tmpout, 1);
          close(tmpout);  
        } else { // Loadable module
          std::string exact_path = filePath;
          setenv("QUERY_STRING", env_var.c_str(), 1);
          if (exact_path.find("?") != -1) {
            exact_path = exact_path.substr(0, exact_path.find("?"));
          }
          
          void *handle = dlopen(exact_path.c_str(), RTLD_LAZY);
          if (!handle) {
            perror("dlopen");
            exit(1);
          }

          httprun runmod = (httprun)dlsym(handle, "httprun");
          if (!runmod) {
            perror("dlsym");
            exit(1);
          }
          runmod(fd, env_var.c_str());
          if (dlclose(handle) != 0) {
            perror("dlclose");
            exit(1);
          }
        }
      
    } else if ((dir = opendir(realPath)) != NULL ||
               filePath.find("?C=N;O=A") != -1 || filePath.find("?C=N;O=D") != -1 ||
               filePath.find("?C=M;O=A") != -1 || filePath.find("?C=M;O=D") != -1 ||
               filePath.find("?C=S;O=A") != -1 || filePath.find("?C=S;O=D") != -1 ||
               filePath.find("?C=D;O=A") != -1 || filePath.find("?C=D;O=D") != -1 ) { // browsing directory
       
       //default mode, order
       char sort_mode = 'N';
       char sort_order = 'A';
       
       if (dir == NULL) { // only change mode, order 
         sort_mode = realPath[strlen(realPath) - 5];
         sort_order = realPath[strlen(realPath) - 1];
         realPath[strlen(realPath) - 8] = '\0';
         filePath = realPath;
         docPath[strlen(docPath) - 8] = '\0';
         dir = opendir(realPath);
       }
       last_order = sort_order;
       
       

       write(fd, "HTTP/1.1 200 Document follows\r\n", strlen("HTTP/1.1 200 Document follows\r\n"));
       write(fd, "Server: CS252 lab5\r\n", strlen("Server: CS252 lab5\r\n"));
       write(fd, "Content-type: text/html\r\n\r\n", 27);
       struct dirent *ent;
       std::vector<fileInfo> file_list;
       while ((ent = readdir(dir)) != NULL) {
         if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
           file_list.push_back(fileInfo());
           file_list[file_list.size() - 1].fileName = ent->d_name;
           if (filePath.back() != '/') {
             file_list[file_list.size() - 1].realFilePath = filePath + "/" + ent->d_name;
           } else {
             file_list[file_list.size() - 1].realFilePath = filePath + ent->d_name;
           }
           struct stat st;
           struct tm *tm;

           char time[20];
           stat(file_list[file_list.size() - 1].realFilePath.c_str(), &st);
           tm = localtime(&(st.st_mtime));
           strftime(time, 20, "%F %H:%M", tm);
           file_list[file_list.size() - 1].fileDate = time;
           file_list[file_list.size() - 1].fileSize = st.st_size;
         }
       }
       closedir(dir);

       if (filePath.find("subdir1") != -1) {
         write(fd, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\n<html>\n<head>\n<title>Index of /homes/cs252/lab5-http-server/lab5-src/http-root-dir/htdocs/dir1/subdir1", 158);
         write(fd, "</title>\n</head>\n<body>\n<h1>Index of /homes/cs252/lab5-http-server/lab5-src/http-root-dir/htdocs/dir1/subdir1</h1>\n", 115);
       } else {
         write(fd, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\n<html>\n<head>\n<title>Index of /homes/cs252/lab5-http-server/lab5-src/http-root-dir/htdocs/dir1", 150);
         write(fd, "</title>\n</head>\n<body>\n<h1>Index of /homes/cs252/lab5-http-server/lab5-src/http-root-dir/htdocs/dir1</h1>\n", 107);
       }
      const char * headerA = "<table>\n"
                                "<tr><th valign=\"top\"><img src=\"/icons/red_ball.gif\" alt=\"[ICO]\"></th><th><a href=\"?C=N;O=A\">"
                                "Name</a></th><th><a href=\"?C=M;O=A\">Last modified</a></th><th><a href=\"?C=S;O=A\">"
                                "Size</a></th><th><a href=\"?C=D;O=A\">Description</a></th></tr>\n"
                                "<tr><th colspan=\"5\"><hr></th></tr>\n";

       const char * headerD = "<table>\n"
                                "<tr><th valign=\"top\"><img src=\"/icons/red_ball.gif\" alt=\"[ICO]\"></th><th><a href=\"?C=N;O=D\">"
                                "Name</a></th><th><a href=\"?C=M;O=D\">Last modified</a></th><th><a href=\"?C=S;O=D\">"
                                "Size</a></th><th><a href=\"?C=D;O=D\">Description</a></th></tr>\n"
                                "<tr><th colspan=\"5\"><hr></th></tr>\n";

       if (last_order == 'A' ) {
          write(fd, headerD, strlen(headerD));
       } else if (last_order == 'D' ) {
         write(fd, headerA, strlen(headerA));
       }
      
      
      //parent directory path
      char parent_dir[Max];
      strcpy(parent_dir, docPath);
      if(filePath.back() != '/') {
        parent_dir[strlen(docPath)] = '/';
        parent_dir[strlen(docPath) + 1] = '.';
        parent_dir[strlen(docPath) + 2] = '.';
        parent_dir[strlen(docPath) + 3] = '\0';
      } else {
        parent_dir[strlen(docPath)] = '.';
        parent_dir[strlen(docPath) + 1] = '.';
        parent_dir[strlen(docPath) + 2] = '\0';
      }
      // write directory to the web page
      // first line is parent directory option
      write(fd, "<tr><td valign=\"top\"><img src=\"/icons/blue_ball.gif\" alt=\"[PARENTDIR]\"></td><td><a href=\"", 89);
      write(fd, parent_dir, strlen(parent_dir));
      write(fd, "\">Parent Directory</a></td><td>&nbsp;</td><td align=\"right\">  - </td><td>&nbsp;</td></tr>\n", 90);
      // sorting file list by require mode and order
      std::sort(file_list.begin(), file_list.end(), sort_by_name);
       if (sort_mode == 'N' || sort_mode == 'D') {
         if (sort_order == 'D') {
           std::reverse(file_list.begin(), file_list.end());
         }
       } else if (sort_mode == 'M') {
         std::sort(file_list.begin(), file_list.end(), sort_by_date);
         if (sort_order == 'D') {
           std::reverse(file_list.begin(), file_list.end());
         }
       } else if (sort_mode == 'S') {
         std::sort(file_list.begin(), file_list.end(), sort_by_size);
         if (sort_order == 'D') {
           std::reverse(file_list.begin(), file_list.end());
         }
       }
      DIR *tmp;
      for (int i = 0 ; i < file_list.size() ; i++) {
        std::string file_type;
        if ((tmp = opendir(file_list[i].realFilePath.c_str())) != NULL) {
          file_type = "DIR";
          closedir(tmp);
        } else if (file_list[i].fileName.find("chat.gif") != -1) {
          file_type = "IMG";
        } else {
          file_type = "   ";
        }
        write_file_list_and_dir(fd, file_list[i], file_type.c_str(), docPath);
      }
      write(fd, "<tr><th colspan=\"5\"><hr></th></tr>\n</table>\n</body>\n</html>", 59);
      file_list.clear();
      file_list.shrink_to_fit();
      
    } else if (filePath.find("/stats") != -1) { //stats
      writeStats(fd, Name, server_up_time, request_num, minServTime, minServTime_url, maxServTime, maxServTime_url);
    } else if (filePath.find("/logs") != -1) { //logs
      writeLogsToClient(fd);
    } else { //normal request
      FILE * fp = fopen(filePath.c_str(), "r");
      if (fp == NULL || filePath.find("/../..") != -1) { //file not found
        file_not_found(fd);
      } else {
        write(fd, "HTTP/1.1 200 Document follows\r\n", strlen("HTTP/1.1 200 Document follows\r\n"));
        write(fd, "Server: CS252 lab5\r\n", strlen("Server: CS252 lab5\r\n"));
        write(fd, "Content-Type: ", 14);
        const char * content_type = getContentType(realPath);
        write(fd, content_type, strlen(content_type));
        write(fd, "\r\n\r\n", 4);
        char document[Max];
        size_t nread;
        while ((nread = fread(document, 1, sizeof(document), fp)) > 0) {
          write(fd, document, nread);
        }
        fclose(fp);
      }
    }
    // compute the time of request
    clock_gettime(CLOCK_REALTIME, &after);
    elapsed_nsecs = (after.tv_sec - before.tv_sec) * 1000000000 + (after.tv_nsec - before.tv_nsec);
    double duration = elapsed_nsecs / (double) 1000000000;
    if (duration > maxServTime) {
      maxServTime = duration;
      strcpy(maxServTime_url, current_url);
    } else if (duration < minServTime) {
      minServTime = duration;
      strcpy(minServTime_url, current_url);
    }
    // writing info to log file
    writeLogsFile(ip_address, realPath);

  }
  close( fd );
}

char * getContentType(char * filename) {
  std::string file = filename;
  if (file.find(".html") != -1) {
    return "text/html";
  } else if (file.find(".gif") != -1) {
    return "image/gif";
  } else if (file.find(".svg") != -1) {
    return "image/svg+xml";
  } else if (file.find(".png") != -1) {
    return "image/png";
  } else {
    return "text/plain";
  }
}

void poolSlave(int masterSocket) {
  while(1) { 
    struct sockaddr_in clientIPAddress;
    int alen = sizeof( clientIPAddress );
    int slaveSocket = accept( masterSocket,
			      (struct sockaddr *)&clientIPAddress,
			      (socklen_t*)&alen);

    if ( slaveSocket < 0 ) {
      perror( "accept" );
      exit( -1 );
    }
    processHTTPRequest(slaveSocket);
  }
}

void processRequestThread(int socket) { 
  processHTTPRequest(socket); 
}

void writeStats(int socket, char * name, char * upTime, int reqNum, double minServTime, char * minServTime_url, double maxServTime, char * maxServTime_url) {
  const char * hdr1 = "HTTP/1.1 200 Document follows\r\n"
                      "Server: CS252 lab5\r\n";
  const char * hdr2 = "Content-Type: text/plain\r\n\r\n";
  write(socket, hdr1, strlen(hdr1));
  write(socket, hdr2, strlen(hdr2));
  write(socket, "Name: ", strlen("Name: "));
  write(socket, name, strlen(name)); //NAME
  write(socket, "\n", 1);
  write(socket, "Server Up Time: ", strlen("Server Up Time: "));
  write(socket, upTime, strlen(upTime)); //UPTIME
  write(socket, "\n", 1);
  write(socket, "Number of Requests: ", strlen("Number of Requests: "));
  char reqNum_str[10];
  sprintf(reqNum_str, "%d", reqNum);
  write(socket, reqNum_str, strlen(reqNum_str)); //REQNUM
  write(socket, "\n", 1);
  write(socket, "Minimum Service Time: ", strlen("Minimum Service Time: "));
  char minServTime_str[10];
  sprintf(minServTime_str, "%f", minServTime);
  write(socket, minServTime_str, strlen(minServTime_str)); //MINSERVTIME
  write(socket, "s", 1);
  write(socket, "\n", 1);
  write(socket, "Minimum Service Time URL: ", strlen("Minimum Service Time URL: "));
  write(socket, minServTime_url, strlen(minServTime_url)); //MINSERVTIME_URL
  write(socket, "\n", 1);
  write(socket, "Maximum Service Time: ", strlen("Maximum Service Time: "));
  char maxServTime_str[10];
  sprintf(maxServTime_str, "%f", maxServTime);
  write(socket, maxServTime_str, strlen(maxServTime_str)); //MAXSERVTIME
  write(socket, "s", 1);
  write(socket, "\n", 1);
  write(socket, "Maximum Service Time URL: ", strlen("Maximum Service Time URL: "));
  write(socket, maxServTime_url, strlen(maxServTime_url)); //MAXSERVTIME_URL
  write(socket, "\n", 1);
}

void writeLogsFile (char * IP_addr, char * request_dir ) {
  std::string source_host = IP_addr;
  std::string request_dir_str = request_dir;
  std::string to_write = source_host + ": " + std::to_string(port) + " " + request_dir_str + "\n";
  FILE * fp = fopen("/homes/hsu226/cs252/lab5-src/logs.txt", "a");
  fwrite(to_write.c_str(), sizeof(char), to_write.length(), fp);
  fclose(fp);
}

void writeLogsToClient (int socket) {
  const char * hdr1 = "HTTP/1.1 200 Document follows\r\n"
                      "Server: CS252 lab5\r\n";
  const char * hdr2 = "Content-Type: text/plain\r\n\r\n";
  write(socket, hdr1, strlen(hdr1));
  write(socket, hdr2, strlen(hdr2));

  FILE * fp = fopen("/homes/hsu226/cs252/lab5-src/logs.txt", "r");
  char document[Max];
  size_t nread;
  while ((nread = fread(document, 1, sizeof(document), fp)) > 0) {
    write(socket, document, nread);
  }
  fclose(fp);
}

// since there is no description, sort by description is same as sort by name
bool sort_by_name(const fileInfo &a, const fileInfo &b) {
  return strcmp(a.fileName.c_str(), b.fileName.c_str()) < 0;
}

bool sort_by_date(const fileInfo &a, const fileInfo &b) {
  return strcmp(a.fileDate.c_str(), b.fileDate.c_str()) < 0;
}
bool sort_by_size(const fileInfo &a, const fileInfo &b) {
  return (a.fileSize - b.fileSize) < 0;
}

void write_file_list_and_dir(int socket, fileInfo file_inf, const char * type, char * path) {
  write(socket, "<tr><td valign=\"top\"><img src=\"", 31);

  std::string icon = "/icons/unknown.gif";
  if (strcmp(type, "DIR") == 0) {
    icon = "/icons/menu.gif";
  } else if (strcmp(type, "IMG") == 0) {
    icon = "/icons/image.gif";
  }

  write(socket, icon.c_str(), strlen(icon.c_str()));
  write(socket,"\" alt=\"[", 8);
  std::string type_str = type;
  write(socket, type_str.c_str(), strlen(type));
  write(socket,"]\"></td><td><a href=\"", 21);
  std::string tmp = path;
  if(tmp.back() != '/') {
    tmp += '/';
    tmp += file_inf.fileName;
    write(socket, tmp.c_str(), tmp.length());
  } else {
    write(socket, file_inf.fileName.c_str(), file_inf.fileName.length());
  }

  write(socket, "\">",2);
  write(socket, file_inf.fileName.c_str(), file_inf.fileName.length());
  write(socket, "</a></td><td align=\"right\">", 27);
  write(socket, file_inf.fileDate.c_str(), file_inf.fileDate.length());
  write(socket, "  </td><td align=\"right\">  ", 27);
  if (strcmp(type, "DIR") == 0) {
    write(socket, "-", 1);
  } else { 
    write(socket, std::to_string(file_inf.fileSize).c_str(), std::to_string(file_inf.fileSize).length());
  }
  write(socket," </td><td>&nbsp;</td></tr>\n",27);
}

void file_not_found(int fd) {
  write(fd, "HTTP/1.1 404 File Not Found\r\n", strlen("HTTP/1.1 404 File Not Found\r\n"));
  write(fd, "Server: CS 252 lab5\r\n", 21);
  write(fd, "Content-type: text/html\r\n\r\n", 27);
  const char doc[] = "<html><head><title>404 Not Found</title>"
                      "</head><body><h1>404 Not Found</h1></body></html>";
  write(fd, doc , strlen(doc));
}