#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "jobs.h"

int main (int argc, char *argv [])
{
    //=======================Initialisation=======================
    // les variables
    struct sockaddr_storage server_adr ;
    struct sockaddr_in *server_adr4 = (struct sockaddr_in *) &server_adr ;
    struct sockaddr_in6 *server_adr6 = (struct sockaddr_in6 *) &server_adr ;
    char *padr = NULL ;
    int s, r, family, port = 9000, doc ;
    socklen_t len ;
    char dir[MAX_NAME_LEN], filepath[MAX_FPATH] ;

    memset (&server_adr, 0, sizeof server_adr) ;

    // récupération d'arguments
    switch (argc)
    {
      case 3 :
        padr = argv [1] ;
        port = 9000 ;
        doc = atoi (argv [2]) ;
        break ;
	    case 4 :
        padr = argv [1] ;
        port = atoi (argv [2]) ;
        doc = atoi (argv [3]) ;
        break ;
	    default :
	      usage (argv [0]) ;
    }

    // On va créer un répertoire pour stocker les fichier téléchargés
    if (snprintf(dir, MAX_NAME_LEN, "Downloaded_From_%d", port) < 0)
      raler ("sprintf") ;
    // on va verifier si tel répertoire existe
    DIR *wd = opendir(dir) ;

    if (wd)  // existe
      closedir(wd) ;
    else if (errno == ENOENT) //n'existe pas
    {
    if (mkdir(dir, S_IRWXO | S_IRWXU) == -1)
      raler("mkdir") ;
    }
    else //opendir a échoué pour une autre raison
      raler("opendir") ;

    port = htons(port);
    printf("L'écriture dans %s.\n", dir) ;
    //=======================IPv4||IPv6=======================
    if (inet_pton (AF_INET6, padr, & server_adr6->sin6_addr) == 1)
    {
        len = sizeof *server_adr6 ;
        family = PF_INET6 ;
        server_adr6->sin6_family = AF_INET6 ;
        server_adr6->sin6_port = port ;
    }
    else if (inet_pton (AF_INET, padr, & server_adr4->sin_addr) == 1)
    {
        len = sizeof *server_adr4 ;
        family = PF_INET ;
        server_adr4->sin_family = AF_INET ;
        server_adr4->sin_port = port ;
    }
    else
    {
        fprintf (stderr, "%s: adresse '%s' non reconnue\n", argv [0], padr) ;
	      exit (1) ;
    }

    //=======================Sockets&Connexion=======================
    s = socket (family, SOCK_STREAM, 0) ;
    if (s == -1) raler ("socket") ;

    r = connect (s, (struct sockaddr *) &server_adr, len) ;
    if (r == -1) raler ("connect") ;

    //=======================Le travail=======================
    if (snprintf(filepath, MAX_FPATH, "%s/File_%d", dir, doc) < 0)
      raler ("sprintf") ;

    download(s, doc, filepath) ;
    close (s) ;
    printf("Le fichier est téléchargé et enregistré sous %s\n", filepath) ;
    exit (0) ;

}
