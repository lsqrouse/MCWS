
const char * usage =
"                                                               \n"
"daytime-server:                                                \n"
"                                                               \n"
"Simple server program that shows how to use socket calls       \n"
"in the server side.                                            \n"
"                                                               \n"
"To use it in one window type:                                  \n"
"                                                               \n"
"   daytime-server <port>                                       \n"
"                                                               \n"
"Where 1024 < port < 65536.             \n"
"                                                               \n"
"In another window type:                                       \n"
"                                                               \n"
"   telnet <host> <port>                                        \n"
"                                                               \n"
"where <host> is the name of the machine where daytime-server  \n"
"is running. <port> is the port number you used when you run   \n"
"daytime-server.                                               \n"
"                                                               \n"
"Then type your name and return. You will get a greeting and   \n"
"the time of the day.                                          \n"
"                                                               \n";


#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <stdio.h>
#include <time.h>

#include <algorithm>
#include <thread>
#include <fcntl.h>
#include <sys/stat.h>
#include <vector>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <dirent.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unordered_map>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

//global vars
int QueueLength = 15;
pthread_mutex_t mutex;

// Processes time request
void processHTTPRequest( int socket );
void processThreadRequest( void * args );
void processRequest(int socket  );

//makes sure the password is right
bool validate( std::string * req );
std::string * finishRequest(int socket);
std::string * getContentType(std::string * doctype);
std::string * checkDir(std::string * docpath, std::string args);


//handles pools of threads
void poolSlave(void * serverSocket_void);

//struct for handling file sorting
struct dirFile {
   std::string * filename{};
   std::string * lastMod{};
   std::string * size{};
   std::string * html{};
};

//helper functions for directory stuff
bool nameSortA(dirFile a, dirFile b);
bool dateSortA(dirFile a, dirFile b);
bool sizeSortA(dirFile a, dirFile b);
bool nameSortD(dirFile a, dirFile b);
bool dateSortD(dirFile a, dirFile b);
bool sizeSortD(dirFile a, dirFile b);

//helper functions for processing strings
std::string * getModString(char * c);
std::string * buildHTMLHead(std::string docpath, std::string args);

/*
 * Functions and variables to handle statistics
 */
void handleStats(int socket);
void handleLogs(int socket);
void updateTimes(time_t req_start);
time_t server_start;
int req_count;
double min_req_time;
double max_req_time;
std::string * ips[32];

/*
 * Functions and data types for loadable modules
 */
//custom function type
typedef void (*httprunfunc)(int ssock, const char* querystring);
std::unordered_map<std::string, httprunfunc> open_mods;




void sigchldHandler(int sig) {
   if (sig == 1) {
    
   }
   int ret = waitpid(-1, NULL, WNOHANG);
   printf("[%d] exited.\n", ret);
}

