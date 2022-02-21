#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include <string.h> 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

#define POLLING_TIME 5
#define PORT_SIZE 6
#define COMMAND_SIZE 13
/* Dimensione massima del comando inserito da un utente (showneighbor <num>) */
#define COMMAND_BUFFER 19
/* Dimensione massima 'porta porta' */
#define PORT_BUFFER 12
/* Dimensione massima stringa contenente il network_msg: 'd IP porta' di lunghezza massima */
#define MESSAGE_BUFFER 36 
#define ADDR_SIZE 16


struct peer_data{
	char ip_peer[16];
	int port_peer;
};

struct peer_elem{
	struct peer_data data;
	struct peer_elem* next;
	int fd_fromComHandler[2];
	int fd_fromMainProc[2];
};

typedef struct peer_elem *List;

enum message_type{
	HELLO,				/* Peer si vuole registrare sulla rete */
	NEIGHBOR,			/* Il peer comunica i suoi vicini */
	NEW_NEIGHBOR,		/* Nuova connessione -> possibili nuovi vicini */
	STOP,				/* Peer vuole abbandonare la rete */
	ESC,				/* Chiusura del server */
	REQ_DATA,
	REPLY_DATA,
	FLOOD_FOR_ENTRIES,
	REQ_ENTRIES,
	PEER_NUMBER
};

struct ds_msg{
	enum message_type type;
	char address[ADDR_SIZE];
	int port1;
	int port2;
	int num_peer;
};

int search_neighbors(List network, List last, int peer_count, int* left, int* right, int searched_port){
	int i, change_flag;
	List current, previous;
	change_flag = 0;
	if(peer_count > 1){
		for(i = 0, previous = last, current = network; i < peer_count; i++, current = current->next){
			if(current->data.port_peer == searched_port){
				if((*left) != previous->data.port_peer){
					(*left) = previous->data.port_peer;
					change_flag = 1;
				}
				if((*right) != current->next->data.port_peer){
					(*right) = current->next->data.port_peer;
					change_flag = 1;
				}
				break;
			}
			previous = current;
		}
	}else{
		if((*left) > -1 && (*right) > -1){
			(*left) = -1;
			(*right) = -1;
			change_flag = 1;
		}
	}
	return change_flag;
}

void insert_peer(List* network, List* last, List* connected_peer, char* ip, int port, int* left_port, int* right_port){
	List new_elem, previous, current;
	int ret;
	/* creazione nuovo elemento della lista*/
	new_elem = malloc(sizeof(struct peer_elem)); 
		
	if (new_elem != NULL) {
		strncpy(new_elem->data.ip_peer, ip, (strlen(ip) + 1)); 
		new_elem->data.port_peer = port;
		new_elem->next = NULL; 
		
		ret = pipe(new_elem->fd_fromComHandler);
		if(ret < 0){
			perror("Errore in fase di creazione della pipe: \n");
			exit(-1);
		}
		
		ret = pipe(new_elem->fd_fromMainProc);
		if(ret < 0){
			perror("Errore in fase di creazione della pipe: \n");
			exit(-1);
		}
		
		previous = NULL; 
		current = *network;

		while(current != NULL && port > current->data.port_peer && !(current == *network && port > current->data.port_peer && previous != NULL)){
			previous = current;
			current = current->next; 
		}
		
		/* Sto inserendo il nuovo ultimo elemento */
		if(current == *network && previous != NULL){
			*last = new_elem;
		}
				
		/* sto inserendo il nuovo primo elemento */
		if(previous == NULL){
			/* è il primo elemento della rete */
			if(*network == NULL){
				*last = new_elem;
				*network = new_elem;
				(*left_port) = -1;
				(*right_port) = -1;
			}
			else{
				(*left_port) = (*last)->data.port_peer;
				(*right_port) = (*network)->data.port_peer;
			}
			new_elem->next = *network;
			*network = new_elem;
			(*last)->next = new_elem;
			*connected_peer = new_elem;
			return;
		}
				
		(*left_port) = previous->data.port_peer;
		(*right_port) = current->data.port_peer;
		previous->next = new_elem;
		new_elem->next = current;
		*connected_peer = new_elem;
	}
	else
		printf("Elemento non inserito, memoria non disponibile\n");
}

