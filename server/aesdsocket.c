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
#include <pthread.h>
#include <time.h>
#include "queue.h"

struct thread_data
{
   pthread_t pthread_id;
   bool thread_complete_success;
   int accepted_fd;
   char accepted_ipstr[INET6_ADDRSTRLEN];
};

typedef struct slist_data_s slist_data_t;
struct slist_data_s
{
   struct thread_data thread_data_variable;
   SLIST_ENTRY(slist_data_s) entries;
};

#define DATA_SIZE    1024

int file_descriptor, socket_number = -1;
struct addrinfo *result;
bool caught_signal = false;
pthread_mutex_t mutex;
struct sockaddr_storage their_address;

void cleanup_log(void)
{
   closelog();
}

static void protected_write_to_file(char *buf, int number_of_bytes)
{
   if(number_of_bytes > 0)
   {   
      (void)pthread_mutex_lock(&mutex);
      if(write(file_descriptor, buf, number_of_bytes) == -1)
      {
         (void)pthread_mutex_unlock(&mutex);
         syslog(LOG_DEBUG, "File Write failed");
         cleanup_log();
         exit(-1);
      }
      (void)pthread_mutex_unlock(&mutex);
   }
}

static void signal_handler(int signal_number)
{
   if(  (signal_number == SIGINT)
      ||(signal_number == SIGTERM))
   {
      caught_signal = true;
   } 
}

