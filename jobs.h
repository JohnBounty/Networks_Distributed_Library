#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
// les sockets
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
// repertoire-fichiers
#include <dirent.h>
#include <sys/stat.h>

//=======================Globales=======================
// Les variables globales communes
#define	MAXLEN	1024
#define MAX_NAME_LEN 255
#define DGRAM_SIZE 258
#define SEGM_SIZE 1500
#define MAX_FPATH 4096

// structure pour stocker le catalogue
struct catalog_record
{
  uint16_t id ;
  uint8_t oct_len ;
  char name[MAX_NAME_LEN] ;
} ;

//=======================les fonctions utilitaires=======================
void usage (char *argv0)
{
    fprintf (stderr, "usage: %s [port]\n", argv0) ;
    exit (1) ;
}

void raler (char *msg)
{
    perror (msg) ;
    exit (1) ;
}

//=======================Les fonctions d'action=======================
//                          >>>Serveur<<<
// une fonction qui obtient l'information sur le dossier
unsigned int get_catalog_info(const char *dir)
{
    DIR  *wd ;
    unsigned int Count = 0 ;
    long int TotalSize = 0 ;

    struct dirent *entry ;
    struct stat buffer ;

    //on ouvre le dossier
    if ((wd = opendir(dir)) == NULL)
        raler("erreur de l'ouverture du dossier") ;

    //on parcours le dossier
    chdir(dir) ;
    printf("Le répertoire est: %s\n", dir) ;
    while ((entry = readdir(wd)) != NULL)
      {
      if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
        {

        if (stat(entry->d_name, &buffer) == -1)
  	      raler("erreur de stat") ;

        // verification du type de fichier
        if (S_ISREG(buffer.st_mode))
  	    {
  	       Count++;
  	       TotalSize+=buffer.st_size ;
  	    }
        }
      }

    chdir("..") ;
    //on ferme le dossier
    if (closedir(wd) == -1)
      raler("erreur de la fermeture du dossier") ;

    printf("La taille totale du répertoire est %ld octets.\n", TotalSize) ;
    printf("Le nombre total de fichiers dans le répertoire est %d.\n", Count) ;
    return Count;
  }

// une fonction qui récupére le catalogue à partir du dossier
void get_catalog(const char *dir, struct catalog_record Catalog[])
{
    DIR  *wd ;
    unsigned int i = 0 ;
    struct dirent *entry ;
    struct stat buffer ;

    //on ouvre le dossier
    if ((wd = opendir(dir)) == NULL)
        raler("erreur de l'ouverture du dossier") ;

    //on parcours le dossier
    chdir(dir) ;
    while ((entry = readdir(wd)) != NULL)
      {
      if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
        {

        if (stat(entry->d_name, &buffer) == -1)
  	      raler("erreur de stat") ;

        // verification du type de fichier
        if (S_ISREG(buffer.st_mode))
  	    {
           if (strlen(entry->d_name) < MAX_NAME_LEN)
           {
    	       Catalog[i].id = i ;
             Catalog[i].oct_len = strlen(entry->d_name) ;
    	       strncpy(Catalog[i].name, entry->d_name, MAX_NAME_LEN) ;
             i += 1 ;
           }
  	    }
        }
      }

    chdir("..") ;
    //on ferme le dossier
    if (closedir(wd) == -1)
      raler("erreur de la fermeture du dossier") ;
  }

// une fonction, qui envoie les enregistrements du catalogue
void send_record (int s, struct catalog_record Catalog[], unsigned int nb_files)
  {
    struct sockaddr_storage client_adr ;
    struct catalog_record one_record ;
    socklen_t len ;
    int r ;
    uint16_t val ;
    uint16_t *buf = malloc(sizeof (uint16_t)) ;

    if (buf == NULL)
      raler("malloc") ;

    // obtention de la requete
    len = sizeof client_adr ;
    r = recvfrom (s, buf, 2, 0, (struct sockaddr *) &client_adr, &len) ;
    if (r == -1) raler("recvform") ;
    val = ntohs(*buf) ;
    memset(buf, 0, 2) ;

    // l'envoi de DG
    if (val < nb_files)
    {
      one_record = Catalog[val] ;
      one_record.id = htons(one_record.id) ;
      r = sendto (s, &one_record, DGRAM_SIZE, 0,
        (struct sockaddr *) &client_adr, len) ;
    }

    // fin du catalogue
    else
    {
      one_record.id = val ;
      one_record.oct_len = 0 ;
      strncpy(one_record.name, "\0", MAX_NAME_LEN) ;
      r = sendto (s, &one_record, DGRAM_SIZE, 0,
        (struct sockaddr *) &client_adr, len) ;
      return ;
    }
    if (r == -1) raler ("sendto") ;

    free(buf) ;
}