int main( int argc, char ** argv ) {
   // Print usage if not enough arguments
   if ( argc < 2 ) {
      fprintf( stderr, "%s", usage );
      exit( -1 );
   }

   int port;
   if (argc == 3) {
      // Get the port from the arguments
      port = atoi( argv[2] );
   } else if (argc == 2) {
      // Get the port from the arguments
      port = atoi( argv[1] );
   } else {
      port = 5000;
   }

   printf("port is %d\n", port);
   time(&server_start);
   req_count = 0;
   min_req_time = std::numeric_limits<double>::max();
   max_req_time = std::numeric_limits<double>::min();


   //sets up sigchild handler for zombie process elimination
   struct sigaction sa;
   sa.sa_handler = sigchldHandler;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = SA_RESTART;
   //signal(SIGINT, sigintHandler);
   signal(SIGCHLD, sigchldHandler);


   // Set the IP address and port for this server
   struct sockaddr_in serverIPAddress; 
   memset( &serverIPAddress, 0, sizeof(serverIPAddress) );
   serverIPAddress.sin_family = AF_INET;
   serverIPAddress.sin_addr.s_addr = INADDR_ANY;
   serverIPAddress.sin_port = htons((u_short) port);

   // Allocate a socket
   int serverSocket =  socket(PF_INET, SOCK_STREAM, 0);
   if ( serverSocket < 0) {
      perror("socket");
      exit( -1 );
   }

   // Set socket options to reuse port. Otherwise we will
   // have to wait about 2 minutes before reusing the sae port number
   int optval = 1; 
   int err = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, 
            (char *) &optval, sizeof( int ) );

   // Bind the socket to the IP address and port
   int error = bind( serverSocket,
         (struct sockaddr *)&serverIPAddress,
         sizeof(serverIPAddress) );
   if ( error ) {
   perror("bind");
   exit( -1 );
   }

   // Put socket in listening mode and set the 
   // size of the queue of unprocessed connections
   error = listen( serverSocket, QueueLength);
   if ( error ) {
   perror("listen");
   exit( -1 );
  }

   while ( 1 ) {
      if (argc == 3 && (strcmp(argv[1], "-p") == 0)) {
         //inits the mutex
         pthread_mutex_init(&mutex,NULL);

         //pool party
         pthread_t tid[5];
         pthread_attr_t attr;

         pthread_attr_init( &attr );
         pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

         for (int i = 0; i < 5; i++) {
               pthread_create( &tid[i], &attr, (void * (*)(void *)) poolSlave, (void *) &serverSocket);
         }
         pthread_join(tid[0], NULL);
         printf("Oh crap we're after tid\n");

      }

      // Accept incoming connections
      struct sockaddr_in clientIPAddress;
      int alen = sizeof( clientIPAddress );

      //actually wait for the connections


      if (argc == 3) {
         //need to determine which method to use
         if (strcmp(argv[1], "-f") == 0) {
            int clientSocket = accept( serverSocket,
               (struct sockaddr *)&clientIPAddress,
               (socklen_t*)&alen);

            if ( clientSocket < 0 ) {
               perror( "accept" );
               exit( -1 );
            }
      in_addr ip_address = ((sockaddr_in)clientIPAddress).sin_addr;
            ips[clientSocket] = new std::string(inet_ntoa(ip_address));            //its process time!
            int pid = fork();
            if (pid == 0) {
               //in the kid
               // Process request.
               processRequest( clientSocket );
                  
               // Close socket
               close( clientSocket );

               printf("exiting process with socket %d\n", clientSocket);

               //end this process
               exit( 0 );
            } 
            close(clientSocket);
         } else if (strcmp(argv[1], "-t") == 0) {
            int clientSocket = accept( serverSocket,
               (struct sockaddr *)&clientIPAddress,
               (socklen_t*)&alen);

            if ( clientSocket < 0 ) {
               perror( "accept" );
               exit( -1 );
            }
            //its thread time baby
            pthread_t tid;
            pthread_attr_t attr;

            pthread_attr_init( &attr );
            pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            
            int * socket = (int *) malloc(sizeof(int));
            *socket = clientSocket;

            // void * args[2];
            // args[0] = (void *) socket;
            // args[1] = (void *) &clientIPAddress;


      in_addr ip_address = ((sockaddr_in)clientIPAddress).sin_addr;
            ips[clientSocket] = new std::string(inet_ntoa(ip_address));

            pthread_create( &tid, &attr, (void * (*)(void *)) processThreadRequest, (void *) socket);

            pthread_attr_destroy(&attr);
         } else {
            printf("ERROR: argc is 3 but argv[1] wasn't -f or -t, it was %s\n", argv[1]);
            int clientSocket = accept( serverSocket,
               (struct sockaddr *)&clientIPAddress,
               (socklen_t*)&alen);

            if ( clientSocket < 0 ) {
               perror( "accept" );
               exit( -1 );
            }
            // Process request.
      in_addr ip_address = ((sockaddr_in)clientIPAddress).sin_addr;
            ips[clientSocket] = new std::string(inet_ntoa(ip_address));
            processRequest( clientSocket );
            
            // Close socket
            close( clientSocket );
         }
      } else {
            int clientSocket = accept( serverSocket,
               (struct sockaddr *)&clientIPAddress,
               (socklen_t*)&alen);

            if ( clientSocket < 0 ) {
               perror( "accept" );
               exit( -1 );
            }
            // Process request.
      in_addr ip_address = ((sockaddr_in)clientIPAddress).sin_addr;
            ips[clientSocket] = new std::string(inet_ntoa(ip_address));
            processRequest( clientSocket );
               
            // Close socket
            close( clientSocket );
      }

   }
  
}

void processThreadRequest( void * args ) {
   // void ** args = ((void **) argv);

   // int socket_val = *((int *) (args[0]));
   // struct sockaddr_in clientIPAddress = *((struct sockaddr_in *) args[1]);

   // printf("socket is now %p and client is now %p, argv is %p\n", ((int *) (args[0])), ((struct sockaddr_in *) args[1]), argv);
   // printf("socket's value is now %d\n", socket_val);

   int socket_val = *((int *) (args));
   processRequest(socket_val);
   close(socket_val);
   // free(((int *) (args[0])));
   free(((int *) (args)));

}

