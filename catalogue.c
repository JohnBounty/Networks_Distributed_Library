#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include "jobs.h"

// Les variables globales
#define ENTRY_LIMIT 65536
#define MAX_COMM_TRIES 4

int main (int argc, char *argv [])
{
    //=======================Initialisation=======================
    // les variables
    struct sockaddr_storage server_adr ;
    struct sockaddr_in *server_adr4 = (struct sockaddr_in *) &server_adr ;
    struct sockaddr_in6 *server_adr6 = (struct sockaddr_in6 *) &server_adr ;
    socklen_t len ;
    char *padr = NULL ;
    int s, r, rs, family, port = 0, o ;
    int k = 0 ;

    // fork
    pid_t wpid ;
    int status ;
    int badexit = 0 ;

    // affichage de l'adresse de serveur
    void *nadr ;			/* au format network */
    int af ;

    // delai
    struct timeval timeout;
    float rem = atoi(argv[1]) % MAX_COMM_TRIES ;
    // 1 tv_usec = 1/1000000 de 1 tv_sec
    rem = rem / MAX_COMM_TRIES * 1000000 ;
    rem = (int)rem ;
    timeout.tv_sec = atoi(argv[1]) / MAX_COMM_TRIES ;
    timeout.tv_usec = rem;

    if (argc % 2 != 0) usage (argv [0]) ;
    printf("Obtention du catalogue...\n") ;
    printf("Le serveur || Port || L'enregistrement || La taille || Le nom\n") ;

    // on crée un processus fils pour chaque serveur donné
    for (k = 2 ; k < argc; k+=2){
      switch (fork())
      {
        case 0:
          memset (&server_adr, 0, sizeof server_adr) ;
          padr = argv [k] ;
          port = atoi (argv [k+1]) ;
          port = htons (port) ;

          //=======================IPv4||IPv6=======================
          if (inet_pton (AF_INET6, padr, & server_adr6->sin6_addr) == 1)
          {
            family = PF_INET6 ;
            server_adr6->sin6_family = AF_INET6 ;
            server_adr6->sin6_port = port ;
            len = sizeof *server_adr6 ;
          }
          else if (inet_pton (AF_INET, padr, & server_adr4->sin_addr) == 1)
          {
            family = PF_INET ;
            server_adr4->sin_family = AF_INET ;
            server_adr4->sin_port = port ;
            len = sizeof *server_adr4 ;
          }
          else
          {
            fprintf (stderr, "%s: adresse '%s'non reconnue\n",\
             argv [0], padr) ;
            exit (1) ;
          }

          //=======================Sockets&Connexion=======================
          s = socket (family, SOCK_DGRAM, 0) ;
          if (s == -1) raler ("socket") ;

          o = 1 ;
          if (setsockopt (s, SOL_SOCKET, SO_BROADCAST, &o, sizeof o) < 0 )
            raler("setsockopt");
          //delay
          if (setsockopt (s, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,
                sizeof(timeout)) < 0 )
              raler("setsockopt");
          if (setsockopt (s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
                sizeof(timeout)) < 0 )
              raler("setsockopt");

          af = ((struct sockaddr *) &server_adr)->sa_family ;
          switch (af)
          {
            case AF_INET :
              nadr = & ((struct sockaddr_in *) &server_adr)->sin_addr ;
              break ;
            case AF_INET6 :
              nadr = & ((struct sockaddr_in6 *) &server_adr)->sin6_addr ;
              break ;
          }
          inet_ntop (af, nadr, padr, sizeof padr) ;

          //=======================Le travail=======================

          uint16_t q ;
          int i = 0 ;
          int tries = 0 ;
          struct catalog_record one_record ;
          for (i = 0; i < ENTRY_LIMIT; i++)
          {
            // conversion NBO
            q = htons(i) ;

            // communication
            rs = sendto (s, &q, 2, 0, (struct sockaddr *) &server_adr, len) ;


            r = recvfrom (s, &one_record, DGRAM_SIZE, 0,
              (struct sockaddr *) &server_adr, &len) ;
            if (r == -1 || rs == -1)
            {
              // renvoyer si le délai est atteint
              if (errno == EWOULDBLOCK || errno == EAGAIN)
              {
                if (tries < MAX_COMM_TRIES)
                {
                  i -= 1 ;
                  tries += 1 ;
                }
                else
                {
                printf("%s sur le port %s est temporairement indisponible; \n",
                padr, argv[k+1]) ;
                printf("MAX_COMM_TRIES est atteint.\n") ;
                exit(0) ;
                }
              }
              else
              {
                if (rs == -1) raler ("sendto") ;
                if (r == -1) raler ("recvfrom") ;
              }
            }

            // affichage d'une ligne du catalogue
            if (one_record.oct_len != 0)
            {
              one_record.id = ntohs(one_record.id) ;
              one_record.name[one_record.oct_len] = '\0' ;
              printf("%s || %s || %d || %d || %s \n",
              padr, argv[k+1], one_record.id,
              one_record.oct_len, one_record.name) ;
            }
            // derniere ligne
            if ((one_record.oct_len == 0) && \
            (errno != EWOULDBLOCK && errno != EAGAIN))
              break;
          }

          close (s) ;

          exit (0) ;

        case -1:
          raler("FORK");
          exit(1);
        default:
        break ;
      }
    }

    // on attend
    for (k = 2; k < argc; k+=2)
    {
      if((wpid = wait(&status)) == -1)
        raler("wait");
      if(!(WIFEXITED(status) && WEXITSTATUS(status)) == 0)
        badexit = 1;
    }

if (badexit == 1)
  raler ("erreur de la terminaison de l'un des processus");
exit(0);
}