int delete_peer(List* network, List* last, int peer_count, int port){
	List previous, current, temp;
	if(port == (*network)->data.port_peer){
		temp = *network;
		*network = (*network)->next;
		(*last)->next = *network;
		
		close(temp->fd_fromMainProc[0]);
		close(temp->fd_fromMainProc[1]);
		close(temp->fd_fromComHandler[0]);
		close(temp->fd_fromComHandler[1]);
		free(temp);
	}
	else{
		previous = *network;
		current = (*network)->next;
		while(current != NULL && port != current->data.port_peer && !(current == *network && port != current->data.port_peer && previous != NULL)){
			previous = current;
			current = current->next;
		}
		if(current == *last){
			*last = previous;
		}
		temp = current;
		previous->next = current->next;
		
		close(temp->fd_fromMainProc[0]);
		close(temp->fd_fromMainProc[1]);
		close(temp->fd_fromComHandler[0]);
		close(temp->fd_fromComHandler[1]);
		free(temp);
	}
	peer_count = peer_count - 1;
	
	/*int i;
	printf("\n");
	for(i = 0, current = *network; i<peer_count; i++, current = current->next){
			printf("%d\n", current->data.port_peer);
	}
	printf("\n");*/
	
	return peer_count;
}

void clear_memory(List* network, int peer_count){
	int i;
	List temp;
	for(i = 0; i< peer_count; i++){
		temp = *network;
		*network = (*network)->next;
		
		close(temp->fd_fromMainProc[0]);
		close(temp->fd_fromMainProc[1]);
		close(temp->fd_fromComHandler[0]);
		close(temp->fd_fromComHandler[1]);
		free(temp);
	}
}