bool validate(std::string * req) {
   int index = req->find("Y3MyNTI6aWxvdmVzaGVsbA==");
   if (index != std::string::npos) {
      return true;
   } else {
      return false;
   }
}

void poolSlave(void * serverSocket_void) {
   int serverSocket = *((int *) serverSocket_void);
   while (1) {
      // Accept incoming connections
      struct sockaddr_in clientIPAddress;
      int alen = sizeof( clientIPAddress );

      pthread_mutex_lock(&mutex);
      //actually wait for the connections
      int clientSocket = accept( serverSocket,
               (struct sockaddr *)&clientIPAddress,
               (socklen_t*)&alen);
      pthread_mutex_unlock(&mutex);


      if ( clientSocket < 0 ) {
         perror( "accept" );
         exit( -1 );
      }
      in_addr ip_address = ((sockaddr_in)clientIPAddress).sin_addr;
      ips[clientSocket] = new std::string(inet_ntoa(ip_address));

      processRequest(clientSocket);
      close(clientSocket); 
   }


}


void processRequest(int socket  ) {
   int MaxRequest = 512;
   char * doc = (char *) calloc(sizeof(char), MaxRequest);
   int docLength = 0;
   int n;

   //adds the current request
   req_count++;

   time_t req_start;
   time(&req_start);



  // Currently character read
  unsigned char newChar;

  // Last character read
  //unsigned char lastChar[] = {0, 0, 0};
  unsigned char oldChar;

   //counting and weirdness
   bool gotGet = false;
   bool gotDoc = false;
   //printf("Reading request now\n");
   while ( ( n = read( socket, &newChar, sizeof(newChar) ) ) > 0 ) {
      //printf("read into doc, %d\n", newChar  );     
      
      if (newChar == ' ') {
            if (gotGet && !gotDoc) {
               gotDoc = true;
            } else if (!gotGet) {
               gotGet = true;
            }
      } else if (newChar == '\n' && oldChar == '\r') {
         break;
      } else {
            oldChar = newChar;

            if (gotGet  && !gotDoc) {
               doc[docLength] = newChar;
               docLength++;
            }
      }
   }
   doc[docLength] = 0;
   std::string * req = finishRequest(socket);

   if (!validate(req)) {
      delete req;
      //send the header back and return
      printf("ERROR: Wrong Password\n");
      const char * header = "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"myhttp-cs252\"\r\n\r\n";
      write(socket, header, strlen(header));
      updateTimes(req_start);
      return;
   }
   delete req; 

   std::string * cpp_doc = new std::string(doc);

   char * c_cwd = (char *) calloc(sizeof(char), 256);
   c_cwd = getcwd(c_cwd, 256);
   std::string * cwd = new std::string(c_cwd);
   
   free(c_cwd);
   c_cwd = NULL;

   std::string * docpath;

   if (cpp_doc->find("u/riker/u93") != -1) {
      //already a full filepath
      docpath = new std::string(doc);
   } else if (strcmp(doc, "/") == 0) {
      docpath = new std::string(*cwd + "/http-root-dir/htdocs/index.html");
   } else if (cpp_doc->find("/icons") == 0) {
      //starts with icons
      docpath = new std::string(*cwd + "/http-root-dir" + doc);
   } else if (cpp_doc->find("/htdocs") == 0) {
      docpath = new std::string(*cwd + "/http-root-dir" + doc);
   } else if (cpp_doc->find("/cgi-bin") == 0) {
      docpath = new std::string(*cwd + "/http-root-dir" + doc);
   }  else {
      docpath = new std::string(*cwd + "/http-root-dir/htdocs" + doc);
   }

   printf("doc is %s\n", doc);
   printf("docpath is %s\n", docpath->c_str());



   //check if we have args
   std::string args;
   if (docpath->find_last_of("?") != -1) {
      args = docpath->substr(docpath->find_last_of("?") + 1, docpath->size() - docpath->find_last_of("?"));
      *docpath = docpath->substr(0, docpath->find_last_of("?"));
      printf("docpath updated to %s\n", docpath->c_str());
   } else {
      args = "";
   }

   //content type
   std::string * contentType = getContentType(docpath);
   
   printf("args is %s\n" , args.c_str());
   if (*contentType == "dir") {
      if (docpath->c_str()[docpath->size() - 1] != '/') {
         printf("changed from %s", docpath->c_str());
         *docpath = *docpath + '/';
         printf("to %s\n", docpath->c_str());

      }
      *docpath = docpath->substr(0, docpath->find_last_of("/") + 1);
   }

   if (*contentType == "stats") {
      //creates a header
      std::string * header = new std::string("HTTP/1.1 200 Document follows\r\nServer: QuinnHTTP\r\nContent-type: text/html\r\n\r\n");

      //writes header
      write(socket, header->c_str(), strlen(header->c_str()));

      //sends the stats stuff
      handleStats(socket);
      updateTimes(req_start);
      return;
      
   }

   if (*contentType == "logs") {
      //creates a header
      std::string * header = new std::string("HTTP/1.1 200 Document follows\r\nServer: QuinnHTTP\r\nContent-type: text/html\r\n\r\n");

      //writes header
      write(socket, header->c_str(), strlen(header->c_str()));

      int log = open("./log.txt", O_CREAT | O_WRONLY | O_APPEND, 0664);
      std::string * ip = ips[socket];
      std::string * log_entry = new std::string("User at IP: " + *ip + " requested " + *docpath + "<br>");
      int wn = write(log, log_entry->c_str(), log_entry->size());
      write(log, "\n", 1);

      delete ip;
      delete log_entry;
      close(log);

      //handles logs
      handleLogs(socket);
      updateTimes(req_start);
      return;
   }

   //expandfilepath
   char * expanded = realpath(docpath->c_str(), NULL);
   if (expanded == NULL) {
      printf("sending back 404 header\n");
      std::string header = "HTTP/1.1 404 File Not Found\r\nServer: QuinnHTTP\r\nContent-type: " + *contentType + "\r\n\r\nCould not find the specified URL. The server returned an error\n";
      write(socket, header.c_str(), header.size());
      //delete &header;
      updateTimes(req_start);   
      return;
   }

   if (strlen(expanded) < (cwd->size() + strlen("/http-root-dir"))) {
      //bad news bears, we gotta return
      printf("ERROR: ATTEMPTED ACCESS AT A HIGHER DIR\n");
      std::string header = "HTTP/1.1 403 Forbidden\r\nServer: QuinnHTTP\r\nContent-type: " + *contentType + "\r\n\r\nRequested Resource is forbidden\n";
      write(socket, header.c_str(), header.size());
      //delete &header;
      updateTimes(req_start);
      return;
   }

   int log = open("./log.txt", O_CREAT | O_WRONLY | O_APPEND, 0664);
   std::string * ip = ips[socket];
   std::string * log_entry = new std::string("User at IP: " + *ip + " requested " + *docpath + "<br>");
   int wn = write(log, log_entry->c_str(), log_entry->size());
   write(log, "\n", 1);
   printf("wrote: %s\nand it was %d chars\n", log_entry->c_str(), wn);
   delete ip;
   delete log_entry;
   close(log);

   if (cpp_doc->find("/cgi-bin") != -1) {
      printf("args will be passed as %s\n", args.c_str());
      //time to fork
      if (cpp_doc->find(".so") == -1) {
         //not a loadable module, time to execvp!
         int pid = fork();
         if (pid == 0) {
            std::string program = docpath->substr(docpath->find_last_of('/') + 1, docpath->size() - docpath->find_last_of('/') + 1);
            //printf("the document is %s\n", program.c_str());f
            setenv("REQUEST_METHOD", "GET", 1);
            setenv("QUERY_STRING", args.c_str(), 1);
            dup2(socket, 1);
            std::string header = "HTTP/1.1 200 Document Follows\r\nServer: QuinnHTTP\r\n";
            write(socket, header.c_str(), header.size());
            char** arg_array = new char*[2];
            arg_array[0] = const_cast<char*>(program.c_str());
            arg_array[1] = NULL;
            execvp(docpath->c_str(), arg_array);
         }
         return;
      } else {
         //loadable module time!
         std::string program = docpath->substr(docpath->find_last_of('/') + 1, docpath->size() - docpath->find_last_of('/') + 1);
         printf("the document is %s\n", program.c_str());
         httprunfunc new_httprun;
         printf("the thing is %p\n", open_mods[program]);
         if (open_mods[program] == NULL) {
            //haven't loaded the mod, run the program
            printf("adding new module for %s\n", program.c_str());
            void * handle = dlopen(docpath->c_str(), RTLD_LAZY);
            if ( handle  == NULL ) {
               fprintf( stderr, "./hello.so not found\n");
               perror( "dlopen");
               exit(1);
            }

            //get the function
            new_httprun = (httprunfunc) dlsym(handle, "httprun");
            open_mods[program] = new_httprun;

         } else {
            //already loaded the module, just access the function
            new_httprun = open_mods[program];
         }

         if ( new_httprun == NULL ) {
            perror( "dlsym: httprun not found:");
            exit(1);
         }

         std::string header = "HTTP/1.1 200 Document Follows\r\nServer: QuinnHTTP\r\n";
         write(socket, header.c_str(), header.size());  

         printf("calling it with %d and %s\n", socket, args.c_str());
         new_httprun(socket, args.c_str());
         updateTimes(req_start);
         return;

      }

   }

   //creates a header
   std::string * header = new std::string("HTTP/1.1 200 Document follows\r\nServer: QuinnHTTP\r\nContent-type: " + *contentType + "\r\n\r\n");

   //writes header
   write(socket, header->c_str(), strlen(header->c_str()));

   if (docpath->find("/stats") !=-1 ) {
      handleStats(socket);
      updateTimes(req_start);
      return;
   }

   if (*contentType != "dir") {
      //opens file
      //printf("opening --%s--\n", docpath->c_str());
      int doc_fd = open(docpath->c_str(), O_RDONLY);
      if (doc_fd == -1) {
         printf("ERROR: DOC_FD IS -1\n");
         std::string header = "HTTP/1.1 404 File Not Found\r\nServer: QuinnHTTP\r\nContent-type: " + *contentType + "\r\n\r\nCould not find the specified URL. The server returned an error\n";
         write(socket, header.c_str(), header.size());
         //delete &header;
         updateTimes(req_start);
         return;
      }




      //reads data
      int maxLength;
      if (std::string::npos == contentType->find("image")) {
         //not an image
         maxLength = 4096;

      } else {
         maxLength = 65536;
      }


      char * data = (char *) malloc(sizeof(char) * maxLength);
      int dataLength = 0;
      char newerChar;
      int newn;
      while (( newn = read( doc_fd, &newerChar, sizeof(newerChar) ) ) > 0 ) {
         data[ dataLength ] = newerChar;
         dataLength++;
         if (dataLength == maxLength) {
            maxLength = maxLength * 2;
            data = (char *) realloc(data, maxLength);
         }
         // if (write(socket, &newerChar, sizeof(newerChar)) != newn) {
         //    break;
         // }

      }
      data[dataLength] = 0;
      close (doc_fd);


      //writes the data
      write(socket, data, dataLength);


      free(data);
      data = NULL;
   } else {

      std::string * dirDisplay = checkDir(docpath, args);
      write(socket, dirDisplay->c_str(), dirDisplay->size());
   }


   free(doc);
   doc = NULL;

   free(expanded);
   expanded = NULL;
   
   //get rid of the cpp stuff
   delete header;
   delete contentType;
   delete docpath;
   delete cpp_doc;
   delete cwd;

   updateTimes(req_start);
}