// l'envoi d'un fichier
void send_file(int s, char* filename, const char *dir)
{
    char buf [SEGM_SIZE] ;
    char msg [SEGM_SIZE] ;
    char *cause = NULL ;
    int input, r, segment_size, empty_count ;
    char *er = NULL ;
    int if_error = 0 ;

    //on ouvre le dossier
    if ((opendir(dir)) == NULL)
    {
      if_error = 1 ;
      cause = "erreur de l'ouverture du dossier" ;
    }

    chdir(dir) ;

    input = open(filename, O_RDONLY | O_NDELAY) ;
    if (input == -1)
    {
      if_error = 1 ;
      cause = "open can't open " ;
    }

    // connexion et premier segment
    segment_size = read(input, buf, SEGM_SIZE-1) ;
    if (segment_size == -1)
    {
      if_error = 1 ;
      cause = "read" ;
    }

    if (if_error != 0)
    {
      er = strerror(errno) ;
      // on ne teste pas snprintf - on garde l'erreur initial
      snprintf(msg, SEGM_SIZE, "2%s: %s", cause, er) ;
      r = send(s, msg, SEGM_SIZE, 0) ;
      raler(cause) ;
    }

    // On suppose qu'après avoir établi la connexion,
    // toutes les autres opérations ne peuvent pas échouer
    // car elles sont similaires
    printf("L'envoi de %s\n", filename) ;
    memset(msg, '1', 1) ;
    memcpy(msg+1, buf, segment_size) ;

    r = send(s, msg, segment_size+1, 0) ;
    if (r < 0)
      raler("can't send") ;

    // les autres segments
    // on considére que 10 lectures vides d'affilée signifient
    // que l'EOF est atteint
    memset(buf, 0, sizeof buf) ;
    empty_count = 0 ;
    while( empty_count < 3 )
    {
      segment_size = read(input, buf, SEGM_SIZE) ;
      if (segment_size == -1)
        raler("read");
      if (segment_size == 0)
        empty_count += 1 ;

      r = send(s, buf, segment_size, 0) ;
      if (r < 0)
        raler("can't send") ;

      memset(buf, 0, sizeof buf) ;
    }

    if (close(input) == -1)
      raler("close") ;
}

//                          >>>Client<<<
void download(int s, uint16_t doc, const char* filepath)
{
    uint16_t d = htons(doc) ;
    char *buf = malloc(SEGM_SIZE) ;
    int r ;
    int segment_size = 0 ;
    int ToWrite ;

    if (buf == NULL)
      raler("malloc") ;

    r = send (s, &d, 2, 0) ;
    if (r < 0)
      raler("send") ;

    // le premier segment
    segment_size = recv (s, buf, SEGM_SIZE, 0) ;
    if (segment_size < 0)
      raler("recv") ;

    if (atoi(&buf[0]) == 2)
      {
        // on considère que le client a fait tout ce qu'il a pu
        // il affiche l'erreur du serveur sur la sortie standard
        // et termine avec 0
        printf("Error on server side:%s\n", buf+1) ;
        exit(0) ;
      }

    if (atoi(&buf[0]) == 1)
    {
      // si tout va bien, on crée un fichier
      ToWrite = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666) ;
      if (ToWrite == -1)
        raler("can't open") ;
      // et on écrit
      r = write(ToWrite, buf+1, segment_size-1) ;
    }
    if (r < 0)
      raler("write") ;

    // les autres segments
    memset(buf, 0, SEGM_SIZE) ;
    while ((segment_size = recv (s, buf, SEGM_SIZE, 0)))
    {
      if (segment_size < 0)
        raler("recv") ;

      r = write(ToWrite, buf, segment_size) ;
      if (r < 0)
        raler("write") ;
      memset(buf, 0, SEGM_SIZE) ;
     }

     if (close(ToWrite) == -1)
        raler("close_file") ;
     free(buf) ;
}
//=======================Les fonctions testes=======================

void simple_read(int sock_in)
{
  int r ;
  char buf [MAXLEN] ;

  while ((r = read (sock_in, buf, MAXLEN)) > 0){
     printf ("%s\n", buf) ;
     memset(buf, 0, sizeof buf);
   }

}

void simple_write(int sock_out)
{
  int r ;
  char buf [MAXLEN] ;

  while ((r = read (0, buf, MAXLEN)) > 0){
     r = write (sock_out, buf, MAXLEN) ;
     if (r < 0)
        raler("write") ;
     memset(buf, 0, sizeof buf);
   }

}

void lire_message (int s)
{
    struct sockaddr_storage client_adr ;
    socklen_t len ;
    int r, af ;
    void *nadr ;			/* au format network */
    char padr [INET6_ADDRSTRLEN] ;	/* au format presentation */
    char buf [MAXLEN] ;

    len = sizeof client_adr ;
    r = recvfrom (s, buf, MAXLEN, 0, (struct sockaddr *) &client_adr, &len) ;
    if (r == -1) raler ("recvform") ;
    af = ((struct sockaddr *) &client_adr)->sa_family ;
    switch (af)
    {
	case AF_INET :
	    nadr = & ((struct sockaddr_in *) &client_adr)->sin_addr ;
	    break ;
	case AF_INET6 :
	    nadr = & ((struct sockaddr_in6 *) &client_adr)->sin6_addr ;
	    break ;
    }
    inet_ntop (af, nadr, padr, sizeof padr) ;
    printf ("%s: nb d'octets lus = %d\n", padr, r) ;
}



//=======================DEBUGGING=======================
/*
      printf("Seg_sizze %d\n", segment_size) ;
      printf("%s\n", buf) ;
      printf("first:%c\n", buf[0]) ;

      printf("Of a size %d\n", segment_size) ;

      printf("CHECKASD:%c&%c\n", buf[0], buf[1]) ;
      printf("Writing:\n%s\n", buf) ;
      */

/*
  int main(int argc, char *argv[])
  {
    int nb_files = 0;
    if (argc == 2)
      {
        printf("On est ici\n");
      }
    else
      {
      printf("erreur de l'utilisation du programme; \
  donnez 3 arguments. Vous avez donne: %d..\n", argc);
      }
    // obtention de l'info sur le catalogue
    nb_files = get_catalog_info(argv[1]) ;
    struct catalog_record Catalog[nb_files] ;

    get_catalog(argv[1], Catalog) ;
    printf("Yo2\n") ;
    int i = 0 ;
    for (i = 0; i < nb_files; i++)
    {
      printf("Catalog[%d]\n", i) ;
      printf("id is %d\n", Catalog[i].id) ;
      printf("oct_len is %lld\n", Catalog[i].oct_len) ;
      printf("name is %s\n", Catalog[i].name) ;
    }

    return 0;
}
*/