int main(int argc, char* argv[]){
	int ret, sd, sd_conn, addrlen, i, j, new_sd, max_sd, pid, port, flag;
	struct sockaddr_in my_addr, connecting_addr, cl_addr;
	List network, last, current, previous, connected_peer;
	int *left_port, *right_port;
	struct peer_data new_peer;
	fd_set complete_set,check_set;
	int fd_fromBoot[2];
	int fd_fromMain[2];
	int peer_count;
	char command_buffer[COMMAND_BUFFER];
	char message_buffer[MESSAGE_BUFFER];
	char port_string[PORT_SIZE];
	char command[COMMAND_SIZE];
	struct ds_msg message;
	
	left_port = (int*) malloc(sizeof(int));
	if(left_port == NULL){
		printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
		return 0;
	}
	right_port = (int*) malloc(sizeof(int));
	if(right_port == NULL){
		printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
		return 0;
	}
		
	peer_count = 0;
	network = NULL;
	last = NULL;
	srand(time(NULL));
	pid = -1;
	
	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(atoi(argv[1]));
	my_addr.sin_addr.s_addr = INADDR_ANY;
	
	ret = pipe(fd_fromMain);
	if(ret < 0){
		perror("Errore in fase di creazione della pipe: \n");
		exit(-1);
	}
	
	ret = pipe(fd_fromBoot);
	if(ret < 0){
		perror("Errore in fase di creazione della pipe: \n");
		exit(-1);
	}
	
	printf("\n");
	printf("***************************** DS COVID STARTED *****************************\n");
	printf("Digita un comando:\n");
	printf("\n");
	printf("1) help --> mostra i dettagli dei comandi\n");
	printf("2) showpeers --> mostra un elenco dei peer connessi\n");
	printf("3) showneighbor <peer> --> mostra i neighbor di un peer\n");
	printf("4) esc --> chiude il DSDettaglio comandi\n");
	printf("\n");
	printf("_\n");
	printf("\n");
	
	pid = fork();
	
	FD_ZERO(&complete_set);

	addrlen = sizeof(connecting_addr);
		
	if(pid != 0){
		int option = 1;
		sd_conn = socket(AF_INET, SOCK_STREAM, 0);
		ret = bind(sd_conn, (struct sockaddr*)&my_addr, sizeof(my_addr) ); 
		ret = listen(sd_conn, 10);
		if(ret < 0){
			perror("Errore in fase di bind: \n");
			exit(-1);
		}
		setsockopt(sd_conn, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
		
		max_sd = fd_fromBoot[0];
		FD_SET(fd_fromBoot[0], &complete_set);
		max_sd = (max_sd >= STDIN_FILENO ? max_sd : STDIN_FILENO);
		FD_SET(STDIN_FILENO, &complete_set);
		close(fd_fromMain[0]);
		close(fd_fromBoot[1]);
	}
	else{
		sd = socket(AF_INET,SOCK_DGRAM,0);
		ret = bind(sd, (struct sockaddr*)&my_addr, sizeof(my_addr)); 
		if(ret < 0){
			perror("Bind non riuscita\n");
			exit(0); 
		}
		max_sd = sd;
		FD_SET(sd, &complete_set);
		max_sd = (max_sd >= fd_fromMain[0] ? max_sd : fd_fromMain[0]);
		FD_SET(fd_fromMain[0], &complete_set);
		close(fd_fromMain[1]);
		close(fd_fromBoot[0]);
	}
		
	while(1){
		memset(&connecting_addr, 0, sizeof(connecting_addr));
		check_set = complete_set;
		select(max_sd + 1, &check_set, NULL, NULL, NULL);
				
		/*processo gestore dell'inizializzazione dei peer*/
		if(pid == 0){
			if(FD_ISSET(sd, &check_set)){
				do{
					ret = recvfrom(sd, message_buffer, MESSAGE_BUFFER, 0, (struct sockaddr*)&connecting_addr, (socklen_t *)&addrlen);
					if(ret < 0) 
						sleep(POLLING_TIME);
				} while(ret < 0);	
		
				sscanf(message_buffer, "%u %s %d %d %d", &message.type, message.address, &message.port1, &message.port2, &message.num_peer);
				
				if(message.type == HELLO){
					printf("Registrazione nuovo client: %s %d\n", message.address, message.port1);
					/* Mando i dati da inserire al processo che gestisce le strutture dati del server */
					do{
						ret = write(fd_fromBoot[1], message_buffer, strlen(message_buffer) + 1);
						if(ret != strlen(message_buffer) + 1){
							perror("Errore nella write del boot process!\n");
							sleep(POLLING_TIME);
						}
					} while(ret != strlen(message_buffer) + 1);
					
					/* Aspetto che il processo che gestisce le strutture dati del server mi mandi indietro i vicini del peer che sta richiedendo 
					   di essere aggiunto */
					do{
						ret = read(fd_fromMain[0], message_buffer, sizeof(message_buffer));
						if(ret < 0){
							perror("Errore nella read del boot process!\n");
							sleep(POLLING_TIME);
						}
					} while(ret < 0);

					/* Rispondo al peer con i suoi vicini */
					do{
						ret = sendto(sd, message_buffer, MESSAGE_BUFFER, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
						if(ret < 0)
							sleep(POLLING_TIME);
					} while(ret < 0);
				}
			}
			
			if(FD_ISSET(fd_fromMain[0], &check_set)){
				/*Chiusura del server*/
				do{
					ret = read(fd_fromMain[0], message_buffer, sizeof(message_buffer));
					if(ret < 0){
						perror("Errore nella read del boot process!\n");
						sleep(POLLING_TIME);
					}
				} while(ret < 0);
				
				sscanf(message_buffer, "%u %s %d %d %d", &message.type, message.address, &message.port1, &message.port2, &message.num_peer);
					
				if(message.type == ESC){
					close(fd_fromMain[0]);
					close(fd_fromBoot[1]);
					close(sd);
					return 0;
				}
			}
		}
		
		/* processo gestore delle strutture dati del server e dei comandi inseriti dall'utente*/
		if(pid != 0){
			//printf("in: %d, fd_fromBoot[0]: %d, sd_conn: %d\n", FD_ISSET(STDIN_FILENO, &check_set), FD_ISSET(fd_fromBoot[0], &check_set), FD_ISSET(sd_conn, &check_set));
			//printf("in: %d, fd_fromBoot[0]: %d\n", FD_ISSET(STDIN_FILENO, &check_set), FD_ISSET(fd_fromBoot[0], &check_set));
			
			if(FD_ISSET(STDIN_FILENO, &check_set)){
				fgets(command_buffer, COMMAND_BUFFER, stdin);
				strncpy(command, command_buffer, 13);
				command[12] = '\n';
				command[strcspn(command, "\n")] = '\0';
				
				if(strcmp(command, "help") == 0){
					printf("1) help: mostra il significato dei comandi e ciò che fanno\n");
					printf("2) showpeers: mostra l’elenco dei peer connessi alla rete \ntramite il loro numero di porta\n");
					printf("3) showneighbor [peer]: mostra i neighbor di un peer specificato \ncome parametro opzionale. Se non c’è il parametro, il comando \nmostra i neighbor di ogni peer\n");
					printf("4) esc: termina il DS. La terminazione del DS causa la terminazione \ndi tutti i peer. Opzionalmente, prima di chiudersi, i peer possono \nsalvare le loro informazioni su un file, ricaricato nel momento in \ncui un peer torna a far parte del network (esegue il boot).\n");
					printf("\n");
				}

				if(strcmp(command, "showpeers") == 0){
					if(peer_count == 0){
						printf("Attualmente non sono presenti peers registrati");
					}
					else{
						for(i = 0, current = network; i < peer_count; i++, current = current->next){
							printf("%s %d\n",current->data.ip_peer, current->data.port_peer);
						}
					}
					printf("\n");
				}
				
				if(strcmp(command, "showneighbor") == 0){
					for(i = 13, j = 0; j < PORT_SIZE; i++, j++){
						port_string[j] = command_buffer[i];
					}
					port = atoi(port_string);

					(*left_port) = -2;
					(*right_port) = -2;
					
					ret = search_neighbors(network, last, peer_count, left_port, right_port, port);
					
					if(ret == 1 && (*left_port) > -1 && (*right_port) > -1)
						printf("Vicino sinistro: %d, vicino destro: %d\n", (*left_port), (*right_port));
					else
						if((*left_port) == -2 && (*right_port) == -2 && ret == 0)
							printf("Il peer associato al numero di porta cercato non è presente\n");
						else
							printf("Il peer al momento non ha vicini\n");
					printf("\n");
				}
				
				if(strcmp(command, "esc") == 0){
					message.type = ESC;
					strcpy(message.address, "127.0.0.1");
					message.port1 = -1;
					message.port1 = -1;
					message.num_peer = -1;
					sprintf(message_buffer, "%u %s %d %d %d", message.type, message.address, message.port1, message.port2, message.num_peer);
					/*Chiusura del boot process e dei processi gestori dei peer*/
					do{
						ret = write(fd_fromMain[1], message_buffer, strlen(message_buffer) + 1);
						if(ret != strlen(message_buffer) + 1){
							perror("Errore nella write esc!\n");
							sleep(POLLING_TIME);
						}
					}while(ret != strlen(message_buffer) + 1);
						
					if(peer_count > 0){
						for(i = 0, current = network; i < peer_count; i++, current = current->next){
							do{
								ret = write(current->fd_fromMainProc[1], message_buffer, strlen(message_buffer) + 1);
								if(ret != strlen(message_buffer) + 1){
									perror("Errore nella write per messaggio di chiusura!\n");
									sleep(POLLING_TIME);
								}
							}while(ret != strlen(message_buffer) + 1);
						}
					
						clear_memory(&network, peer_count);
					}
					
					close(sd_conn);
					close(fd_fromMain[1]);
					close(fd_fromBoot[0]);

					free(left_port);
					free(right_port);
					return 0;
				}
			}
			
			if(FD_ISSET(fd_fromBoot[0], &check_set)){
				do{
					ret = read(fd_fromBoot[0], message_buffer, sizeof(message_buffer));
					if(ret < 0){
						perror("Errore nella read di HELLO!\n");
						sleep(POLLING_TIME);
					}
				} while(ret < 0);
			
				memset(&new_peer, 0, sizeof(new_peer));
				sscanf(message_buffer, "%u %s %d %d %d", &message.type, message.address, &message.port1, &message.port2, &message.num_peer);
				strncpy(new_peer.ip_peer, message.address, strlen(message.address) + 1);
				new_peer.port_peer = message.port1;

				if(message.type == HELLO){	
					/*Aggiunta del nuovo peer alla rete e ricerca dei vicini*/
					peer_count = peer_count + 1;
					insert_peer(&network, &last, &connected_peer, new_peer.ip_peer, new_peer.port_peer, left_port, right_port);
					
					max_sd = (max_sd >= connected_peer->fd_fromComHandler[0] ? max_sd : connected_peer->fd_fromComHandler[0]);
					FD_SET(connected_peer->fd_fromComHandler[0], &complete_set);
					
					message.port1 = (*left_port);
					message.port2 = (*right_port);
					sprintf(message_buffer, "%u %s %d %d %d", message.type, message.address, message.port1, message.port2, message.num_peer);
					
					do{
						ret = write(fd_fromMain[1], message_buffer, strlen(message_buffer) + 1);
						if(ret != strlen(message_buffer) + 1){
							perror("Errore nella write della risposta HELLO!\n");
							sleep(POLLING_TIME);
						}
					} while(ret != strlen(message_buffer) + 1);
					
					addrlen = sizeof(cl_addr);
					//sleep(1);
					if(peer_count > 1){
						for(i = 0, previous = last, current = network; i < peer_count; i++, current = current->next){
							if(current->data.port_peer == (*left_port) || current->data.port_peer == (*right_port)){
								message.type = NEW_NEIGHBOR;
								message.port1 = previous->data.port_peer;
								message.port2 = (current->next)->data.port_peer;
								message.num_peer = -1;
								sprintf(message_buffer, "%u %s %d %d %d", message.type, message.address, message.port1, message.port2, message.num_peer);
								do{
									ret = write(current->fd_fromMainProc[1], message_buffer, strlen(message_buffer) + 1);
									if(ret != strlen(message_buffer) + 1){
										perror("Errore nella write verso un gestore del peer!\n");
										sleep(POLLING_TIME);
									}
								} while(ret != strlen(message_buffer) + 1);
								
							}
							previous = current;
						}
					}
					
					/*Creazione connessione TCP con il DS e processo gestore associato alla nuova porta*/
					new_sd = accept(sd_conn, (struct sockaddr*) &cl_addr, (socklen_t *)&addrlen);	
					pid = fork();
				
					if(pid != 0){
						close(new_sd);
					}
					else{
						close(fd_fromMain[1]);
						close(fd_fromBoot[0]);
						close(sd_conn);
						break;
					}
				}
			}

			/*Comunicazione con i peer che gestiscono la singola porta*/
			for(i = 0, previous = last, current = network; i < peer_count; i++, current = current->next){
				if(FD_ISSET(current->fd_fromComHandler[0], &check_set)){
					do{
						ret = read(current->fd_fromComHandler[0], message_buffer, sizeof(message_buffer));
						if(ret < 0){
							perror("Errore nella read dei dati mandati da gestore di un processo!\n");
							sleep(POLLING_TIME);
						}
					}while(ret < 0);
					
					sscanf(message_buffer, "%u %s %d %d %d", &message.type, message.address, &message.port1, &message.port2, &message.num_peer);
					
					if(message.type == STOP){
						FD_CLR(current->fd_fromComHandler[0], &complete_set);
						peer_count = delete_peer(&network, &last, peer_count, current->data.port_peer);
						continue;
					}
					
					if(message.type == PEER_NUMBER){
						message.num_peer = peer_count;
						sprintf(message_buffer, "%u %s %d %d %d", message.type, message.address, message.port1, message.port2, message.num_peer);
												
						do{
							ret = write(current->fd_fromMainProc[1], message_buffer, strlen(message_buffer) + 1);
							if(ret != strlen(message_buffer) + 1){
								perror("Errore nella write verso un gestore del peer!\n");
								sleep(POLLING_TIME);
							}
						} while(ret != strlen(message_buffer) + 1);
					}
				}
				previous = current;
			}
		}
	}
		
		
	/* processo generato con la fork che gestirà il singolo peer */
	if(pid == 0){	
		FD_ZERO(&complete_set);
		max_sd = new_sd;
		FD_SET(new_sd, &complete_set);
		max_sd = (max_sd >= connected_peer->fd_fromMainProc[0] ? max_sd : connected_peer->fd_fromMainProc[0]);
		FD_SET(connected_peer->fd_fromMainProc[0], &complete_set);
				
		while(1){
			check_set = complete_set;
			select(max_sd + 1, &check_set, NULL, NULL, NULL);
				
			//printf("processo con porta %d, new_sd: %d, fd_fromMainProc[0]: %d\n", new_peer.port_peer, FD_ISSET(new_sd, &check_set), FD_ISSET(connected_peer->fd_fromMainProc[0], &check_set));
						
			if(FD_ISSET(new_sd, &check_set)){
				ret = recv(new_sd, (void*)message_buffer, MESSAGE_BUFFER, 0);
				if(ret < 0){
					perror("Errore in fase di comunicazione con i peer: \n"); 
					exit(-1);
				}
				
				sscanf(message_buffer, "%u %s %d %d %d", &message.type, message.address, &message.port1, &message.port2, &message.num_peer);
				
				if(message.type == STOP || message.type == PEER_NUMBER){	
					do{
						ret = write(connected_peer->fd_fromComHandler[1], message_buffer, strlen(message_buffer) + 1);
						if(ret != strlen(message_buffer) + 1){
							perror("Errore nella write dei messaggi provenienti dai peer!\n");
							sleep(POLLING_TIME);
						}
					} while(ret != strlen(message_buffer) + 1);
				}
				
				/* il peer che gestisco mi sta richiedendo il numero dei peer connessi alla rete*/
				if(message.type == PEER_NUMBER){
					do{
						ret = read(connected_peer->fd_fromMainProc[0], message_buffer, sizeof(message_buffer));
						if(ret < 0){
							perror("Errore nella read dei dati mandati da uno dei processi server!\n");
							sleep(POLLING_TIME);
						}
					}while(ret < 0);

					ret = send(new_sd, (void *) message_buffer, strlen(message_buffer) + 1, 0);
					if(ret < 0){
						perror("Errore in fase di comunicazione con i peer: \n"); 
					}
					printf("Mando al peer con porta %d il numero di peer presenti sulla rete\n", new_peer.port_peer);
				}

				/* il peer che gestivo sta richiedendo la disconnessione*/
				if(message.type == STOP){
					printf("Il peer con porta %d, si sta disconnettendo dalla rete\n", connected_peer->data.port_peer);
					break;
				}
			}
			
			/* se è la esc sul server terminerà tutto di conseguenza non sarà rilevante leggere il dato sulla pipe ma mi basterà 
				sfruttare la notifica della select e eseguire le operazioni di chiusura */
			if(FD_ISSET(connected_peer->fd_fromMainProc[0], &check_set)){
				do{
					ret = read(connected_peer->fd_fromMainProc[0], message_buffer, sizeof(message_buffer));
					if(ret < 0){
						perror("Errore nella read!\n");
						sleep(POLLING_TIME);
					}
				}while(ret < 0);
				sscanf(message_buffer, "%u %s %d %d %d", &message.type, message.address, &message.port1, &message.port2, &message.num_peer);
			
				if(message.type == ESC){
					ret = send(new_sd, (void*)message_buffer, MESSAGE_BUFFER, 0);
					if(ret < 0){
						perror("Errore in fase di comunicazione con il client: \n"); 
						exit(-1);
					}

					ret = recv(new_sd, (void*)message_buffer, MESSAGE_BUFFER, 0);
					if(ret < 0){
						perror("Errore in fase di comunicazione con i peer: \n"); 
						exit(-1);
					}
					break;
				}
				
				if(message.type == NEW_NEIGHBOR){
					flag = 0;

					if(message.port1 != (*left_port)){
						(*left_port) = message.port1;
						flag = 1;
					}
					
					if(message.port2 != (*right_port)){
						(*right_port) = message.port2;
						flag = 1;
					}
										
					if(flag == 1){
						printf("Comunico al peer con porta %d, il/i nuovo/i vicini: %d, %d\n", connected_peer->data.port_peer, message.port1, message.port2);
						ret = send(new_sd, (void*)message_buffer, strlen(message_buffer) + 1, 0);
						if(ret < 0){
							perror("Errore in fase di comunicazione con il client: \n"); 
						}
					}
	
				}
			}
		}
		
		close(connected_peer->fd_fromMainProc[0]);
		close(connected_peer->fd_fromComHandler[1]);
		close(connected_peer->fd_fromMainProc[1]);
		close(connected_peer->fd_fromComHandler[0]);
		close(new_sd);
		clear_memory(&network, peer_count);

		free(left_port);
		free(right_port);
	}
	return 0;
}