std::string * getContentType(std::string * docpath) {
   int start_of_type = docpath->find(".");
   
   if (docpath->find("/cgi-bin") != -1) {
      return new std::string("exec");
   }
   if (docpath->find("/stats") != -1) {
      //its stats
      return new std::string("stats");
   }
   if (docpath->find("/logs") != -1) {
      //its logs
      return new std::string("logs");
   }
   DIR * dir = opendir(docpath->c_str());
   if (dir != NULL) {
      //its a directory
      return new std::string("dir");
   }
   if (start_of_type == -1) {
      printf("NO type found uh oh\n");
      return new std::string("text/plain");
   }


   std::string ext = docpath->substr(start_of_type, docpath->size() - start_of_type);
   std::string * contentType;
   if (ext == ".html" || ext == ".html/") {
      contentType = new std::string("text/html");
   } else if (ext == ".gif" || ext == ".gif/") {
      contentType = new std::string("image/gif");
   } else if (ext == ".svg") {
      contentType = new std::string("image/svg+xml");
   } else if (ext == ".png" || ext ==".jpg") {
      contentType = new std::string("image/png");
   } //else if (ext == ".jpg")
   else {
      contentType = new std::string("text/plain");
   }
   return contentType;
}

std::string * finishRequest(int socket) {
   int n;
   // Currently character read
   unsigned char newChar;

   // Last character read
   unsigned char lastChar[] = {0, 0, 0};

   char req[1024];
   int reqLength = 0;

   //printf("Reading request now\n");
   while ( ( n = read( socket, &newChar, sizeof(newChar) ) ) > 0 ) {
         if ( lastChar[0] == '\015' && lastChar[1] == '\012' && lastChar[2] == '\015' && newChar == '\012' ) {
         // Discard previous two <CR> from request
         reqLength -=2;
         break;
      }
      req[reqLength] = newChar;
      reqLength++;
      //resets the last characters
      lastChar[2] = lastChar[1];
      lastChar[1] = lastChar[0];
      lastChar[0] = newChar;
   }
   req[reqLength] = 0;
   return new std::string(req);
}