static void signal_handler_timer(int signal_number)
{
   char outstring[200];
   time_t t;
   struct tm *temp;
   int length_of_time;
   
   if(signal_number == SIGRTMIN)
   {
      //10 second timer expired
      t = time(NULL);
      temp = localtime(&t);
      length_of_time = strftime(outstring, sizeof(outstring), "timestamp:%a, %d %b %Y %T %z\n", temp);
      protected_write_to_file(outstring, length_of_time);
   } 
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

void* threadfunc(void* thread_param)
{
   bool receiving_done, sending_done = false;
   unsigned int number_of_blocks = 0, number_of_bytes = 0;
   int number_of_bytes_received, number_of_bytes_from_file = -1;
   char *buf_ptr;
   
   struct thread_data* thread_func_args = (struct thread_data *) thread_param;
   
   syslog(LOG_DEBUG, "Accepted connection from %s\n", thread_func_args->accepted_ipstr);
      
   buf_ptr = calloc(DATA_SIZE, 1);
   if(buf_ptr == NULL)
   {
      syslog(LOG_ERR, "Could not allocate buf_ptr storage");
      thread_func_args->thread_complete_success = false;
      return(0);
   }
   number_of_blocks = 1;
   number_of_bytes = 0;
      
   do
   {
      receiving_done = false;
      sending_done = false;
      number_of_bytes_received = recv(thread_func_args->accepted_fd, &buf_ptr[number_of_bytes], DATA_SIZE, 0);
      
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
            thread_func_args->thread_complete_success = false;
            return(0);
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
      //cleanup_log();
      free(buf_ptr);
      thread_func_args->thread_complete_success = false;
      return(0);
   }
      
   //Write to all to file
   protected_write_to_file(&buf_ptr[0], number_of_bytes);
               
   //Send everything back.  Need to reset back to start of file
   fsync(file_descriptor);
   lseek(file_descriptor, 0, SEEK_SET);
      
   do
   {
      number_of_bytes_from_file = read(file_descriptor, &buf_ptr[0], DATA_SIZE);
      if(number_of_bytes_from_file > 0)
      {
         send(thread_func_args->accepted_fd, &buf_ptr[0], number_of_bytes_from_file, 0);
      }
      else
      {
         sending_done = true;
      }
         

   }while(   (sending_done == false) 
          && (caught_signal == false));
         
   close(thread_func_args->accepted_fd);
   syslog(LOG_DEBUG, "Closed connection from %s", thread_func_args->accepted_ipstr);
   free(buf_ptr);
   thread_func_args->thread_complete_success = true;
      
   return(thread_param);
}

int main (int argc, char *argv[])
{
   struct sigaction new_action;
   struct addrinfo addinfo;
   socklen_t address_size;
   int accepted_socket_number = -1;
   char ipstr[INET6_ADDRSTRLEN];
   pid_t pid;
   int one_value = 1;
   timer_t timer;
   struct sigevent evp;
   struct itimerspec ts;
   slist_data_t *datap = NULL, *datap_temp = NULL;
   
   memset(&new_action, 0, sizeof(struct sigaction));
   new_action.sa_handler = signal_handler;
   if(   (sigaction(SIGTERM, &new_action, NULL) != 0)
      || (sigaction(SIGINT, &new_action, NULL) != 0))
   {
      return(-1);
   }
   new_action.sa_handler = signal_handler_timer;
   if(sigaction(SIGRTMIN, &new_action, NULL) != 0)
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
      cleanup_log();
      return(-1);
   }
   
   pthread_mutex_init(&mutex, NULL);
   SLIST_HEAD(slisthead, slist_data_s) head;
   SLIST_INIT(&head);
   
   getaddrinfo(NULL, "9000", &addinfo, &result);
   socket_number = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
   setsockopt(socket_number, SOL_SOCKET, SO_REUSEADDR, &one_value, sizeof(one_value));
   bind(socket_number, result->ai_addr, result->ai_addrlen);
   listen(socket_number, 10);
   freeaddrinfo(result); 
   
   //if specified, run as daemon
   if((argc > 1) && (strcmp(argv[1], "-d") == 0))
   {
      //printf("Daemon mode\n");
      pid = fork();    
      if(pid == -1)
      {
         //Fork failed
         syslog(LOG_ERR, "fork() failed");
         cleanup_log();
         return(-1);
      }
      else if(pid == 0)
      {
         //Child process - keep going
      }
      else
      {
         //In the parent
         cleanup_log();    
         return(0);
      }
   }
   
   //Setup timer
   evp.sigev_value.sival_ptr = &timer;
   evp.sigev_notify = SIGEV_SIGNAL;
   evp.sigev_signo = SIGRTMIN;
   if(timer_create(CLOCK_REALTIME, &evp, &timer) != 0)
   {
      syslog(LOG_ERR, "Could not create timer");
      cleanup_log();
      return(-1);
   }
   ts.it_interval.tv_sec = 10;
   ts.it_interval.tv_nsec = 0;
   ts.it_value.tv_sec = 10;
   ts.it_value.tv_nsec = 0;
   if(timer_settime(timer, 0, &ts, NULL) != 0)
   {
      syslog(LOG_ERR, "Could not set timer");
      cleanup_log();
      return(-1);
   }
   
   do //Big loop - accept, recv, write to file, read back out in chunks, send contents back, and close
   {
      address_size = sizeof(their_address);
      accepted_socket_number = accept(socket_number, (struct sockaddr *)&their_address, &address_size);
      
      inet_ntop(their_address.ss_family, get_in_addr((struct sockaddr *)&their_address), ipstr, sizeof(ipstr));

      //Create thread and node
      datap = malloc(sizeof(slist_data_t));
      datap->thread_data_variable.thread_complete_success = false;
      datap->thread_data_variable.accepted_fd = accepted_socket_number;
      strcpy(datap->thread_data_variable.accepted_ipstr, ipstr);
      
      SLIST_INSERT_HEAD(&head, datap, entries);
     
      if(pthread_create((&datap->thread_data_variable.pthread_id), NULL, threadfunc, (void *)&(datap->thread_data_variable)) != 0)
      {
         syslog(LOG_DEBUG, "Could not create thread, exiting");
         free(datap);
         cleanup_log();
         return(-1);
      }
      
      //Check for thread cleanup
      SLIST_FOREACH_SAFE(datap, &head, entries, datap_temp)
      {
         if(datap->thread_data_variable.thread_complete_success == true)
         {
            pthread_join(datap->thread_data_variable.pthread_id, NULL);
            SLIST_REMOVE(&head, datap, slist_data_s, entries);
            free(datap);
         }      
      }
   
   } while(caught_signal == false);
   
   close(file_descriptor);
   close(socket_number);
   
   if(caught_signal == true)
   {
      syslog(LOG_DEBUG, "Caught signal, exiting");
      remove("/var/tmp/aesdsocketdata");
   }
   
   cleanup_log();
   
   //Check for thread cleanup in case we were terminated early
   SLIST_FOREACH_SAFE(datap, &head, entries, datap_temp)
   {
      if(datap->thread_data_variable.thread_complete_success == true)
      {
         pthread_join(datap->thread_data_variable.pthread_id, NULL);
         SLIST_REMOVE(&head, datap, slist_data_s, entries);
         free(datap);
      }      
   }
      
   return(0);
   
}
