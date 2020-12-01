#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>
#include "jobs.h"

// Les variables globales
#define	SERVICE	"9000"	// == port
#define	MAXSOCK	32
#define MAXCON 20

int main (int argc, char *argv [])
{
    //=======================Initialisation=======================
    unsigned int nb_files ;

    // les variables TCP
    int s[MAXSOCK], sd, r, opt = 1 ;
    struct addrinfo hints_tcp, *res_tcp, *res0_tcp ;

    // les variables UDP
    int u [MAXSOCK], nsock, r2, o = 1 ;
    struct addrinfo hints_udp, *res_udp, *res0_udp ;
    char *cause ;
    char *serv = NULL ;
    char *rep ;

    // fork
    pid_t wpid ;
    int status ;
    int badexit = 0 ;

    // récupération d'arguments
    switch (argc)
    {
      case 2 :
         serv = SERVICE ;
         rep = argv[1] ;
	       break ;
	    case 3 :
         serv = argv [1] ;
         rep = argv [2] ;
	       break ;
	    default :
	       usage (argv [0]) ;
    }

    //=======================LeCatalogue=======================

    // obtention de l'info sur le catalogue
    nb_files = get_catalog_info(rep) ;
    struct catalog_record Catalog[nb_files] ;
    get_catalog(rep, Catalog) ;

    //=======================Sockets&Connexion=======================
    //                          >>>TCP<<<
    //                          >>>UDP<<<
    // création d'adresse comme structure addrinfo
    memset (&hints_udp, 0, sizeof hints_udp) ;
    hints_udp.ai_family = PF_UNSPEC ;
    hints_udp.ai_socktype = SOCK_DGRAM ;
    hints_udp.ai_flags = AI_PASSIVE ;

    memset (&hints_tcp, 0, sizeof hints_tcp) ;
    hints_tcp.ai_family = PF_UNSPEC ;
    hints_tcp.ai_socktype = SOCK_STREAM ;
    hints_tcp.ai_flags = AI_PASSIVE ;

    r2 = getaddrinfo (NULL, serv,  &hints_udp, &res0_udp) ;
    if (r2 != 0)
    {
      fprintf (stderr, "getaddrinfo: %s\n", gai_strerror (r2)) ;
      exit (1) ;
    }
    if ((r = getaddrinfo (NULL, serv,  &hints_tcp, &res0_tcp)) != 0)
    {
      fprintf (stderr, "getaddrinfo: %s\n", gai_strerror (r)) ;
      exit (1) ;
    }

    // création des MAXSOCK UDP&TCP sockets
    nsock = 0 ;
    res_tcp = res0_tcp ;
    for (res_udp = res0_udp; res_tcp && res_udp && nsock < MAXSOCK;\
       res_udp = res_udp->ai_next, res_tcp = res_tcp->ai_next)
    {
      u [nsock] = socket (res_udp->ai_family, res_udp->ai_socktype,\
         res_udp->ai_protocol) ;
      s [nsock] = socket (res_tcp->ai_family, res_tcp->ai_socktype,\
         res_tcp->ai_protocol) ;
      if (u [nsock] == -1)
        cause = "UDP-socket" ;
      else if (s [nsock] == -1)
        cause = "TCP-socket" ;
      else
      {
        // les options des sockets
        setsockopt (u [nsock], IPPROTO_IPV6, IPV6_V6ONLY, &o, sizeof o) ;
        setsockopt (s [nsock], IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof opt) ;
        setsockopt (s [nsock], SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt) ;

        // affectation d'adresses
        r2 = bind (u [nsock], res_udp->ai_addr, res_udp->ai_addrlen) ;
        r = bind (s [nsock], res_tcp->ai_addr, res_tcp->ai_addrlen) ;
        if (r2 == -1)
        {
          cause = "UDP-bind" ;
          close (u [nsock]) ;
        }
        else if (r == -1)
        {
          cause = "TCP-bind" ;
          close (s [nsock]) ;
        }
        else {
          listen (s [nsock], MAXCON) ;
          nsock++ ;
        }
      }
    }
    if (nsock == 0) raler (cause) ;
    freeaddrinfo (res0_udp) ;
    freeaddrinfo (res0_tcp) ;
    //=======================Le travail=======================
    // on genere les processus pour executer la commande
    printf("\n");
    switch (fork())
    {
      default:
        // TCP: server's job
        for (;;)
        {
          fd_set readfds ;
    	    int i, max = 0 ;

          FD_ZERO (&readfds) ;
          for (i = 0 ; i < nsock ; i++)
          {
            FD_SET (s [i], &readfds) ;
            if (s [i] > max)
    	         max = s [i] ;
          }

          if (select (max+1, &readfds, NULL, NULL, NULL) == -1)
            raler("select") ;

          for (i = 0 ; i < nsock ; i++)
          {
            struct sockaddr_storage client_adr ;
            socklen_t client_len ;

            // une connexion entrante
            if (FD_ISSET (s [i], &readfds))
            {
          	   client_len = sizeof client_adr ;
          	   sd = accept (s [i], (struct sockaddr *) &client_adr, &client_len) ;
 
               // on parallélise,
               // un processus fils est créé pour faire le travail lourd
          	   if (fork () == 0)
               {
                uint16_t doc ;

                r = recv (sd, &doc, 2, 0) ;
                if (r < 0)
                  raler("read") ;

                // fils obtient et convertit l'id du document
                doc = ntohs(doc) ;
                // et commence son travail
                send_file(sd, Catalog[doc].name, rep) ;

          	    exit (0) ;
          	   }
          	   close (sd) ;
            }
          }
        }
        exit(0);
      case -1:
        raler("erreur de fork");
        exit(1);
      case 0:
        // UDP: server's job
        for (;;)
        {
          // Initialisation de l'ensemble de descripteurs
          fd_set readfds ;
          int i, max = 0 ;

          FD_ZERO (&readfds) ;
          // on cherche le descripteur le plus élevé
          for (i = 0 ; i < nsock ; i++)
          {
            FD_SET (u [i], &readfds) ;
            if (u [i] > max)
              max = u [i] ;
          }

          if (select (max+1, &readfds, NULL, NULL, NULL) == -1)
            raler ("select") ;

          // une connexion entrante
          for (i = 0 ; i < nsock ; i++)
            if (FD_ISSET (u [i], &readfds))
            {
              // on répond
              // lire_message est pratique pour vérifier la taille
              // lire_message (u [i]) ;
              send_record(u [i], Catalog, nb_files) ;
            }
        }

        break;
    }

    // on attend
    int p ;
    for (p = 0; p < MAXCON; p++)
    {
      if((wpid = wait(&status)) == -1)
        raler("wait");
      if(!(WIFEXITED(status) && WEXITSTATUS(status)) == 0)
        badexit = 1;
    }

    if (badexit == 1)
      raler ("erreur de la terminaison de l'un des processus");

    exit (0) ;
}