void updateTimes(time_t req_start) {
   time_t req_end;
   time(&req_end);
   double cur_req_time = difftime(req_end, req_start);
   //update the req time counters if neccesary
   if (cur_req_time < min_req_time) {
      min_req_time = cur_req_time;
   }
   if (cur_req_time > max_req_time) {
      max_req_time = cur_req_time;
   }
   return;
}

std::string * checkDir(std::string * docpath, std::string args) {
   DIR * dir = opendir(docpath->c_str());
   if (dir == NULL) {
      return NULL;
   }

   //gets the default args
   if (args == "") {
      args = "?C=N;O=A";
   } 

   struct dirent * ent;
   std::string * ret = buildHTMLHead(*docpath, args);
   // std::string * ret = new std::string("<html><head> <title>Index of " + *docpath + "</title> </head>"
   // + "<body> <h1>Index of " + *docpath + "</h1> <table> <tbody><tr><th valign='top'><img src='/icons/blank.gif' alt='[ICO]'></th><th><a href='?C=N;O=D'>Name</a></th><th><a href='?C=M;O=A'>Last modified</a></th><th><a href='?C=S;O=A'>Size</a></th><th><a href='?C=D;O=A'  >Description</a></th></tr>"
   // + "<tr><th colspan=\"5\"><hr></th></tr>");
   int index = 0;
   std::vector<dirFile> files;
   int end = docpath->size() - 2;
   int diff = 2;
   while ((*docpath)[end] != '/') {
      diff++;
      end--;
   }

   char * c_cwd = (char *) calloc(sizeof(char), 256);
   c_cwd = getcwd(c_cwd, 256);
   std::string * cwd = new std::string(c_cwd);
   *cwd = *cwd + "/http-root-dir/htdocs/";
   int len = cwd->size();

   printf("docpath is %s, end is %d and cwd_size is %d\n", docpath->c_str(), end, len);

   std::string base_filepath = docpath->substr(len - 1, docpath->size() - (len - 1));
   printf("filpath is %s\n", base_filepath.c_str());
   

   while ( (ent = readdir(dir)) != NULL) {
      std::string * filename = new std::string(ent->d_name);
      std::string * filepath = new std::string(*docpath + *filename);
      std::string * contentType = getContentType(filepath);
      std::string * icon;
      printf("contenttype is %s\n", contentType->c_str());
      printf("filepath is %s\n", filepath->c_str());
      printf("filename is %s, find returned %ld\n", filename->c_str(), filename->find("dir"));
      if (filename->find("dir") != -1) {
         printf("icon set to folder\n");
         icon = new std::string("/icons/menu.gif");
      } else if (*contentType == "dir") {
         icon = new std::string("/icons/menu.gif");
      } else if (*contentType == "image/gif") {
         icon = new std::string("/icons/image.gif");
      } else {
         icon = new std::string("/icons/unknown.gif");
      }
      struct stat statbuf;

      int n = stat(((*docpath) + (*filename)).c_str(), &statbuf);
      struct tm * tm = gmtime(&statbuf.st_mtime);
      char * astime = ctime(&statbuf.st_mtime);
      std::string * lastMod = getModString(astime);
      std::string * size = new std::string(std::to_string(ent->d_reclen));
      std::string * html = new std::string("<tr><td valign=\"top\"><img src=\"" + *icon + "\" alt=\"[TXT]\"></td><td><a href=\"" + base_filepath + *filename + "\">" + *filename + "</a></td><td align=\"right\">" + *lastMod + "</td><td align=\"right\"> " + *size + "</td><td>&nbsp;</td></tr>");





      if ((*filename) != "." && (*filename) != "..") {
         dirFile newFile{};
         newFile.filename = filename;
         newFile.lastMod = lastMod;
         newFile.size = size;
         newFile.html = html;
         //adds the file to our list of files
         files.push_back(newFile);
      }

   }
   closedir(dir);

   printf("sorting with args %s\n", args.c_str());
   //figure out how to sort the files
   if (args[2] == 'M') {
      //sort by Modified date
      if (args[6] == 'A') {
         //ascending sort
         std::sort(files.begin(), files.end(), dateSortA);
      } else {
         std::sort(files.begin(), files.end(), dateSortD);
      }
   } else if (args[2] == 'S') {
      //sort by size
      if (args[6] == 'A') {
         //ascending sort
         std::sort(files.begin(), files.end(), sizeSortA);
      } else {
         std::sort(files.begin(), files.end(), sizeSortD);
      }
   } else {
      //sort by name or description
      if (args[6] == 'A') {
         //ascending sort
         std::sort(files.begin(), files.end(), nameSortA);
      } else {
         std::sort(files.begin(), files.end(), nameSortD);
      }
   }
   // std::sort(files.begin(), files.end(), dateSort);



   for (int i = 0; i < files.size(); i++) {
      *ret = *ret + *(files[i].html);
   }

   *ret = *ret + "</tbody></table></body></html>";

   printf("returning ");

   ret[ret->size() - 1] = '\0';
   printf("returning %s", ret->c_str());
   return ret;
}

