#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

int main (int argc, char *argv[])
{
   int file_descriptor;
   ssize_t result;
   
   openlog(NULL, 0, LOG_USER);

   //There should be 3 args.  Program name, filename, and string to write
   if(argc != 3)
   {
      syslog(LOG_ERR, "Invalid Number of Arguments: %d", argc);
      return(1);
   }
   
   file_descriptor = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
   
   if(file_descriptor == -1)
   {
      syslog(LOG_ERR, "Could not open file %s", argv[1]);
      return(1);
   }
   
   result = write(file_descriptor, argv[2], strlen(argv[2]));
   
   if(result != -1)
   {
      //Successful write
      syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
   }
   else
   {
      syslog(LOG_ERR, "Could not write file %s", argv[1]);
      //Close file
      if(close(file_descriptor) == -1)
      {
         syslog(LOG_ERR, "Error closing file %s", argv[1]);
      }
      return(1);
   }
   
   //Close file
   if(close(file_descriptor) == -1)
   {
      syslog(LOG_ERR, "Error closing file %s", argv[1]);
      return(1);
   }
   else
   {
      //Everything worked!
      return(0);
   }
}
