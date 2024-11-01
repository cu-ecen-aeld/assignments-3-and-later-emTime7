#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <arpa/inet.h>


#define DATA_SIZE    1024

int file_descriptor, socket_number, accepted_socket_number = -1;
struct addrinfo *result;
char *buf_ptr;
bool caught_signal = false;

static void signal_handler(int signal_number)
{
   if(  (signal_number == SIGINT)
      ||(signal_number == SIGTERM))
   {
      caught_signal = true;
   } 
}

void cleanup_memory(void)
{
   closelog();
   close(file_descriptor);
   close(accepted_socket_number);
   close(socket_number);
   freeaddrinfo(result);
   free(buf_ptr);
}

void *get_in_addr(struct sockaddr *sa)
{
   if(sa->sa_family == AF_INET)
   {
      return (&(((struct sockaddr_in*)sa)->sin_addr));
   }
   else
   {
      return (&(((struct sockaddr_in6*)sa)->sin6_addr));
   }
}

int main (int argc, char *argv[])
{
   struct sigaction new_action;
   struct addrinfo addinfo;
   socklen_t address_size;
   struct sockaddr_storage their_address;
   int number_of_bytes_received, number_of_bytes_from_file = -1;
   char ipstr[INET6_ADDRSTRLEN];
   bool receiving_done, sending_done = false;
   unsigned int number_of_blocks = 0, number_of_bytes = 0;
   pid_t pid;
   int one_value = 1;
   
   memset(&new_action, 0, sizeof(struct sigaction));
   new_action.sa_handler = signal_handler;
   if(   (sigaction(SIGTERM, &new_action, NULL) != 0)
      || (sigaction(SIGINT, &new_action, NULL) != 0))
   {
      return(-1);
   }
   
   memset(&addinfo, 0, sizeof(addinfo));
   addinfo.ai_family = AF_UNSPEC;
   addinfo.ai_socktype = SOCK_STREAM;
   addinfo.ai_flags = AI_PASSIVE;
   
   openlog(NULL, 0, LOG_USER);
   
   file_descriptor = open("/var/tmp/aesdsocketdata", O_RDWR | O_CREAT | O_TRUNC, 0644);
   
   if(file_descriptor == -1)
   {
      syslog(LOG_ERR, "Could not open file /var/tmp/aesdsocketdata");
      cleanup_memory();
      return(-1);
   }
   
   getaddrinfo(NULL, "9000", &addinfo, &result);
   socket_number = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
   setsockopt(socket_number, SOL_SOCKET, SO_REUSEADDR, &one_value, sizeof(one_value));
   bind(socket_number, result->ai_addr, result->ai_addrlen);
   listen(socket_number, 10);
  
   //if specified, run as daemon
   if((argc > 1) && (strcmp(argv[1], "-d") == 0))
   {
      //printf("Daemon mode\n");
      pid = fork();    
      if(pid == -1)
      {
         //Fork failed
         syslog(LOG_ERR, "fork() failed");
         cleanup_memory();
         return(-1);
      }
      else if(pid == 0)
      {
         //Child process - keep going
      }
      else
      {
         //In the parent
         cleanup_memory();    
         return(0);
      }
   }
   
   do
   {
      receiving_done = false;
      sending_done = false;
      address_size = sizeof(their_address);
      accepted_socket_number = accept(socket_number, (struct sockaddr *)&their_address, &address_size);
      
      inet_ntop(their_address.ss_family, get_in_addr((struct sockaddr *)&their_address), ipstr, sizeof(ipstr));

      syslog(LOG_DEBUG, "Accepted connection from %s\n", ipstr);
      
      buf_ptr = calloc(DATA_SIZE, 1);
      if(buf_ptr == NULL)
      {
         syslog(LOG_ERR, "Could not allocate buf_ptr storage");
         cleanup_memory();
         return(-1);
      }
      number_of_blocks = 1;
      number_of_bytes = 0;
      
      do
      {

         number_of_bytes_received = recv(accepted_socket_number, &buf_ptr[number_of_bytes], DATA_SIZE, 0);
      
         if(number_of_bytes_received <= 0)
         {   
            receiving_done = true;
         }
         else
         {
            number_of_bytes += number_of_bytes_received;
            number_of_blocks++;
            buf_ptr = realloc(buf_ptr, (DATA_SIZE * number_of_blocks));
            if(buf_ptr == NULL)
            {
               syslog(LOG_ERR, "Could not re-allocate buf_ptr storage");
               cleanup_memory();
               return(-1);
            }
         }
         
         //If we receive a newline we are done.
         if(strchr(buf_ptr, '\n') > 0)
         {
            receiving_done = true;
         }

      }while(   (receiving_done == false) 
             && (caught_signal == false));
 
      if(caught_signal == true)
      {
         syslog(LOG_DEBUG, "Caught signal, exiting");
         remove("/var/tmp/aesdsocketdata");
         cleanup_memory();
         return(0);
      }
      
      //Write to all to file
      if(number_of_bytes > 0)
      {   
         if(write(file_descriptor, &buf_ptr[0], number_of_bytes) == -1)
         {
            syslog(LOG_DEBUG, "File Write failed");
            cleanup_memory();
            return(-1);
         }
      }
         
      //Send everything back.  Need to reset back to start of file
      fsync(file_descriptor);
      lseek(file_descriptor, 0, SEEK_SET);
      
      do
      {
         number_of_bytes_from_file = read(file_descriptor, &buf_ptr[0], DATA_SIZE);
         if(number_of_bytes_from_file > 0)
         {
            send(accepted_socket_number, &buf_ptr[0], number_of_bytes_from_file, 0);
         }
         else
         {
            sending_done = true;
         }
         

      }while(   (sending_done == false) 
             && (caught_signal == false));
         
      close(accepted_socket_number);
      syslog(LOG_DEBUG, "Closed connection from %s", ipstr);
      free(buf_ptr);
   
   } while(caught_signal == false);
   
   if(caught_signal == true)
   {
      syslog(LOG_DEBUG, "Caught signal, exiting");
      remove("/var/tmp/aesdsocketdata");
   }
   
   cleanup_memory();
   return(0);
   
}