bool nameSortA(dirFile a, dirFile b) {
   if (strcmp(a.filename->c_str(), b.filename->c_str()) < 0) {
      //a is smaller than b
      return true;
   }
   return false;
}

bool dateSortA(dirFile a, dirFile b) {
   if (strcmp(a.lastMod->c_str(), b.lastMod->c_str()) < 0) {
      //a is smaller than b
      return true;
   } else if (strcmp(a.lastMod->c_str(), b.lastMod->c_str()) == 0) {
      return nameSortA(a, b);
   }
   return false;
}

bool sizeSortA(dirFile a, dirFile b) {
   if (strcmp(a.size->c_str(), b.size->c_str()) < 0) {
      //a is smaller than b
      return true;
   }
   return false;
}
//inversions of previous functions to get the descending order
bool nameSortD(dirFile a, dirFile b) {
   if (strcmp(a.filename->c_str(), b.filename->c_str()) < 0) {
      //a is smaller than b
      return false;
   }
   return true;
}

bool dateSortD(dirFile a, dirFile b) {
   if (strcmp(a.lastMod->c_str(), b.lastMod->c_str()) < 0) {
      //a is smaller than b
      return false;
   } else if (strcmp(a.lastMod->c_str(), b.lastMod->c_str()) == 0) {
      return nameSortD(a, b);
   }
   return true;
}

