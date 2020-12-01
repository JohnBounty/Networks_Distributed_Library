CFLAGS = -g -Wall -Wextra -Werror # -DDEBUG=1

all: serveur lire catalogue

serveur: serveur.c

lire: lire.c

catalogue: catalogue.c

clean:
	rm -f *.o serveur lire catalogue