bool sizeSortD(dirFile a, dirFile b) {
   if (strcmp(a.size->c_str(), b.size->c_str()) < 0) {
      //a is smaller than b
      return false;
   }
   return true;
}

// bool invert(bool (*f) (dirFile, dirFile), dirFile a, dirFile b) {
//    return !((*f)(a, b));
// }


std::string * getModString(char * c) {
   std::string * input = new std::string(c);
   std::string year = input->substr(input->size() - 5, 4);
   std::string month = input->substr(4, 3);
   printf("month name is %s\n", month.c_str());
   if (month == "Jan")
      month = "01";
   if (month == "Feb")
      month = "02";
   if (month == "Mar")
      month = "01";
   if (month == "Apr")
      month = "04";
   if (month == "May")
      month = "05";
   if (month == "Jun")
      month = "06";
   if (month == "Jul")
      month = "07";
   if (month == "Aug")
      month = "08";
   if (month == "Sep")
      month = "09";
   if (month == "Oct")
      month = "10";
   if (month == "Nov")
      month = "11";
   if (month == "Dec")
      month = "12";
   
   std::string day_hr_min = input->substr(8, 8);
   std::string * ret =  new std::string(year + "-" + month + "-" + day_hr_min);
   delete input;
   return ret;
}

std::string * buildHTMLHead(std::string docpath, std::string args) {
   std::string * ret = new std::string("<html><head> <title>Index of " + docpath + "</title> </head>" + "<body> <h1>Index of " + docpath + "</h1> <table> <tbody><tr><th valign='top'></th>");
   std::string * inverted_args = new std::string(args);
   if ((*inverted_args)[6] == 'A') {
      (*inverted_args)[6] = 'D';
   } else {
      (*inverted_args)[6] = 'A';
   }

   if ((*inverted_args)[2] == 'N') {
      //changing name
      *ret = *ret + "<th><a href='?" + *inverted_args + "'>Name</a></th><th><a href='?C=M;O=A'>Last modified</a></th><th><a href='?C=S;O=A'>Size</a></th><th><a href='?C=D;O=A'  >Description</a></th></tr>";
   } else if ((*inverted_args)[2] == 'M') {
      *ret = *ret + "<th><a href='?C=N;O=D'>Name</a></th><th><a href='?" + *inverted_args + "'>Last modified</a></th><th><a href='?C=S;O=A'>Size</a></th><th><a href='?C=D;O=A'  >Description</a></th></tr>";
   } else if ((*inverted_args)[2] == 'S') {
      *ret = *ret + "<th><a href='?C=N;O=D'>Name</a></th><th><a href='?C=M;O=A'>Last modified</a></th><th><a href='?" + *inverted_args + "'>Size</a></th><th><a href='?C=D;O=A'  >Description</a></th></tr>";
   } else if ((*inverted_args)[2] == 'D') {
      *ret = *ret + "<th><a href='?C=N;O=D'>Name</a></th><th><a href='?C=M;O=A'>Last modified</a></th><th><a href='?C=S;O=A'>Size</a></th><th><a href='?" + *inverted_args + "'  >Description</a></th></tr>";
   } else {
      *ret = *ret + "<th><a href='?C=N;O=D'>Name</a></th><th><a href='?C=M;O=A'>Last modified</a></th><th><a href='?C=S;O=A'>Size</a></th><th><a href='?C=D;O=A'  >Description</a></th></tr>";
   }
   
   int end = docpath.size() - 2;
   int diff = 2;
   while (docpath[end] != '/') {
      diff++;
      end--;
   }

   char * c_cwd = (char *) calloc(sizeof(char), 256);
   c_cwd = getcwd(c_cwd, 256);
   std::string * cwd = new std::string(c_cwd);
   *cwd = *cwd + "/http-root-dir/htdocs/";
   int length = end - (cwd->size() - 1);
   std::string parent = docpath.substr(cwd->size() - 1, length + 1);

   

   *ret = *ret + "<tr><th colspan=\"5\"><hr></th></tr><tr><td valign=\"top\"><img src=\"/icons/menu.gif\" alt=\"[PARENTDIR]\"></td><td><a href=\"" + parent + "\">Parent Directory</a></td><td align=\"right\"></td><td align=\"right\"> - </td><td>&nbsp;</td></tr>";

   free(c_cwd);
   c_cwd = NULL;
   delete cwd;
   delete inverted_args;
   return ret;
}

void handleStats(int socket) {
   time_t cur_time;
   time(&cur_time);
   std::string * uptime = new std::string(std::to_string(difftime(cur_time, server_start)));
   std::string * ret = new std::string("Author: Quinn Rouse<br>Uptime:" + *uptime + " seconds<br>Served " + std::to_string(req_count) + " Requests<br>Minimum Service Time: " + std::to_string(min_req_time) + " seconds<br>Maximum Service Time: " + std::to_string(max_req_time) + "<br>");
   write(socket, ret->c_str(), ret->size());
   delete ret;
   delete uptime;
   close(socket);
   return;
}

void handleLogs(int socket) {
   int log = open("./log.txt", O_CREAT | O_RDONLY | O_APPEND, 0664);
   
   int maxLength = 256;
   char * data = (char *) malloc(sizeof(char) * maxLength);
   int dataLength = 0;
   char newerChar;
   int newn;
   while (( newn = read( log, &newerChar, sizeof(newerChar) ) ) > 0 ) {
      data[ dataLength ] = newerChar;
      dataLength++;
      if (dataLength == maxLength) {
         maxLength = maxLength * 2;
         data = (char *) realloc(data, maxLength);
      }
   }
   data[dataLength] = 0;
   close (log);  

   //writes the data
   write(socket, data, dataLength);


   free(data);
   data = NULL;
   return;
}


