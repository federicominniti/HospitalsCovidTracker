#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>

/* Lunghezza massima di un comando completo -> comando get variazione nuovo caso period/0 */
#define BUFFER_SIZE 48 
/* Lunghezza massima di un comando -> start/0 */
#define COMMAND_SIZE 6 
#define ADDR_SIZE 16 
#define PORT_SIZE 6
#define POLLING_TIME 5
#define RES_LEN 12
//7 8 T 01-01-2019 01-02-2019 r 38875 211 -1 821111111
#define MESSAGE_BUFFER 53
#define TYPE_SIZE 11
#define NUMBER_SIZE 7
/* Lunghezza massima percorso './GG_MM_AAAA_PPPPP.txt' */
#define PATH_SIZE 23
/* 'GG-MM-AAAA HH:MM:SS' */
#define TIMESTAMP_SIZE 20
/* 'GG-MM-AAAA HH:MM:SS,C,S,QQ,PORTA' */
#define ENTRY_SIZE 33
#define DATE_SIZE 11
#define TYPE_AGGR_SIZE 11
/*'GG-MM-AAAA_HH:MM:SS PPPPP T T 99999\n\0'*/
#define MAX_ENTRY_LEN 37
/*12-12-2012 12121\0*/
#define DATE_PORT 17

struct peer_data{
	char ip_peer[ADDR_SIZE];
	int port_peer;
};

enum message_type{
	//0
	HELLO,				/* Peer si vuole registrare sulla rete */
	//1
	NEIGHBOR,			/* Il peer comunica i suoi vicini */
	//2
	NEW_NEIGHBOR,		/* Nuova connessione -> possibili nuovi vicini */
	//3
	STOP,				/* Peer vuole abbandonare la rete */
	//4
	ESC,				/* Chiusura del server */
	//5
	REQ_DATA,
	//6
	REPLY_DATA,
	//7
	FLOOD_FOR_ENTRIES,
	//8
	REQ_ENTRIES,
	//9
	PEER_NUMBER			/*Richiedere il numero di peer collegati alla rete*/
};

enum service_type{
	TOTALE,				
	VARIAZIONE,
	NULLO
};
//1 192.100.233.222 22222 22222 22222\0
struct ds_msg{
	enum message_type type;
	char address[ADDR_SIZE];
	int port1;
	int port2;
	int num_peer;
};
//1 1 T 01-01-2020\0 01-03-2020\0 l 11111 1 11111 -\0\0 
struct peer_msg_request{
	enum message_type msg_type;
	enum service_type srv_type;
	char type;
	char date1[DATE_SIZE];
	char date2[DATE_SIZE];
	char dir;
	int requester_port;
	int size;
	int ttl;
	char *buffer;
};

struct file_entry{
	char timestamp[TIMESTAMP_SIZE];
	int port;
	char type;
	char symbol;
	int quantity;
};

struct day_entry_elem{
	char date[DATE_SIZE];
	int sender_port;
	char type;
	int total;
	struct day_entry_elem *next;
};

typedef struct day_entry_elem *date_entry;

struct entry_to_send{
	int port_requester;
	char *buffer;
	struct entry_to_send *next;
};

typedef struct entry_to_send *prepared_entry;

struct computed_data{
	enum service_type srv_type;
	char t_n;
	char start_date[DATE_SIZE];
	char end_date[DATE_SIZE];
	char *result;
	struct computed_data *next;
};

typedef struct computed_data *data_list;

char *strptime(const char *buf, const char *format, struct tm *tm);

int read_type(int begin_index, char *command_buffer, char *entry_type){
	int i;
	int j;
	i = begin_index;
	
	for(j = 0; command_buffer[i] != ' '; i++, j++){
		entry_type[j] = command_buffer[i];
	}
	entry_type[j] = '\0';
	i = i + 1;
	if(strcmp(entry_type, "tampone") != 0){
		entry_type[j] = ' ';
		j = j + 1;
		for(; command_buffer[i] != ' '; i++, j++){
			entry_type[j] = command_buffer[i];
		}
		entry_type[j] = '\0';
		i = i + 1;
		if(strcmp(entry_type, "nuovo caso") != 0){
			printf("Riprova! Il tipo inserito non è ricononosciuto; tipi disponibili: tampone, nuovo caso\n");
			return -1;
		}
		else
			return i;
	}
	else
		return i;
}

int read_date(int begin_index, char *command_buffer, char *date){
	int i, j;
	struct tm pnt_tm, *tm_today;
	time_t today, date_check, seconds;

	i = begin_index;
	
	if(command_buffer[begin_index] == '*'){
		date[0] = command_buffer[i];
		date[1] = '\0';
		return (i + 2);
	}

	for(j = 0; command_buffer[i] != '-' && command_buffer[i] != '\n'; i++, j++){
		if(command_buffer[i] != ':'){
			date[j] = command_buffer[i];	
		}
		else{
			date[j] = '-';	
		}
	}
	date[j] = '\0';
	
	strptime(date, "%d-%m-%Y", &pnt_tm);
	pnt_tm.tm_hour = 0;
	pnt_tm.tm_min = 0;
    pnt_tm.tm_sec = 0;
    pnt_tm.tm_isdst = -1;
	
	today = time(NULL);
	if(today == -1) {
		perror("Imposibile eseguire la funzione time()");
		return -1;
	}
	tm_today = localtime(&today);
	if(tm_today == NULL) {
		perror("Impossibile ottenere il timestamp attuale");
		return -1;
	}
	
	date_check = mktime(&pnt_tm);
	seconds = difftime(today, date_check);

	if(seconds > 0)
		return i + 1;
	else
		return -1;
}

void insert_head_data(data_list *head,  enum service_type srv_type, char n_t, char* start_date, char* end_date, char* result){
	data_list new_data;
	new_data = malloc(sizeof(struct computed_data));
	if (new_data != NULL) {
		new_data->srv_type = srv_type;
		new_data->t_n = n_t;
		strncpy(new_data->start_date, start_date, strlen(start_date) + 1);
		strncpy(new_data->end_date, end_date, strlen(end_date) + 1);
		new_data->result = (char *) malloc(sizeof(char) * (strlen(result) + 1));
		if(new_data->result == NULL){
			printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
			exit(0);
		}
		strncpy(new_data->result, result, strlen(result) + 1);
		new_data->next = *head;
		*head = new_data;
	}
	else
		printf("Impossibile salvare l'elemento aggregato; memoria non disponibile.\n");
}

/*Ricerca tra i dati aggregati gia calcolati*/
void search_result(data_list *head, enum service_type request, char type, char* date1, char* date2, char ** buffer_data){
	data_list *current; 
	(*buffer_data) = NULL; 
	for(current = head; (*current) != NULL; (*current) = (*current)->next){
		if((*current)->srv_type == request && (*current)->t_n == type && strcmp((*current)->start_date, date1) == 0 && strcmp((*current)->end_date, date2) == 0){
			(*buffer_data) = (char *) malloc(sizeof(char) * (strlen((*current)->result) + 1));
			if((*buffer_data) == NULL){
				printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
				exit(0);
			}
			strncpy((*buffer_data), (*current)->result, strlen((*current)->result) + 1);
			return;
		}
	}
}

void insert_head_entries(prepared_entry *head, char * buffer, int port_requester){
	prepared_entry elem;
	elem = malloc(sizeof( struct entry_to_send));
	if(elem != NULL) {
		elem->port_requester = port_requester;
		elem->buffer = buffer;
		elem->next = *head;
		*head = elem;
	}
	else
		printf("Impossibile salvare i dati da inviare al peer %d; memoria non disponibile.\n", port_requester);
}

void insert_head_date(date_entry *head, char *date, int sender_port, int total, char type){
	date_entry elem;
	elem = (date_entry)malloc(sizeof(struct day_entry_elem));
	if(elem != NULL) {
		strncpy(elem->date, date, strlen(date) + 1);
		elem->sender_port = sender_port;
		elem->total = total;
		elem->type = type;
		elem->next = *head;
		*head = elem;
	}
	else
		printf("Impossibile salvare entry del totale da inviare al peer %d; memoria non disponibile.\n", sender_port);
}

/*Ricerca di entry (secondo i parametri passati) tra i miei register e tra quelle passatemi da altri peer che hanno abbandonato la rete*/
char * search_entries(char *date1, char *date2, date_entry *entries_peer, int my_port, char searched_type, int requester_port, char *first_date){
	time_t date_seconds, tmp_seconds1, date2_seconds, first_seconds, rawtime;
	struct tm tm_date1, tm_date2, *next_date, tmp_date, tm_first, *pnt_tm;
	char searched_path[PATH_SIZE], analized_date[DATE_SIZE], now[TIMESTAMP_SIZE];
	FILE *file_ptr, *to_read;
	int total, len, flag1, flag2;
	struct file_entry entry;	
	date_entry analized_entry;
	char * buffer, *temporary_buffer;
	buffer = NULL;
	
	rawtime = time(NULL);
	pnt_tm = localtime(&rawtime);    
	sprintf(now, "%02d-%02d-%d_%02d:%02d:%02d", pnt_tm->tm_mday, (pnt_tm->tm_mon + 1), (1900 + pnt_tm->tm_year), pnt_tm->tm_hour, pnt_tm->tm_min, pnt_tm->tm_sec);
	
	if(strcmp(first_date, "-") != 0){
		strptime(first_date, "%d-%m-%Y", &tm_first);
		tm_first.tm_hour = 0;
		tm_first.tm_min = 0;
		tm_first.tm_sec = 0;
		tm_first.tm_isdst = -1;
		first_seconds = mktime(&tm_first);
	}
	if(strcmp(date1, "-") != 0){
		strptime(date1, "%d-%m-%Y", &tm_date1);
		strptime(date2, "%d-%m-%Y", &tm_date2);
		
		tm_date2.tm_hour = 0;
		tm_date2.tm_min = 0;
		tm_date2.tm_sec = 0;
		tm_date2.tm_isdst = -1;
		date2_seconds = mktime(&tm_date2);	
				
		tm_date1.tm_hour = 0;
		tm_date1.tm_min = 0;
		tm_date1.tm_sec = 0;
		tm_date1.tm_isdst = -1;
		date_seconds = mktime(&tm_date1);
		next_date = localtime(&date_seconds);
		do{
			tm_date1.tm_mday = next_date->tm_mday;
			tm_date1.tm_mon = next_date->tm_mon;
			tm_date1.tm_year = next_date->tm_year;
			tm_date1.tm_hour = 0;
			tm_date1.tm_min = 0;
			tm_date1.tm_sec = 0;
			tm_date1.tm_isdst = -1;
						
			sprintf(searched_path, "./%02d_%02d_%d_%d.txt", next_date->tm_mday, (next_date->tm_mon + 1), (1900 + next_date->tm_year), my_port);

			total = 0;	
			flag1 = 0;
			flag2 = 0;
			if(access(searched_path, F_OK ) == 0 && strcmp(first_date, "-") != 0 && date_seconds >= first_seconds) {
				to_read = fopen(searched_path,"r");
				if(to_read == NULL){
					printf("Errore nell'apertura del file %s!\n", searched_path);   
					continue;             
				}
				fseek(to_read, -(2*(MAX_ENTRY_LEN - 1)), SEEK_END);
								
				while(EOF != fscanf(to_read,"%s %d %c %c %d", entry.timestamp, &entry.port, &entry.type, &entry.symbol, &entry.quantity)){
					if(entry.symbol == 'T'  && entry.type == searched_type){
						total = entry.quantity;
						flag1 = 1;
						break;
					}
				}

				if(flag1 == 0){
					fseek(to_read, 0, SEEK_SET);
						
					while(EOF != fscanf(to_read,"%s %d %c %c %d", entry.timestamp, &entry.port, &entry.type, &entry.symbol, &entry.quantity)){
						if(entry.type == searched_type)
							total = total + entry.quantity;
					}
					flag2 = 1;
				} 
				fclose(to_read);
			
				if(flag2 == 1){
					sprintf(entry.timestamp, "%02d-%02d-%d_%s", next_date->tm_mday, (next_date->tm_mon + 1), (1900 + next_date->tm_year), (now + 11));
					entry.quantity = total;
					entry.symbol = 'T';
					entry.type = searched_type;

					file_ptr = fopen(searched_path, "a");
					if(file_ptr == NULL){
						printf("Errore nell'apertura del file %s!\n", searched_path);   
						continue;            
					}
		
					fprintf(file_ptr, "%s %05d %c %c %05d\n", entry.timestamp, entry.port, entry.type, entry.symbol, entry.quantity);
					fclose(file_ptr);
				}
							
				sprintf(analized_date, "%02d-%02d-%d", next_date->tm_mday, (next_date->tm_mon + 1), (1900 + next_date->tm_year));
				if(buffer == NULL){
					buffer = (char *) malloc(sizeof(char) * (strlen(analized_date) + 5 + 5 + 1 + 4) + 1);
					if(buffer == NULL){
						printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
						exit(0);
					}
					sprintf(buffer, "%s,%d,%d,%c;", analized_date, my_port, entry.quantity, searched_type);
				} else{
					len = strlen(buffer);
					buffer = (char *) realloc(buffer, (len + strlen(analized_date) + 5 + 5 + 1 + 4 + 1));
					temporary_buffer = (char *) malloc(sizeof(char) * (strlen(buffer) + 1));
					if(temporary_buffer == NULL){
						printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
						exit(0);
					}
					sprintf(temporary_buffer, "%s,%d,%d,%c;", analized_date, my_port, entry.quantity, searched_type);
					strcat(buffer, temporary_buffer);
				}
			}
	
			date_seconds = mktime(&tm_date1) + (24 * 60 * 60);
			next_date = localtime(&date_seconds);
														
		} while(tm_date2.tm_year != tm_date1.tm_year || tm_date2.tm_mon != tm_date1.tm_mon || tm_date2.tm_mday != tm_date1.tm_mday);
	}
	
	if(strcmp(date1, "-") != 0){
		strptime(date1, "%d-%m-%Y", &tm_date1);
		tm_date1.tm_hour = 0;
		tm_date1.tm_min = 0;
		tm_date1.tm_sec = 0;
		tm_date1.tm_isdst = -1;
		date_seconds = mktime(&tm_date1);
	}
	strptime(date2, "%d-%m-%Y", &tm_date2);
	tm_date2.tm_hour = 0;
	tm_date2.tm_min = 0;
	tm_date2.tm_sec = 0;
	tm_date2.tm_isdst = -1;
	date2_seconds = mktime(&tm_date2);	
	
	if(*entries_peer != NULL){	
		for(analized_entry = *entries_peer; analized_entry != NULL; analized_entry = analized_entry->next){
			strptime(analized_entry->date, "%d-%m-%Y", &tmp_date);
			tmp_date.tm_hour = 0;
			tmp_date.tm_min = 0;
			tmp_date.tm_sec = 0;
			tmp_date.tm_isdst = -1;
			tmp_seconds1 = mktime(&tmp_date);
						
			if(analized_entry->type == searched_type && ((strcmp(date1, "-") == 0 && difftime(date2_seconds, tmp_seconds1) >= 0) 
				|| (strcmp(date1, "-") != 0 && difftime(tmp_seconds1, date_seconds) >= 0 && difftime(date2_seconds, tmp_seconds1) >= 0))){
				
				sprintf(analized_date, "%02d-%02d-%d", tmp_date.tm_mday, (tmp_date.tm_mon + 1), (1900 + tmp_date.tm_year));
				if(buffer == NULL){
					buffer = (char *) malloc(sizeof(char) * (strlen(analized_date) + 5 + 5 + 1 + 4) + 1);
					if(buffer == NULL){
						printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
						exit(0);
					}
					sprintf(buffer, "%s,%d,%d,%c;", analized_date, analized_entry->sender_port, analized_entry->total, analized_entry->type);
				} else{
					len = strlen(buffer);
					buffer = (char *) realloc(buffer, (len + strlen(analized_date) + 5 + 5 + 1 + 4 + 1));
					temporary_buffer = (char *) malloc(sizeof(char) * (strlen(analized_date) + 5 + 5 + 1 + 4) + 1);
					if(temporary_buffer == NULL){
						printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
						exit(0);
					}
					sprintf(temporary_buffer, "%s,%d,%d,%c;", analized_date, analized_entry->sender_port, analized_entry->total, analized_entry->type);
					strcat(buffer, temporary_buffer);
				}
			}
		}
	}

	return buffer;
}

/*Calcolo del dato aggregato totale, tenendo conto del fatto che un peer puo abbandonare la rete e riconnettersi aggiungendo nuove entries al register gia esistente; cosi facendo potrebbero esserci problemi relativamente alle entries che precedentemente lui stesso aveva mandato ai vicini quando aveva abbandonato la rete. Il tutto è stato gestito mandando normalmente tute le entries e al momento del calcoloaggregato verrà considerata la entries con maggior valore associata a quel giorno - porta che ovviamente sarà quella piu aggiornata*/
int calculate_total(char *buffer_left, char *buffer_right, char *previous_part, char *date1, char *date2){
	int total, j, max, flag;
	char *unsized_buffer, *token, *buffer_data, *duplicate_entries1, *duplicate_entries2, *to_analize, *pattern, *remain_part, *token2, *token1, *analized_day, *result, start_date[DATE_SIZE], *backup_buffer, *already_analized, *cur_entry;
	struct day_entry_elem day_entry, day_app;
	const char terminator[2] = ";";
	const char coma = ',';
	struct tm tm_date1, tm_date2, *next_date, min_date, record_date;
	time_t date_seconds;
	total = 0;
		
	unsized_buffer = (char *) malloc(sizeof(char));
	if(unsized_buffer == NULL){
		printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
		exit(0);
	}
	strcpy(unsized_buffer, "");
	if(buffer_left != NULL && strcmp(buffer_left, "-") != 0){
		j = strlen(unsized_buffer);
		unsized_buffer = (char *) realloc(unsized_buffer, (j + strlen(buffer_left) + 1));
		strcpy(unsized_buffer, buffer_left);
	}

	if(buffer_right != NULL && strcmp(buffer_right, "-") != 0){
		j = strlen(unsized_buffer);
		unsized_buffer = (char *) realloc(unsized_buffer, (j + strlen(buffer_right) + 1));
		if(strcmp(unsized_buffer, "") == 0)
			strcpy(unsized_buffer, buffer_right);
		else
			strcat(unsized_buffer, buffer_right);
	}

	if(previous_part != NULL && strcmp(previous_part, "-") != 0){
		j = strlen(unsized_buffer);
		unsized_buffer = (char *) realloc(unsized_buffer, (j + strlen(previous_part) + 1));
		if(strcmp(unsized_buffer, "") == 0)
			strcpy(unsized_buffer, previous_part);
		else
			strcat(unsized_buffer, previous_part);
	}

	if(unsized_buffer!= NULL){
		while((buffer_data = strchr(unsized_buffer, coma)) != NULL){
			unsized_buffer[strlen(unsized_buffer) - strlen(buffer_data)] = ' ';
		}
			
		if(strcmp(date1, "*") == 0){
			result = (char *) malloc(sizeof(char) * (strlen(unsized_buffer) + 1));
			if(result == NULL){
				printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
				exit(0);
			}
			strcpy(result, unsized_buffer);
			strcpy(start_date, "-");
			token = strtok(result, terminator);
			while(token != NULL) {
				sscanf(token, "%s %d %d %c", day_entry.date, &day_entry.sender_port, &day_entry.total, &day_entry.type);
				if(strcmp(start_date, "-") == 0)
					strcpy(start_date, day_entry.date);
				else{
					strptime(start_date, "%d-%m-%Y", &min_date);
					strptime(day_entry.date, "%d-%m-%Y", &record_date);
					if(min_date.tm_year > record_date.tm_year || (min_date.tm_year == record_date.tm_year && min_date.tm_mon > record_date.tm_mon)
						|| (min_date.tm_year == record_date.tm_year && min_date.tm_mon == record_date.tm_mon && min_date.tm_mday > record_date.tm_mday))
						
						strcpy(start_date, day_entry.date);
				}
				token = strtok(NULL, terminator);
			}
			strcpy(date1, start_date);
			free(result);
		}
		strptime(date1, "%d-%m-%Y", &tm_date1);
		strptime(date2, "%d-%m-%Y", &tm_date2);
										
		tm_date2.tm_hour = 0;
		tm_date2.tm_min = 0;
		tm_date2.tm_sec = 0;
		tm_date2.tm_isdst = -1; 
									
		tm_date1.tm_hour = 0;
		tm_date1.tm_min = 0;
		tm_date1.tm_sec = 0;
		tm_date1.tm_isdst = -1;
		date_seconds = mktime(&tm_date1);
		next_date = localtime(&date_seconds);
				
		total = 0;
		do{
			tm_date1.tm_mday = next_date->tm_mday;
			tm_date1.tm_mon = next_date->tm_mon;
			tm_date1.tm_year = next_date->tm_year;
			tm_date1.tm_hour = 0;
			tm_date1.tm_min = 0;
			tm_date1.tm_sec = 0;
			tm_date1.tm_isdst = -1;
						
			analized_day = (char *) malloc(sizeof(char) * DATE_SIZE);
			if(analized_day == NULL){
				printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
				exit(0);
			}
			sprintf(analized_day, "%02d-%02d-%d", next_date->tm_mday, (next_date->tm_mon + 1), (next_date->tm_year + 1900));
			to_analize = (char *) malloc(sizeof(char) * (strlen(unsized_buffer) + 1));
			if(to_analize == NULL){
				printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
				return 0;
			}
			strcpy(to_analize, unsized_buffer);
			already_analized = NULL;
			flag = 0;
			max = -1;
			while((duplicate_entries1 = strstr(to_analize, analized_day)) != NULL){
				backup_buffer = (char *) malloc(sizeof(char) * (strlen(duplicate_entries1) + 1));
				if(backup_buffer == NULL){
					printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
					exit(0);
				}
				strcpy(backup_buffer, duplicate_entries1);
				token1 = strtok(backup_buffer, terminator);
				sscanf(token1, "%s %d %d %c", day_entry.date, &day_entry.sender_port, &day_entry.total, &day_entry.type);
				pattern = (char *) malloc(sizeof(char) * DATE_PORT);
				if(pattern == NULL){
					printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
					exit(0);
				}
				sprintf(pattern, "%s %d", day_entry.date, day_entry.sender_port);
				free(backup_buffer);
				
				cur_entry = (char *) malloc(sizeof(char) * (strlen(day_entry.date) + 5 + 5 + 1 + 3 + 1));
				if(cur_entry == NULL){
					printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
					exit(0);
				}
				sprintf(cur_entry, "%s %d %d %c", day_entry.date, day_entry.sender_port, day_entry.total, day_entry.type);
				
				if((already_analized != NULL && strstr(already_analized, (pattern + strlen(day_entry.date) + 1)) == NULL) || already_analized == NULL){
					remain_part = (char *) malloc(sizeof(char) * (strlen(duplicate_entries1) + 1));
					if(remain_part == NULL){
						printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
						exit(0);
					}
					strcpy(remain_part, (duplicate_entries1 + strlen(token1) + 1));
					max = day_entry.total;
					while((duplicate_entries2 = strstr(remain_part, pattern)) != NULL){
						backup_buffer = (char *) malloc(sizeof(char) * (strlen(duplicate_entries2) + 1));
						if(backup_buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							exit(0);
						}
						strcpy(backup_buffer, duplicate_entries2);
						token2 = strtok(backup_buffer, terminator);
						sscanf(token2, "%s %d %d %c", day_app.date, &day_app.sender_port, &day_app.total, &day_app.type);
						if(day_app.total > max){
							max = day_app.total;
						}
						strcpy(remain_part, (duplicate_entries2 + strlen(token2) + 1));
						free(backup_buffer);
					}
					flag = 1;
					free(remain_part);
					if(max != -1)
						total = total + max;
				}
				
				free(pattern);
				strcpy(to_analize, (duplicate_entries1 + strlen(cur_entry) + 1));
				free(cur_entry);
				
				if(flag == 1){
					if(already_analized == NULL){
						already_analized = (char *) malloc(sizeof(char) * (5 + 2));
						if(already_analized == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							exit(0);
						}
						sprintf(already_analized, "%d", day_entry.sender_port);
					} else{
						j = strlen(already_analized);
						already_analized = (char *) realloc(already_analized, (j + 5 + 2));
						backup_buffer = (char *) malloc(sizeof(char) * (5 + 1));
						if(backup_buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							exit(0);
						}
						sprintf(backup_buffer, "%d", day_entry.sender_port);
						strcat(already_analized, backup_buffer);
						free(backup_buffer);
					}
					j = strlen(already_analized);
					already_analized[j] = ',';
					already_analized[j + 1] = '\0';
				}
				flag = 0;
			}
		
			free(analized_day);
			free(to_analize);
			if(already_analized != NULL){	
				free(already_analized);
			}
			
			max = -1;
			date_seconds = mktime(&tm_date1) + (24 * 60 * 60);
			next_date = localtime(&date_seconds);
													
		} while(tm_date2.tm_year != tm_date1.tm_year || tm_date2.tm_mon != tm_date1.tm_mon || tm_date2.tm_mday != tm_date1.tm_mday);

		free(unsized_buffer);
	}
	return total;
}

/*Funzione che calcola la variazione sulla base dei dati passati; il calcolo del totale giornaliero viene eseguito come spegato per la calculate_total()*/
char * calculate_variation(char *buffer_left, char *buffer_right, char *previous_part, char *date1, char *date2, char type){
	char *temporary_buffer, *unsized_buffer, *analized_day, *result, *buffer_data, *token, start_date[DATE_SIZE], *already_analized, *duplicate_entries1, *to_analize, *backup_buffer, *token1, *pattern, *remain_part, *duplicate_entries2, *token2, *cur_entry;
	char number_string[NUMBER_SIZE];
	struct day_entry_elem day_entry;
	int previous, j, total, counter, ret, max, flag;
	const char terminator[2] = ";";
	const char coma = ',';
	struct tm tm_date1, tm_date2, *next_date, min_date, record_date;
	time_t date_seconds;
	
	temporary_buffer = NULL;
	previous = -1;
	unsized_buffer = (char *) malloc(sizeof(char));
	if(unsized_buffer == NULL){
		printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
		exit(0);
	}
	strcpy(unsized_buffer, "");
	if(buffer_left != NULL && strcmp(buffer_left, "-") != 0){
		unsized_buffer = (char *) malloc(sizeof(char) * strlen(buffer_left) + 1);
		if(unsized_buffer == NULL){
			printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
			exit(0);
		}
		strcpy(unsized_buffer, buffer_left);
	}

	if(buffer_right != NULL && strcmp(buffer_right, "-") != 0){
		j = strlen(unsized_buffer);
		unsized_buffer = (char *) realloc(unsized_buffer, (j + strlen(buffer_right) + 1));
		if(strcmp(unsized_buffer, "") == 0)
			strcpy(unsized_buffer, buffer_right);
		else
			strcat(unsized_buffer, buffer_right);
	}

	if(previous_part != NULL && strcmp(previous_part, "-") != 0){
		j = strlen(unsized_buffer);
		unsized_buffer = (char *) realloc(unsized_buffer, (j + strlen(previous_part) + 1));
		if(strcmp(unsized_buffer, "") == 0)
			strcpy(unsized_buffer, previous_part);
		else
			strcat(unsized_buffer, previous_part);
	}

	if(unsized_buffer!= NULL){
		while((buffer_data = strchr(unsized_buffer, coma)) != NULL){
			unsized_buffer[strlen(unsized_buffer) - strlen(buffer_data)] = ' ';
		}
						
		if(strcmp(date1, "*") == 0){
			result = (char *) malloc(sizeof(char) * (strlen(unsized_buffer) + 1));
			if(result == NULL){
				printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
				exit(0);
			}
			strcpy(result, unsized_buffer);
			strcpy(start_date, "-");
			token = strtok(result, terminator);
			while(token != NULL) {
				sscanf(token, "%s %d %d %c", day_entry.date, &day_entry.sender_port, &day_entry.total, &day_entry.type);
				if(strcmp(start_date, "-") == 0)
					strcpy(start_date, day_entry.date);
				else{
					strptime(start_date, "%d-%m-%Y", &min_date);
					strptime(day_entry.date, "%d-%m-%Y", &record_date);
					if(min_date.tm_year > record_date.tm_year || (min_date.tm_year == record_date.tm_year && min_date.tm_mon > record_date.tm_mon)
						|| (min_date.tm_year == record_date.tm_year && min_date.tm_mon == record_date.tm_mon && min_date.tm_mday > record_date.tm_mday))
						
						strcpy(start_date, day_entry.date);
				}
				token = strtok(NULL, terminator);
			}
			strcpy(date1, start_date);
			free(result);
		}
		
		strptime(date1, "%d-%m-%Y", &tm_date1);
		strptime(date2, "%d-%m-%Y", &tm_date2);
									
		tm_date2.tm_hour = 0;
		tm_date2.tm_min = 0;
		tm_date2.tm_sec = 0;
		tm_date2.tm_isdst = -1;
								
		tm_date1.tm_hour = 0;
		tm_date1.tm_min = 0;
		tm_date1.tm_sec = 0;
		tm_date1.tm_isdst = -1;
		date_seconds = mktime(&tm_date1);
		next_date = localtime(&date_seconds);
		
		do{
			tm_date1.tm_mday = next_date->tm_mday;
			tm_date1.tm_mon = next_date->tm_mon;
			tm_date1.tm_year = next_date->tm_year;
			tm_date1.tm_hour = 0;
			tm_date1.tm_min = 0;
			tm_date1.tm_sec = 0;
			tm_date1.tm_isdst = -1;
					
			total = 0;
			analized_day = (char *) malloc(sizeof(char) * DATE_SIZE);
			if(analized_day == NULL){
				printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
				exit(0);
			}
			sprintf(analized_day, "%02d-%02d-%d", next_date->tm_mday, (next_date->tm_mon + 1), (next_date->tm_year + 1900));

			to_analize = (char *) malloc(sizeof(char) * (strlen(unsized_buffer) + 1));
			if(to_analize == NULL){
				printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
				exit(0);
			}
			strcpy(to_analize, unsized_buffer);
			already_analized = NULL;
			flag = 0;
			max = -1;
			while((duplicate_entries1 = strstr(to_analize, analized_day)) != NULL){
				backup_buffer = (char *) malloc(sizeof(char) * (strlen(duplicate_entries1) + 1));
				if(backup_buffer == NULL){
					printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
					exit(0);
				}
				strcpy(backup_buffer, duplicate_entries1);
				token1 = strtok(backup_buffer, terminator);
				sscanf(token1, "%s %d %d %c", day_entry.date, &day_entry.sender_port, &day_entry.total, &day_entry.type);
				pattern = (char *) malloc(sizeof(char) * DATE_PORT);
				if(pattern == NULL){
					printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
					exit(0);
				}
				sprintf(pattern, "%s %d", day_entry.date, day_entry.sender_port);
				
				cur_entry = (char *) malloc(sizeof(char) * (strlen(day_entry.date) + 5 + 5 + 1 + 3 + 1));
				if(cur_entry == NULL){
					printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
					exit(0);
				}
				sprintf(cur_entry, "%s %d %d %c", day_entry.date, day_entry.sender_port, day_entry.total, day_entry.type);
				
				free(backup_buffer);
				if((already_analized != NULL && strstr(already_analized, (pattern + strlen(day_entry.date) + 1)) == NULL) || already_analized == NULL){
					remain_part = (char *) malloc(sizeof(char) * (strlen(duplicate_entries1) + 1));
					if(remain_part == NULL){
						printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
						exit(0);
					}
					strcpy(remain_part, (duplicate_entries1 + strlen(token1) + 1));
					max = day_entry.total;
					while((duplicate_entries2 = strstr(remain_part, pattern)) != NULL){
						backup_buffer = (char *) malloc(sizeof(char) * (strlen(duplicate_entries2) + 1));
						if(backup_buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							exit(0);
						}
						strcpy(backup_buffer, duplicate_entries2);
						token2 = strtok(backup_buffer, terminator);
						sscanf(token2, "%s %d %d %c", day_entry.date, &day_entry.sender_port, &day_entry.total, &day_entry.type);
						if(day_entry.total > max){
							max = day_entry.total;
						}
						strcpy(remain_part, (duplicate_entries2 + strlen(token2) + 1));
						free(backup_buffer);
					}
					free(remain_part);
					flag = 1;
					if(max != -1)
						total = total + max;
				}
				
				strcpy(to_analize, (duplicate_entries1 + strlen(cur_entry) + 1));
				
				if(flag == 1){
					if(already_analized == NULL){
						already_analized = (char *) malloc(sizeof(char) * (5 + 2));
						if(already_analized == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							exit(0);
						}
						sprintf(already_analized, "%d", day_entry.sender_port);
					} else{
						j = strlen(already_analized);
						already_analized = (char *) realloc(already_analized, (j + 5 + 2));
						backup_buffer = (char *) malloc(sizeof(char) * (5 + 1));
						if(backup_buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							exit(0);
						}
						sprintf(backup_buffer, "%d", day_entry.sender_port);
						strcat(already_analized, backup_buffer);
						free(backup_buffer);
					}
					already_analized[strlen(already_analized)] = ',';
					already_analized[strlen(already_analized)] = '\0';
				}
				flag = 0;
				free(pattern);
			}
		
			free(analized_day);
			free(to_analize);
			if(already_analized != NULL){	
				free(already_analized);
			}
			
			if(previous != -1){
				counter = total;
				total = total - previous;
				previous = counter;
				sprintf(number_string, "%d,", total);
				if(temporary_buffer != NULL){
					ret = strlen(temporary_buffer);
					temporary_buffer = (char *) realloc(temporary_buffer, (ret + strlen(number_string) + 1));
					strcat(temporary_buffer, number_string);
				} else{
					temporary_buffer = (char *) malloc(sizeof(char) * (strlen(number_string) + 1));
					if(temporary_buffer == NULL){
						printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
						return 0;
					}
					strcpy(temporary_buffer, number_string);
				}
			}
			else
				previous = total;
															
			date_seconds = mktime(&tm_date1) + (24 * 60 * 60);
			next_date = localtime(&date_seconds);
			max = -1;
												
		} while(tm_date2.tm_year != tm_date1.tm_year || tm_date2.tm_mon != tm_date1.tm_mon || tm_date2.tm_mday != tm_date1.tm_mday);

		j = strlen(temporary_buffer);

		temporary_buffer[j - 1] = '\0';
		free(unsized_buffer);
	}
	else{
		temporary_buffer = (char *) malloc(sizeof(char));
		if(temporary_buffer == NULL){
			printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
			return 0;
		}
		strcpy(temporary_buffer, "");
	}
	return temporary_buffer;
	
}

/*Funzione che restituisce una stringa contenente le entries raccolte da un certo peer*/
char * complete_list(prepared_entry *list_of_entries, int requester_port){
	prepared_entry cur_entries, garbage_entries, prec_entries;
	char *previous_part;
	previous_part = NULL;
	if(list_of_entries != NULL){
		for(prec_entries = NULL, cur_entries = *list_of_entries; cur_entries != NULL; cur_entries = cur_entries->next){
			if(cur_entries->port_requester == requester_port){
				previous_part = (char *) malloc(sizeof(char) * (strlen(cur_entries->buffer) + 1));
				if(previous_part == NULL){
					printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
					exit(0);
				}
				strcpy(previous_part, cur_entries->buffer);
				garbage_entries = cur_entries;
				if(prec_entries == NULL)
					*list_of_entries = garbage_entries->next;
				else
					prec_entries->next = garbage_entries->next;
				free(garbage_entries);
				break;
			}
			prec_entries = cur_entries;
		}
	}
	return previous_part;
}

int main(int argc, char* argv[]){
	int ret, addrlen, max_sd, i, j, count, flag_left, flag_right, quantity, left_right, counter, total/*, previous*/, flag_all, app;
	int neighbours[2];
	int sd_peer, sd_ds, sd_left, sd_right, sd_neigh;
	struct sockaddr_in srv_addr, my_addr, right_addr, left_addr, peer_addr;
	struct peer_data my_data;
	fd_set timeout_set,check_set, complete_set;
	struct timeval timeout;
	double peer_count;
	
	char command_buffer[BUFFER_SIZE];
	char command[COMMAND_SIZE];
	char port_string[PORT_SIZE];
	char addr_ds[ADDR_SIZE];
	char searched_path[PATH_SIZE];
	
	char message_buffer[MESSAGE_BUFFER];
	char message_received[MESSAGE_BUFFER];
	struct ds_msg message;
	
	char number_string[NUMBER_SIZE];
	char entry_type[TYPE_SIZE];
	char aggr_type[TYPE_AGGR_SIZE];
	char path[PATH_SIZE];
	
	const char terminator[2] = ";";
	const char coma_str[2] = ",";
	const char pipe = '|';
	char type_found = '-';
	char today_str[DATE_SIZE];
	char analized_date[DATE_SIZE];
	char now[TIMESTAMP_SIZE];
	
	struct peer_msg_request peer_request;
	FILE *file_ptr, *to_read, *new_file;
	time_t rawtime, date_seconds, date2_seconds, today_time;
	struct tm *pnt_tm, *pnt_date2, tm_date1, tm_date2, *next_date, *tmp_today;
	struct file_entry entry;
	char *unsized_buffer, *buffer_data, *result, *previous_part, *buffer_left, *buffer_right, *token, *temporary_buffer, *return_buffer;
	char first_date[DATE_SIZE] = "-";
	 
	prepared_entry list_of_entries, garbage_entries, cur_entries, prec_entries; 
	data_list result_list, garbage_result;
	struct day_entry_elem day_entry;
	date_entry entries_peer, garbage_peer;
	
	int option = 1;
	
	sd_peer = socket(AF_INET, SOCK_DGRAM, 0);
	sd_ds = socket(AF_INET, SOCK_STREAM, 0);
	
	sd_neigh = socket(AF_INET, SOCK_STREAM, 0);
	sd_right = socket(AF_INET, SOCK_STREAM, 0);
	
	setsockopt(sd_peer, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
	setsockopt(sd_neigh, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(atoi(argv[1]));
	my_addr.sin_addr.s_addr = INADDR_ANY;
	
	ret = bind(sd_peer, (struct sockaddr*)&my_addr, sizeof(my_addr));
	if( ret < 0 ){
		perror("Connessione non riuscita\n");
		exit(0);
	}
	
	ret = bind(sd_neigh, (struct sockaddr*)&my_addr, sizeof(my_addr));
	if(ret < 0){
		perror("Connessione non riuscita\n");
		exit(0);
	}
	listen(sd_neigh, 2);
	
	memset(&my_data, 0, sizeof(my_data));
	strncpy(my_data.ip_peer, "127.0.0.1", 10);
	my_data.port_peer = atoi(argv[1]);
	
	FD_ZERO(&complete_set);
	max_sd = STDIN_FILENO;
	FD_SET(STDIN_FILENO, &complete_set);
	max_sd = (max_sd >= sd_neigh ? max_sd : sd_neigh);
	FD_SET(sd_neigh, &complete_set);
	max_sd = (max_sd >= sd_peer ? max_sd : sd_peer);
	FD_SET(sd_peer, &complete_set);
	
	result_list = NULL;
	list_of_entries = NULL;
	unsized_buffer = NULL;
	peer_request.buffer = NULL;
	buffer_left = NULL;
	buffer_right = NULL;
	entries_peer = NULL;
	
	memset(&peer_addr, 0, sizeof(peer_addr)); 
	flag_left = 0;
	flag_right = 0;
	
	neighbours[0] = -2;
	neighbours[1] = -2;
	
strcpy(first_date, "01-06-2021"); //SERVE PER TESTARE L'APPLICAZIONE ALTRIMENTI VERREBBE CONSIDERATA COME DATA DEL PRIMO INSERIMENTO DI DATI

	printf("\n");
	printf("**************************** PEER COVID STARTED ****************************\n");
	printf("Digita un comando:\n");
	printf("\n");
	printf("1) start <DS_addr> <DS_port> --> contatta il server per entrare a far parte della rete\n");
	printf("2) add <type> <quantity --> registra tamponi/nuovi casi\n");
	printf("3) showneighbor <peer> --> mostra i neighbor di un peer\n");
	printf("4) get aggr type period --> esegue un aggregazione di dati secondo i parametri specificati\n");
	printf("\n");
	printf("_\n");
	printf("\n");

	while(1){
		check_set = complete_set;
		select(max_sd + 1, &check_set, NULL, NULL, NULL);
		
		/*if(flag_left == 1 && flag_right == 1)
			printf("STDIN_FILENO: %d, sd_ds: %d, sd_peer: %d, sd_neigh: %d, sd_left: %d, sd_right: %d\n", FD_ISSET(STDIN_FILENO, &check_set), FD_ISSET(sd_ds, &check_set), FD_ISSET(sd_peer, &check_set), FD_ISSET(sd_neigh, &check_set),
				FD_ISSET(sd_left, &check_set), FD_ISSET(sd_right, &check_set));
		else
			printf("STDIN_FILENO: %d, sd_ds: %d, sd_peer: %d, sd_neigh: %d\n", FD_ISSET(STDIN_FILENO, &check_set), FD_ISSET(sd_ds, &check_set), FD_ISSET(sd_peer, &check_set), FD_ISSET(sd_neigh, &check_set));
		*/
		 
		if(FD_ISSET(STDIN_FILENO, &check_set)){
			fgets(command_buffer, BUFFER_SIZE, stdin);	
			for(i = 0, j = 0; command_buffer[i] != ' ' && command_buffer[i] != '\n'; i++, j++){
				command[j] = command_buffer[i];
			}
			command[j] = '\0';
			i = i + 1;
		
			if(strcmp(command, "start") == 0){
				for(j = 0; command_buffer[i] != ' '; i++, j++){
					addr_ds[j] = command_buffer[i];
				}
				addr_ds[j] = '\0';
				i = i + 1;
				
				for(j = 0; command_buffer[i] != '\n'; i++, j++){
					port_string[j] = command_buffer[i];
				}
				port_string[j] = '\0';
					
				memset(&srv_addr, 0, sizeof(srv_addr)); 
				srv_addr.sin_family = AF_INET;
				srv_addr.sin_port = htons(atoi(port_string));
				inet_pton(AF_INET, addr_ds, &srv_addr.sin_addr);
				
				FD_ZERO(&timeout_set);
				FD_SET(sd_peer, &timeout_set);
				
				timeout.tv_sec = 10;              
				timeout.tv_usec = 0;
						
				/*Tento di inviare la richiesta continuamente, finche non ricevo risposta*/
				do{
					check_set = timeout_set;
					timeout.tv_sec = 10;  
					message.type = HELLO;
					strncpy(message.address, my_data.ip_peer, strlen(my_data.ip_peer) + 1);
					message.port1 = my_data.port_peer;
					message.port2 = -1;
					message.num_peer = -1;
					
					sprintf(message_buffer, "%u %s %d %d %d", message.type, message.address, message.port1, message.port2, message.num_peer);
									
					ret = sendto(sd_peer, message_buffer, MESSAGE_BUFFER, 0, (struct sockaddr*)&srv_addr, sizeof(srv_addr));
					select(sd_peer + 1, &check_set, NULL, NULL, &timeout);
										
					if(!FD_ISSET(sd_peer, &check_set)){
						perror("Errore in fase di comunicazione con il client\n");
					}
				} while(!FD_ISSET(sd_peer, &check_set));
				printf("Inoltro al server la richiesta di collegarmi alla rete\n(%s)\n\n", message_buffer);

				addrlen = sizeof(srv_addr);
				do{
					ret = recvfrom(sd_peer, message_buffer, MESSAGE_BUFFER, 0, (struct sockaddr*)&srv_addr, (socklen_t *)&addrlen);
					if(ret < 0)
						sleep(POLLING_TIME);
				} while(ret < 0);

				sscanf(message_buffer, "%u %s %d %d %d", &message.type, message.address, &message.port1, &message.port2, &message.num_peer);
				neighbours[0] = message.port1; 
				neighbours[1] = message.port2;
				
				if(message.type == HELLO){
					printf("Il server mi ha mandato i vicini: %d %d\n(%s)\n\n", neighbours[0], neighbours[1], message_buffer);
				
					/* Istauro la connessione TCP con il DS */
					ret = connect(sd_ds, (struct sockaddr*)&srv_addr, sizeof(srv_addr));
					if(ret < 0){
						perror("Errore in fase di connessione: \n");
						exit(-1);
					}
					max_sd = (max_sd >= sd_ds ? max_sd : sd_ds);
					FD_SET(sd_ds, &complete_set);
					
					memset(&left_addr, 0, sizeof(left_addr));
					memset(&right_addr, 0, sizeof(right_addr));

					if(neighbours[1] != -1){  
						left_addr.sin_family = AF_INET;
						left_addr.sin_port = htons(neighbours[0]);
						inet_pton(AF_INET, "127.0.0.1", &left_addr.sin_addr);
						
						right_addr.sin_family = AF_INET;
						right_addr.sin_port = htons(neighbours[1]);
						inet_pton(AF_INET, "127.0.0.1", &right_addr.sin_addr);
						
						/*Connessione con il vicino destro*/
						ret = connect(sd_right, (struct sockaddr*)&right_addr, sizeof(right_addr));
						if(ret < 0){
							perror("Errore in fase di connessione con il vicino sinistro: \n");
							exit(-1);
						}
						flag_right = 1;
						max_sd = (max_sd >= sd_right ? max_sd : sd_right);
						FD_SET(sd_right, &complete_set);
					}
					continue;
				}
				else{
					printf("Errore nella comunicazione con il ds\n");
					return 1;
				}
			}

			if(strcmp(command, "add") == 0 && neighbours[0] > -2 && neighbours[1] > -2){
				i = read_type(i, command_buffer, entry_type);
				if(i == -1)
					continue;
				
				for(j = 0; command_buffer[i] != '\n'; i++, j++){
					number_string[j] = command_buffer[i];
				}
				number_string[j] = '\0';
				i = i + 1;
				
				rawtime = time(NULL);
				if (rawtime == -1) {
					perror("Imposibile eseguire la funzione time()");
					return 1;
				}
				pnt_tm = localtime(&rawtime);    
				if (pnt_tm == NULL) {
					perror("Impossibile ottenere il timestamp attuale");
					return 1;
				}

				if(pnt_tm->tm_hour < 18){
					sprintf(path, "./%02d_%02d_%d_%d.txt", pnt_tm->tm_mday, (pnt_tm->tm_mon + 1), (1900 + pnt_tm->tm_year), my_data.port_peer);
				}
				else{
					rawtime = rawtime + (24 - pnt_tm->tm_hour) * 60 * 60 + (60 - pnt_tm->tm_min) * 60 + (61 - pnt_tm->tm_sec);
					pnt_tm = localtime(&rawtime);
					printf("%d, %d\n", pnt_tm->tm_year, my_data.port_peer);
					sprintf(path, "./%02d_%02d_%d_%d.txt", pnt_tm->tm_mday, (pnt_tm->tm_mon + 1), (1900 + pnt_tm->tm_year), my_data.port_peer);
					rawtime = time(NULL);
					if (rawtime == -1) {
						perror("Imposibile eseguire la funzione time()");
						return 1;
					}
					pnt_tm = localtime(&rawtime);    
					if (pnt_tm == NULL) {
						perror("Impossibile ottenere il timestamp attuale");
						return 1;
					}
				}
				
				if(strcmp(entry_type, "nuovo caso") == 0 || strcmp(entry_type, "tampone") == 0){
					/*if(strcmp(first_date, "-") == 0){
						strncpy(first_date, (path + 2), 10);
						while((buffer_data = strchr(first_date, '_')) != NULL){
							first_date[strlen(first_date) - strlen(buffer_data)] = '-';
						}
					}*/ //COMMENTATO PER TESTARE L'APPLICAZIONE
					if(access(path, F_OK) == 0){
						to_read = fopen(path,"r");
						if(to_read == NULL){
							printf("Errore nell'apertura del file %s!\n", path);   
							continue;            
						}
								
						unsized_buffer = (char *) malloc(sizeof(char) * (2 * MAX_ENTRY_LEN));
						if(unsized_buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						temporary_buffer = (char *) malloc(sizeof(char) * MAX_ENTRY_LEN);
						if(temporary_buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						strcpy(unsized_buffer, "");
						j = 0;
						
						/*Controllo se le ultime 2 righe contengono gli aggregati se si li elimino perchè significa che sto riaccedendo ad un register che era gia stato creato e che sto inserendo nuove entries. L'eliminazione degli aggregati viene eseguita ricreando un nuovo file e riscrivendo le precedenti*/
						fseek(to_read, 0, SEEK_END);	
						app = ftell(to_read);
						fseek(to_read, 0, SEEK_SET);
						fclose(to_read);
						
						if(app >= (2*(MAX_ENTRY_LEN - 1))){
							rename(path, "temp.txt");
							to_read = fopen("temp.txt", "r");
							if(to_read == NULL){
								printf("Errore nell'apertura del file temporaneo!\n");   
								continue;            
							}
							new_file = fopen(path, "w");
							if(new_file == NULL){
								printf("Errore nell'apertura del file %s!\n", path);   
								continue;            
							}
							
							while(EOF != fscanf(to_read,"%s %d %c %c %d", entry.timestamp, &entry.port, &entry.type, &entry.symbol, &entry.quantity)){
								if(entry.symbol != 'T')
									fprintf(new_file, "%s %05d %c %c %05d\n", entry.timestamp, entry.port, entry.type, entry.symbol, entry.quantity);
							}
							
							fclose(new_file);
							fclose(to_read);
							remove("temp.txt");
							
						}
					}
					
					sprintf(entry.timestamp, "%02d-%02d-%d_%02d:%02d:%02d", pnt_tm->tm_mday, (pnt_tm->tm_mon + 1), (1900 + pnt_tm->tm_year), pnt_tm->tm_hour, pnt_tm->tm_min, pnt_tm->tm_sec);
					entry.port = my_data.port_peer;
					entry.type = entry_type[0];
					entry.type = toupper(entry.type);
					entry.symbol = '+';
					entry.quantity = atoi(number_string);
					
					file_ptr = fopen(path, "a+");
					if(file_ptr == NULL)
					{
						printf("Errore nell'apertura del file %s!\n", path);   
						exit(1);             
					}
					
					fprintf(file_ptr, "%s %05d %c %c %05d\n", entry.timestamp, entry.port, entry.type, entry.symbol, entry.quantity);
					fclose(file_ptr);
				}
				continue;
			}

			if(strcmp(command, "get") == 0 && neighbours[0] > -2 && neighbours[1] > -2){				
				for(j = 0; command_buffer[i] != ' '; i++, j++){
					aggr_type[j] = command_buffer[i];	
				}
				aggr_type[j] = '\0';
								
				if(strcmp(aggr_type, "totale") == 0){
					peer_request.srv_type = TOTALE;
				} 
				else{
					if(strcmp(aggr_type, "variazione") == 0){
						peer_request.srv_type = VARIAZIONE;
					}
					else{
						return 1;
					}
				}
				i = i + 1;
				
				peer_request.type = toupper(command_buffer[i]);
				i = read_type(i, command_buffer, entry_type);
				if(i == -1)
					continue;
									
				i = read_date(i, command_buffer,  peer_request.date1);
				if(i == -1){
					printf("Errore nell'inserimento della data di inizio periodo\n");
					continue;
				}
				
				i = read_date(i, command_buffer,  peer_request.date2);
				if(i == -1){
					printf("Errore nell'inserimento della data di fine periodo\n");
					continue;
				}
				
				today_time = time(NULL);
				tmp_today = localtime(&today_time);
				sprintf(today_str, "%02d-%02d-%d", tmp_today->tm_mday, tmp_today->tm_mon + 1, tmp_today->tm_year + 1900);
				if(strcmp(peer_request.date2, "*") == 0) 
					sprintf(peer_request.date2, "%02d-%02d-%d", tmp_today->tm_mday, tmp_today->tm_mon + 1, tmp_today->tm_year + 1900);
				if(strcmp(today_str, peer_request.date2) == 0 && tmp_today->tm_hour < 18){
					/*Se non sono passate le 18 non posso usare il registrer quindi conto il giorno precedente*/
					today_time = today_time - (24 * 60 * 60);
					tmp_today = localtime(&today_time);
					sprintf(today_str, "%02d-%02d-%d", tmp_today->tm_mday, tmp_today->tm_mon + 1, tmp_today->tm_year + 1900);
				}
								
				strptime(peer_request.date1, "%d-%m-%Y", &tm_date1);
				tm_date1.tm_hour = 0;
				tm_date1.tm_min = 0;
				tm_date1.tm_sec = 0;
				tm_date1.tm_isdst = -1;
				strptime(peer_request.date2, "%d-%m-%Y", &tm_date2);
				tm_date2.tm_hour = 0;
				tm_date2.tm_min = 0;
				tm_date2.tm_sec = 0;
				tm_date2.tm_isdst = -1;

				if(difftime(mktime(&tm_date2), mktime(&tm_date1)) < 0)
					continue;

				buffer_data = NULL;
				if(result_list != NULL && strcmp(peer_request.date2, "*") != 0)
					search_result(&result_list, peer_request.srv_type, peer_request.type, peer_request.date1, peer_request.date2, &buffer_data);

				/*Il peer possiede gia il risultato quindi lo stampa e passa a servire la prossima richiesta*/
				if(buffer_data != NULL){
					printf("Risultato ricerca: %s\n", buffer_data);
					free(buffer_data);
					continue;
				}
				
				peer_request.dir = '-';
				peer_request.requester_port = my_data.port_peer;
				peer_request.size = 1;
				peer_request.ttl = -1;
				
				peer_request.buffer = (char *)malloc(sizeof(char) * 2);
				if(peer_request.buffer == NULL){
					printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
					return 0;
				}
				
				/*Il campo dir puo valere: u (il peer ha un unico vicino presente sulla rete), l (il messaggio sta viaggiando verso sinistra), r (il messaggio sta viaggiando verso destra). Se il campo vale u le operazioni verranno svolte comunicando con il solo vicino presente*/
				if(left_addr.sin_port == right_addr.sin_port)
					peer_request.dir = 'u';
				else
					peer_request.dir = '-';
				strcpy(peer_request.buffer, "-");

				ret = -1;
				count = 0;
				
				do{ 
					peer_request.msg_type = REQ_DATA;
					ret = -1;
					do {
						if(count == 1){
							sprintf(message_buffer, "%u %u %c %s %s %c %d %d %d %s", peer_request.msg_type, peer_request.srv_type, peer_request.type, 
								peer_request.date1, peer_request.date2, peer_request.dir, peer_request.requester_port, peer_request.size, peer_request.ttl, peer_request.buffer);
							ret = sendto(sd_peer, message_buffer, sizeof(message_buffer), 0, (struct sockaddr*)&right_addr, sizeof(right_addr));
							printf("Inoltro al vicino di destra la richiesta del dato aggregato\n(%s)\n\n", message_buffer);
						}else{
							if(count == 0 || peer_request.dir == 'u'){
								sprintf(message_buffer, "%u %u %c %s %s %c %d %d %d %s", peer_request.msg_type, peer_request.srv_type, peer_request.type, 
									peer_request.date1, peer_request.date2, peer_request.dir, peer_request.requester_port, peer_request.size, peer_request.ttl, peer_request.buffer);
								ret = sendto(sd_peer, message_buffer, sizeof(message_buffer), 0, (struct sockaddr*)&left_addr, sizeof(left_addr));
								printf("Inoltro al vicino di sinistra la richiesta del dato aggregato\n(%s)\n\n", message_buffer);
							}else
								break;
						}
						if(ret < 0) 
							sleep(POLLING_TIME);
					} while(ret < 0);
					
					if(ret < 0)
						break;
					
					ret = -1;
					/*Il vicino manda la dimensione totale che ha il messaggio che mi deve mandare*/
					if(count == 1 && flag_right == 1){
						ret = recv(sd_right, (void *)message_received, MESSAGE_BUFFER, 0);
						if(ret < 0){
							perror("Errore in fase di ricezione dal vicino destro: \n");
							exit(-1);
						}
					}
					else{
						if((count == 0 || peer_request.dir == 'u') && flag_left == 1){
							ret = recv(sd_left, (void *)message_received, MESSAGE_BUFFER, 0);
							if(ret < 0){
								perror("Errore in fase di ricezione dal vicino sinistro: \n");
								exit(-1);
							}
						}
						else 
							break;
					}
						
					free(peer_request.buffer);
					
					sscanf(message_received, "%u %u %c %s %s %c %d %d %d", &peer_request.msg_type, &peer_request.srv_type, &peer_request.type, 
						peer_request.date1, peer_request.date2, &peer_request.dir, &peer_request.requester_port, &peer_request.size, &peer_request.ttl);
					
					peer_request.buffer	= (char *)malloc(sizeof(char) * (peer_request.size + 1));
					if(peer_request.buffer == NULL){
						printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
						return 0;
					}
					
					sscanf(message_received, "%u %u %c %s %s %c %d %d %d %s", &peer_request.msg_type, &peer_request.srv_type, &peer_request.type, 
						peer_request.date1, peer_request.date2, &peer_request.dir, &peer_request.requester_port, &peer_request.size, &peer_request.ttl, peer_request.buffer);
					
					if(strcmp(peer_request.buffer, "-") == 0)
						printf("Risponde che non ha il dato aggregato\n(%s)\n\n", message_received);
					
					if(peer_request.dir == 'u' && (peer_request.msg_type != REPLY_DATA || strcmp(peer_request.buffer, "-") == 0)){
						break;
					}
					
					if(peer_request.msg_type != REPLY_DATA || strcmp(peer_request.buffer, "-") == 0){
						count = count + 1;
						continue;
					}

					/*Il viciono contattato ha il dato gia calcolato*/					
					unsized_buffer = (char *)malloc(sizeof(char) * (atoi(peer_request.buffer) + 1));
					if(unsized_buffer == NULL){
						printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
						return 0;
					}

					if(count == 1 && flag_right == 1){
						ret = recv(sd_right, (void *)unsized_buffer, (atoi(peer_request.buffer) + 1), 0);
						if(ret < 0){
							perror("Errore in fase di ricezione dal vicino destro: \n");
							exit(-1);
						}
					}
					else{
						if(count == 0 && flag_left == 1){
							ret = recv(sd_left, (void *)unsized_buffer, (atoi(peer_request.buffer) + 1), 0);
							if(ret < 0){
								perror("Errore in fase di ricezione dal vicino sinistro: \n");
								exit(-1);
							}
						}
						else 
							break;
					}
					if(ret < 0){	
						perror("Errore in fase di ricezione dal vicino: \n"); 
						return 1;
					}
										
					free(peer_request.buffer);
					printf("Mi manda il dato aggregato effettivo\n(%s)\n\n", unsized_buffer);
					sscanf(unsized_buffer, "%u %u %c %s %s %c %d %d %d", &peer_request.msg_type, &peer_request.srv_type, &peer_request.type, 
						peer_request.date1, peer_request.date2, &peer_request.dir, &peer_request.requester_port, &peer_request.size, &peer_request.ttl);
					
					peer_request.buffer	= (char *)malloc(sizeof(char) * (peer_request.size + 1));
					if(peer_request.buffer == NULL){
						printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
						return 0;
					}

					sscanf(unsized_buffer, "%u %u %c %s %s %c %d %d %d %s", &peer_request.msg_type, &peer_request.srv_type, &peer_request.type,
						peer_request.date1, peer_request.date2, &peer_request.dir, &peer_request.requester_port, &peer_request.size, &peer_request.ttl, peer_request.buffer);

					free(unsized_buffer);
						
					/*Salvo il dato aggregato*/
					insert_head_data(&result_list, peer_request.srv_type, peer_request.type, peer_request.date1, peer_request.date2, peer_request.buffer);
					
					printf("Risultato ricerca: %s\n", peer_request.buffer);
					break;
				} while(count != 2);
				
				/*I vicini di dx e sx avevano il dato aggregato*/
				if(strcmp(peer_request.buffer, "-") != 0){
					continue;
				}
				
				/*Chiedo al DS quanti peer sono presenti sulla rete*/
				message.type = PEER_NUMBER;
				strcpy(message.address, my_data.ip_peer);
				message.port1 = my_data.port_peer;
				message.port2 = -1;
				message.num_peer = -1;
				
				sprintf(message_buffer, "%u %s %d %d %d", message.type, message.address, message.port1, message.port2, message.num_peer);
			
				ret = send(sd_ds, (void *)message_buffer, strlen(message_buffer) + 1, 0);
				if(ret < 0){
					perror("Errore in fase di invio comando PEER_NUMBER: \n");
					return 1; 
				}
				printf("Chido al DS il numero peer connessi alla rete\n(%s)\n\n", message_buffer);
				
				ret = recv(sd_ds, (void *)message_buffer, MESSAGE_BUFFER, 0);
				if(ret < 0){	
					perror("Errore in fase di ricezione dal ds: \n"); 
					exit(-1);
				}
				
				sscanf(message_buffer, "%u %s %d %d %d", &message.type, message.address, &message.port1, &message.port2, &message.num_peer);

				printf("Il DS mi ha mandato il numero peer: %d\n(%s)\n\n", message.num_peer, message_buffer);
				
				/*Inoltro la richiesta dei dati che mi sono necessari per calcolare il dato aggregato*/
				if(message.type == PEER_NUMBER && message.num_peer > 1){
					printf("Inizio il floading\n");
					peer_request.msg_type = FLOOD_FOR_ENTRIES;
					peer_count =  (message.num_peer - 1) / 2.0;
					peer_request.ttl = ((int)peer_count == peer_count) ? peer_count : peer_count + 1;
					peer_request.size = 1;
					peer_request.buffer = (char *)malloc(sizeof(char) * 2);
					if(peer_request.buffer == NULL){
						printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
						return 0;
					}
					
					strncpy(peer_request.buffer, "-", 2);
					peer_request.requester_port = my_data.port_peer;
					if(peer_request.dir != 'u')
						peer_request.dir = 'l';
					
					sprintf(message_buffer, "%u %u %c %s %s %c %d %d %d %s", peer_request.msg_type, peer_request.srv_type, peer_request.type, 
						peer_request.date1, peer_request.date2, peer_request.dir, peer_request.requester_port, peer_request.size, peer_request.ttl, peer_request.buffer);
					
					if(peer_request.ttl != 0){
						do {
							ret = sendto(sd_peer, message_buffer, MESSAGE_BUFFER, 0, (struct sockaddr*)&left_addr, sizeof(left_addr));
							if(ret < 0)
								sleep(POLLING_TIME);
						} while(ret < 0);
						printf("Mando richiesta di entry al mio vicino sinistro\n(%s)\n\n", message_buffer);
					}
					peer_request.ttl = ((int)peer_count == peer_count) ? peer_count : (int)peer_count;
					peer_request.dir = 'r';
					
					sprintf(message_buffer, "%u %u %c %s %s %c %d %d %d %s", peer_request.msg_type, peer_request.srv_type, peer_request.type, 
						peer_request.date1, peer_request.date2, peer_request.dir, peer_request.requester_port, peer_request.size, peer_request.ttl, peer_request.buffer);
					
					if(peer_request.ttl != 0){
						do {
							ret = sendto(sd_peer, message_buffer, MESSAGE_BUFFER, 0, (struct sockaddr*)&right_addr, sizeof(right_addr));
							if(ret < 0)
								sleep(POLLING_TIME);
						} while(ret < 0);
						printf("Mando richiesta di entry al mio vicino destro\n(%s)\n\n", message_buffer);
					}
				}
				else{
					/*Sono l'unico collegato in rete e quindi l'elaborazione dei dati viene fatta con i soli dati che posseggo quindi non dovrò iinoltrare richieste a nessuno*/
					return_buffer = NULL;
					if(strcmp(peer_request.date1, "*") != 0)
						return_buffer = search_entries(peer_request.date1, peer_request.date2, &entries_peer, my_data.port_peer, peer_request.type, peer_request.requester_port, first_date);
					else
						return_buffer = search_entries(first_date, peer_request.date2, &entries_peer, my_data.port_peer, peer_request.type, peer_request.requester_port, first_date);
					if(return_buffer != NULL)
						insert_head_entries(&list_of_entries, return_buffer, peer_request.requester_port);
					previous_part = complete_list(&list_of_entries, peer_request.requester_port);
					if(peer_request.srv_type == TOTALE){
						total = calculate_total(NULL, NULL, previous_part, peer_request.date1, peer_request.date2);
						printf("Risultato ricerca totale: %d\n", total);
						sprintf(number_string, "%d", total);
						insert_head_data(&result_list, peer_request.srv_type, peer_request.type, peer_request.date1, peer_request.date2, number_string);
					}
						
					if(peer_request.srv_type == VARIAZIONE){
						temporary_buffer = calculate_variation(NULL, NULL, previous_part, peer_request.date1, peer_request.date2, peer_request.type);
						printf("Risultato aggregazione: %s\n", temporary_buffer);
						insert_head_data(&result_list, peer_request.srv_type, peer_request.type, peer_request.date1, peer_request.date2, temporary_buffer);
					}
				}
				continue;
			}
			
			if(strcmp(command, "stop") == 0){
				if(flag_left == 1 && flag_right == 1){
					if(strcmp(first_date, "-") != 0){
						strptime(first_date, "%d-%m-%Y", &tm_date1);
						date2_seconds = time(NULL);
						if (date2_seconds == -1) {
							perror("Imposibile eseguire la funzione time()");
							return 1;
						}
						pnt_date2 = localtime(&date2_seconds);    
						if (pnt_date2 == NULL) {
							perror("Impossibile ottenere il timestamp attuale");
							return 1;
						}
						sprintf(today_str, "%d-%d-%d", pnt_date2->tm_mday, pnt_date2->tm_mon + 1, pnt_date2->tm_year + 1900);
						sprintf(now, "%02d-%02d-%d_%02d:%02d:%02d", pnt_date2->tm_mday, pnt_date2->tm_mon + 1, pnt_date2->tm_year + 1900, pnt_date2->tm_hour, pnt_date2->tm_min, pnt_date2->tm_sec);
								
						tm_date1.tm_hour = 0;
						tm_date1.tm_min = 0;
						tm_date1.tm_sec = 0;
						tm_date1.tm_isdst = -1;
						date_seconds = mktime(&tm_date1);
						next_date = localtime(&date_seconds);
						buffer_data = (char *) malloc(sizeof(char) * ((2 * MAX_ENTRY_LEN) + 1));
						if(buffer_data == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						strcpy(buffer_data, "-");
						
						do{
							tm_date1.tm_mday = next_date->tm_mday;
							tm_date1.tm_mon = next_date->tm_mon;
							tm_date1.tm_year = next_date->tm_year;
							tm_date1.tm_hour = 0;
							tm_date1.tm_min = 0;
							tm_date1.tm_sec = 0;
							tm_date1.tm_isdst = -1;
							sprintf(searched_path, "./%02d_%02d_%d_%d.txt", next_date->tm_mday, (next_date->tm_mon + 1), (1900 + next_date->tm_year), my_data.port_peer);
										
							if(access(searched_path, F_OK ) == 0) {
								to_read = fopen(searched_path,"r");
								if(new_file == NULL){
									printf("Errore nell'apertura del file %s!\n", searched_path);   
									continue;            
								}
								
								unsized_buffer = (char *) malloc(sizeof(char) * (2 * MAX_ENTRY_LEN));
								if(unsized_buffer == NULL){
									printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
									return 0;
								}
								temporary_buffer = (char *) malloc(sizeof(char) * MAX_ENTRY_LEN);
								if(temporary_buffer == NULL){
									printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
									return 0;
								}
								strcpy(unsized_buffer, "");
								j = 0;
								/*Controllo se le ultime 2 righe contengono gli aggregati se si prendo quelli che sono disponibili, se no me li calcolo e li salvo*/
								fseek(to_read, -(2*(MAX_ENTRY_LEN - 1)), SEEK_END);
											
								while(EOF != fscanf(to_read,"%s %d %c %c %d", entry.timestamp, &entry.port, &entry.type, &entry.symbol, &entry.quantity)){
									if(entry.symbol == 'T'){
										sprintf(temporary_buffer, "%02d-%02d-%d|%d|%d|%c", next_date->tm_mday, (next_date->tm_mon + 1), (1900 + next_date->tm_year), entry.port, entry.quantity, entry.type);
										if(strcmp(unsized_buffer, "") == 0){
											strcpy(unsized_buffer, temporary_buffer);
											i = strlen(unsized_buffer);
											unsized_buffer[i] = ',';
											unsized_buffer[i + 1] = '\0';
										}
										else
											strcat(unsized_buffer, temporary_buffer);
										j = j + 1;
										type_found = entry.type;
									}
								}

								if(j != 2){
									/*Non erano presenti entrambi i dati aggregati*/
									fseek(to_read, 0, SEEK_SET);
									total = 0;
									counter = 0;
									while(EOF != fscanf(to_read,"%s %d %c %c %d", entry.timestamp, &entry.port, &entry.type, &entry.symbol, &entry.quantity)){
										if(type_found != '-' && entry.type != type_found){
											total = total + entry.quantity;
										}
										else{
											if(entry.type == 'T' && type_found == '-')
												total = total + entry.quantity;
												
											if(entry.type == 'N' && type_found == '-')
												counter = counter + entry.quantity;
										}
									}

									fclose(to_read);
									
									for(i = 0; i < 2; i++){
										sprintf(entry.timestamp, "%02d-%02d-%d_%s", next_date->tm_mday, (next_date->tm_mon + 1), (1900 + next_date->tm_year), (now + 11));
										entry.port = my_data.port_peer;
										if(type_found == 'T')
											entry.type = 'N';
										
										if(type_found == 'N')
											entry.type = 'T';
											
										if(type_found == '-' && i == 1)
											entry.type = 'T';	
										
										if((type_found != '-') || (type_found == '-' && i == 1))
											entry.quantity = total;
											
										if(type_found == '-' && i == 0){
											entry.type = 'N';
											entry.quantity = counter;
										}											
										
										entry.symbol = 'T';

										file_ptr = fopen(searched_path, "a");
										if(file_ptr == NULL){
											printf("Errore nell'apertura del file %s!\n", searched_path);   
											continue;             
										}
							
										fprintf(file_ptr, "%s %05d %c %c %05d\n", entry.timestamp, entry.port, entry.type, entry.symbol, entry.quantity);
										fclose(file_ptr);
											
										sprintf(temporary_buffer, "%02d-%02d-%d|%d|%d|%c", next_date->tm_mday, (next_date->tm_mon + 1), (1900 + next_date->tm_year), entry.port, entry.quantity, entry.type);
										if(strcmp(unsized_buffer, "") == 0){
											strcpy(unsized_buffer, temporary_buffer);
											j = strlen(unsized_buffer);
											unsized_buffer[j] = ',';
											unsized_buffer[j + 1] = '\0';
										}
										else
											strcat(unsized_buffer, temporary_buffer);
										
										if(type_found != '-')
											break;
									}
								} else{
									fclose(to_read);
								}
								if(strcmp(unsized_buffer, "") != 0){
									buffer_data = (char *) realloc(buffer_data, (2 * MAX_ENTRY_LEN + strlen(buffer_data) + 1));
									if(strcmp(buffer_data, "-") == 0)
										strcpy(buffer_data, unsized_buffer);
									else
										strcat(buffer_data, unsized_buffer);
									i = strlen(buffer_data);
									buffer_data[i] = ';';
									buffer_data[i + 1] = '\0';
								}
								
								free(unsized_buffer);
								free(temporary_buffer);
							}
							
							sprintf(analized_date, "%d-%d-%d", tm_date1.tm_mday, tm_date1.tm_mon + 1, tm_date1.tm_year + 1900);
							date_seconds = mktime(&tm_date1) + (24 * 60 * 60);
							if(strcmp(today_str, analized_date) != 0)
								next_date = localtime(&date_seconds);
							
						} while(strcmp(today_str, analized_date) != 0);

						temporary_buffer = (char *) malloc(sizeof(char) * MAX_ENTRY_LEN);
						if(temporary_buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						while(entries_peer != NULL){	
							garbage_peer = entries_peer;
							entries_peer = entries_peer->next;
							sprintf(temporary_buffer, "%s|%d|%d|%c", garbage_peer->date, garbage_peer->sender_port, garbage_peer->total, garbage_peer->type);
							if(strcmp(buffer_data, "") == 0){
								strcpy(buffer_data, temporary_buffer);
								j = strlen(buffer_data);
								buffer_data[j] = ',';
								buffer_data[j + 1] = '\0';
							}
							else
								strcat(buffer_data, temporary_buffer);
						}
						free(temporary_buffer);
						
						/*Se ho delle entry da mandare al mio vicino gliele mando*/
						if(strcmp(buffer_data, "-") != 0){
							printf("Mando le entry al/ai mio/ei vicino/i\n");
							strcpy(peer_request.date1, first_date);
							temporary_buffer = (char *) malloc(sizeof(char) * DATE_SIZE);
							if(temporary_buffer == NULL){
								printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
								return 0;
							}
							sprintf(temporary_buffer, "%02d-%02d-%d", pnt_date2->tm_mday, pnt_date2->tm_mon + 1, pnt_date2->tm_year + 1900);
							strcpy(peer_request.date2, temporary_buffer);
							free(temporary_buffer);
							if(left_addr.sin_port != right_addr.sin_port)
								peer_request.dir = '-';
							else
								peer_request.dir = 'u';
							peer_request.msg_type = STOP;
							peer_request.requester_port = my_data.port_peer;
							sprintf(number_string, "%d", (int)(42 + strlen(buffer_data)));
							peer_request.size = strlen(number_string);
							peer_request.srv_type = NULLO;
							peer_request.ttl = -1;
							peer_request.type = '-';
							peer_request.buffer = (char *) malloc(sizeof(char) * (strlen(buffer_data) + 1));
							if(peer_request.buffer == NULL){
								printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
								return 0;
							}
							strcpy(peer_request.buffer, number_string);
							
							sprintf(message_buffer, "%u %u %c %s %s %c %d %d %d %s", peer_request.msg_type, peer_request.srv_type, peer_request.type, 
								peer_request.date1, peer_request.date2, peer_request.dir, peer_request.requester_port, peer_request.size, peer_request.ttl, peer_request.buffer);
							if(peer_request.dir == '-'){
								ret = send(sd_left, (void*) message_buffer, MESSAGE_BUFFER, 0);
								if(ret < 0){
									perror("Errore in fase di comunicazione con il vicino: \n");
								}
							}
							
							ret = send(sd_right, (void*) message_buffer, MESSAGE_BUFFER, 0);
							if(ret < 0){
								perror("Errore in fase di comunicazione con il vicino: \n");
							}
		 
							free(peer_request.buffer);
							peer_request.buffer = (char *) malloc(sizeof(char) * (strlen(buffer_data) + 1));
							if(peer_request.buffer == NULL){
								printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
								return 0;
							}
							strcpy(peer_request.buffer, buffer_data);				
							peer_request.size = strlen(buffer_data);
							unsized_buffer = (char *) malloc(sizeof(char) * (int)(42 + strlen(buffer_data) + 1));
							if(unsized_buffer == NULL){
								printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
								return 0;
							}
							sprintf(unsized_buffer, "%u %u %c %s %s %c %05d %d %d %s", peer_request.msg_type, peer_request.srv_type, peer_request.type, 
								peer_request.date1, peer_request.date2, peer_request.dir, peer_request.requester_port, peer_request.size, peer_request.ttl, peer_request.buffer);
							
							if(peer_request.dir == '-'){
								ret = send(sd_left, (void*) unsized_buffer, strlen(unsized_buffer) + 1, 0);
								if(ret < 0){
									perror("Errore in fase di comunicazione con il vicino: \n");
								}
							}
					
							ret = send(sd_right, (void*) unsized_buffer, strlen(unsized_buffer) + 1, 0);
							if(ret < 0){
								perror("Errore in fase di comunicazione con il vicino: \n");
							}
						
						}
					}
					
					/*Comunico al mio vicino che me ne sto andando e le porte dei miei vicini per far si che possano ricostruire l'anello*/
					printf("Comunico al/ai mio/ei vicino/i che sto abbandonando la rete\n");
					strcpy(peer_request.date1, "-");
					strcpy(peer_request.date2, "-");
					peer_request.dir = '-';
					peer_request.msg_type = STOP;
					peer_request.requester_port = my_data.port_peer;
					peer_request.srv_type = NULLO;
					peer_request.ttl = -1;
					peer_request.type = '-';
					
					if(right_addr.sin_port != left_addr.sin_port){
						sprintf(number_string, "%d", ntohs(left_addr.sin_port));
						peer_request.size = strlen(number_string);
						peer_request.buffer = (char *) malloc(sizeof(char) * (strlen(number_string) + 1));
						if(peer_request.buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						strcpy(peer_request.buffer, number_string);
						sprintf(message_buffer, "%u %u %c %s %s %c %d %d %d %s", peer_request.msg_type, peer_request.srv_type, peer_request.type, 
							peer_request.date1, peer_request.date2, peer_request.dir, peer_request.requester_port, peer_request.size, peer_request.ttl, peer_request.buffer);
						ret = send(sd_right, (void*) message_buffer, MESSAGE_BUFFER, 0);
						if(ret < 0){
							perror("Errore in fase di comunicazione con il vicino: \n");
						}
						free(peer_request.buffer);
						sleep(1);
					}
					
					if(right_addr.sin_port == left_addr.sin_port)
						sprintf(number_string, "%d", -1);
					else
						sprintf(number_string, "%d", ntohs(right_addr.sin_port));
					peer_request.size = strlen(number_string);
					peer_request.buffer = (char *) malloc(sizeof(char) * (strlen(number_string) + 1));
					if(peer_request.buffer == NULL){
						printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
						return 0;
					}
					strcpy(peer_request.buffer, number_string);
					
					sprintf(message_buffer, "%u %u %c %s %s %c %d %d %d %s", peer_request.msg_type, peer_request.srv_type, peer_request.type, 
						peer_request.date1, peer_request.date2, peer_request.dir, peer_request.requester_port, peer_request.size, peer_request.ttl, peer_request.buffer);
					
					if(strcmp(peer_request.buffer, "-1") != 0)
						ret = send(sd_left, (void*) message_buffer, MESSAGE_BUFFER, 0);
					else
						ret = send(sd_right, (void*) message_buffer, MESSAGE_BUFFER, 0);
					if(ret < 0){
						perror("Errore in fase di comunicazione con il vicino: \n");
					}
					
					free(peer_request.buffer);
				}
				
				/*Comunico al DS che sto abbandonando la rete*/
				if(neighbours[0] != -2 && neighbours[1] != -2){
					printf("Comunico al server che sto abbandonando la rete\n");
					message.type = STOP;
					strcpy(message.address, my_data.ip_peer);
					message.port1 = my_data.port_peer;
					message.port2 = -1;
					message.num_peer = -1;
					sprintf(message_buffer, "%u %s %d %d %d", message.type, message.address, message.port1, message.port2, message.num_peer);
										
					FD_CLR(sd_ds, &complete_set);
					ret = send(sd_ds, (void *)message_buffer, MESSAGE_BUFFER, 0);
					if(ret < 0){
						perror("Errore in fase di invio comando STOP: \n");
						return 1; 
					}
				}
				break;
			}
		}

		if(FD_ISSET(sd_peer, &check_set)){
			flag_all = 0;
			do{
				addrlen = sizeof(peer_addr);
				ret = recvfrom(sd_peer, message_buffer, MESSAGE_BUFFER, 0, (struct sockaddr*)&peer_addr, (socklen_t *)&addrlen);
				if(ret < 0) 
					sleep(POLLING_TIME);
			} while(ret < 0);
			sscanf(message_buffer, "%u %u %c %s %s %c %d %d %d", &peer_request.msg_type, &peer_request.srv_type, &peer_request.type, 
						peer_request.date1, peer_request.date2, &peer_request.dir, &peer_request.requester_port, &peer_request.size, &peer_request.ttl);
			
			peer_request.buffer = (char *) malloc(sizeof(char) * (peer_request.size + 1));
			if(peer_request.buffer == NULL){
				printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
				return 0;
			}
			sscanf(message_buffer, "%u %u %c %s %s %c %d %d %d %s", &peer_request.msg_type, &peer_request.srv_type, &peer_request.type, 
						peer_request.date1, peer_request.date2, &peer_request.dir, &peer_request.requester_port, &peer_request.size, &peer_request.ttl, peer_request.buffer);
			
			
			/*Aggregazione senza lowerbound -> ognuno come lower bound considerera la sua prima data di inserimento e in fase di aggregazione dei dati verra trovata la prima data assoluta di quel blocco di entries*/
			if(strcmp(peer_request.date1, "*") == 0)
				flag_all = 1;
			
			/*Il vicino sta cercando un'aggregazione se ce l'ho gli mando prima la dimensione e poi il dato altrimenti rispondo con il buffer vuoto (dimensione unitaria)*/
			if(peer_request.msg_type == REQ_DATA){
				printf("Un vicino mi ha richiesto un dato aggregato\n(%s)\n\n", message_buffer);
				result = NULL;
				if(flag_all == 0)
					search_result(&result_list, peer_request.srv_type, peer_request.type, peer_request.date1, peer_request.date2, &result);
				
				peer_request.msg_type = REPLY_DATA;
				if(result == NULL){
					sprintf(number_string, "%d", 1);
				}else{
					sprintf(number_string, "%d", (int)(43 + strlen(result)));
				}
				peer_request.size = strlen(number_string);
				peer_request.buffer = (char *) malloc(sizeof(char) * (peer_request.size + 1));
				if(peer_request.buffer == NULL){
					printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
					return 0;
				}
				if(peer_request.size != 1)
					strcpy(peer_request.buffer, number_string);
				else
					strcpy(peer_request.buffer, "-");
				quantity = atoi(number_string);
				
				sprintf(message_buffer, "%u %u %c %s %s %c %d %d %d %s", peer_request.msg_type, peer_request.srv_type, peer_request.type, 
					peer_request.date1, peer_request.date2, peer_request.dir, peer_request.requester_port, peer_request.size, peer_request.ttl, peer_request.buffer);

				ret = -1;
				if(peer_request.dir != 'u'){
					if(right_addr.sin_port == htons(peer_request.requester_port) && flag_right == 1){
						ret = send(sd_right, (void*) message_buffer, MESSAGE_BUFFER, 0);
					} else{
						if(left_addr.sin_port == htons(peer_request.requester_port) && flag_left == 1){
							ret = send(sd_left, (void*) message_buffer, MESSAGE_BUFFER, 0);
						}
						else 
							continue;
					}
				}
				else{
					if(right_addr.sin_port == htons(peer_request.requester_port) && flag_right == 1)
						ret = send(sd_right, (void*) message_buffer, MESSAGE_BUFFER, 0);
					else
						continue;
				}
				if(ret < 0){
					perror("Errore in fase di comunicazione con il vicino: \n");
				}
				printf("Rispondo al mio vicino con\n(%s)\n\n", message_buffer);
				
				free(peer_request.buffer);
				
				if(result != NULL){
					peer_request.buffer = (char *) malloc(sizeof(char) * (strlen(result) + 1));
					if(peer_request.buffer == NULL){
						printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
						return 0;
					}
					strncpy(peer_request.buffer, result, (strlen(result) + 1));
					free(result);
					peer_request.size = strlen(peer_request.buffer);

					unsized_buffer = (char *) malloc(sizeof(char) * (quantity + 1));
					if(unsized_buffer == NULL){
						printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
						return 0;
					}
					sprintf(unsized_buffer, "%u %u %c %s %s %c %05d %d %d %s", peer_request.msg_type, peer_request.srv_type, peer_request.type, 
						peer_request.date1, peer_request.date2, peer_request.dir, peer_request.requester_port, peer_request.size, peer_request.ttl, peer_request.buffer);
										
					if(peer_request.dir != 'u'){
						if(left_addr.sin_port == htons(peer_request.requester_port) && flag_left == 1){
							ret = send(sd_left, (void*) unsized_buffer, quantity + 1, 0);
						} else{
							if(right_addr.sin_port == htons(peer_request.requester_port) && flag_right == 1){
								ret = send(sd_right, (void*) unsized_buffer, quantity + 1, 0);
							}
							else 
								continue;
						}
					}else{
						if(right_addr.sin_port == htons(peer_request.requester_port) && flag_right == 1)
							ret = send(sd_right, (void*) unsized_buffer, quantity + 1, 0);
						else
							continue;
					}
					if(ret < 0){
						perror("Errore in fase di comunicazione con il vicino: \n");
					}
					printf("Mando al vicino le mie entry\n(%s)\n\n", unsized_buffer);
					free(peer_request.buffer);
					free(unsized_buffer);
				}
			}
			/*Passo il messaggio al mio vicino e preparo i dati che dovro mandare al ritorno con il messaggio REQ_ENTRIES*/
			if(peer_request.msg_type == FLOOD_FOR_ENTRIES){
				printf("Ho ricevuto un messaggio di floading\n(%s)\n\n", message_buffer);
				peer_request.ttl = peer_request.ttl - 1;
				sprintf(message_buffer, "%u %u %c %s %s %c %d %d %d %s", peer_request.msg_type, peer_request.srv_type, peer_request.type, 
						peer_request.date1, peer_request.date2, peer_request.dir, peer_request.requester_port, peer_request.size, peer_request.ttl, peer_request.buffer);
				
				if(peer_request.ttl > 0){
					if(peer_request.dir == 'l'){
						do {
							ret = sendto(sd_peer, message_buffer, MESSAGE_BUFFER, 0, (struct sockaddr*)&left_addr, sizeof(left_addr));
							if(ret < 0)
								sleep(POLLING_TIME);
						} while(ret < 0);
					} else{
						do {
							ret = sendto(sd_peer, message_buffer, MESSAGE_BUFFER, 0, (struct sockaddr*)&right_addr, sizeof(right_addr));
							if(ret < 0)
								sleep(POLLING_TIME);
						} while(ret < 0);
					}
					printf("Mando al vicino il messaggio di floading\n(%s)\n\n", message_buffer);
				}
				return_buffer = NULL;
				if(flag_all == 0)
					return_buffer = search_entries(peer_request.date1, peer_request.date2, &entries_peer, my_data.port_peer, peer_request.type, peer_request.requester_port, first_date);
				else
					return_buffer = search_entries(first_date, peer_request.date2, &entries_peer, my_data.port_peer, peer_request.type, peer_request.requester_port, first_date);
				if(return_buffer != NULL)
					insert_head_entries(&list_of_entries, return_buffer, peer_request.requester_port);
				
				/*Ho percorso mezzo anello (ttl impostato dal richiedente scaduto), inverto la direzione inserisco eventuali dati (mando prima la dimensione del prossimo pacchetto che riceverà e poi l'effettivo pacchetto con i data) e lo rimando indietro*/
				if(peer_request.ttl == 0){
					peer_request.msg_type = REQ_ENTRIES;
					peer_request.dir = (peer_request.dir == 'l' ? 'r' : 'l');
					if(list_of_entries != NULL){
						for(prec_entries = NULL, cur_entries = list_of_entries; cur_entries != NULL; cur_entries = cur_entries->next){
							if(cur_entries->port_requester == peer_request.requester_port){
								peer_request.buffer = (char *) malloc(sizeof(char) * (strlen(cur_entries->buffer) + 1));
								if(peer_request.buffer == NULL){
									printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
									return 0;
								}
								strcpy(peer_request.buffer, cur_entries->buffer);
								garbage_entries = cur_entries;
								if(prec_entries == NULL)
									list_of_entries = garbage_entries->next;
								else
									prec_entries->next = garbage_entries->next;
								free(garbage_entries);
								peer_request.size = strlen(peer_request.buffer);
								break;
							}
							prec_entries = cur_entries;
						}
					} else
						peer_request.size = 1;
					peer_request.ttl = -1;
					
					if(peer_request.size != 1){
						peer_request.size = strlen(peer_request.buffer);
						previous_part = (char *) malloc(sizeof(char) * (peer_request.size + 1));
						if(previous_part == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						count = peer_request.size;
						strncpy(previous_part, peer_request.buffer, (strlen(peer_request.buffer) + 1));
						free(peer_request.buffer);
						sprintf(number_string, "%d", (int)(42 + strlen(previous_part)));
						peer_request.size = strlen(number_string);
						peer_request.buffer = (char *) malloc(sizeof(char) * (peer_request.size + 1));
						if(peer_request.buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						strncpy(peer_request.buffer, number_string, (strlen(number_string) + 1));
					} else{
						peer_request.buffer = (char *) malloc(sizeof(char) * (peer_request.size + 1));
						if(peer_request.buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						strcpy(peer_request.buffer, "-");
					}
					
					sprintf(message_buffer, "%u %u %c %s %s %c %d %d %d %s", peer_request.msg_type, peer_request.srv_type, peer_request.type, 
							peer_request.date1, peer_request.date2, peer_request.dir, peer_request.requester_port, peer_request.size, peer_request.ttl, peer_request.buffer);
														
					if(peer_request.dir == 'l')
						ret = send(sd_left, (void *)message_buffer, MESSAGE_BUFFER, 0);
					else
						ret = send(sd_right, (void *)message_buffer, MESSAGE_BUFFER, 0);
					if(ret < 0){
						perror("Errore in fase di invio della dimensione del messaggio REQ_ENTRIES al vicino: \n");
						return 1; 
					}
					printf("Sono l'ultimo in questa direzione quindi rispondo con REQ_ENTRIES\n(%s)\n\n", message_buffer);
					
					if(peer_request.size != 1){
						free(peer_request.buffer);
						peer_request.size = count;
						peer_request.buffer = (char *) malloc(sizeof(char) * (peer_request.size + 1));
						if(peer_request.buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						strncpy(peer_request.buffer, previous_part, strlen(previous_part) + 1);
						free(previous_part);
						unsized_buffer = (char *) malloc(sizeof(char) * (atoi(number_string) + 1));
						if(unsized_buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						sprintf(unsized_buffer, "%u %u %c %s %s %c %05d %d %d %s", peer_request.msg_type, peer_request.srv_type, peer_request.type, 
							peer_request.date1, peer_request.date2, peer_request.dir, peer_request.requester_port, peer_request.size, peer_request.ttl, peer_request.buffer);
						
						if(peer_request.dir == 'l'){
							ret = send(sd_left, (void *)unsized_buffer, (atoi(number_string) + 1), 0);
						}
						else{
							ret = send(sd_right, (void *)unsized_buffer, (atoi(number_string) + 1), 0);
						}
						
						if(ret < 0){
							perror("Errore in fase di invio REQ_ENTRIES al vicino: \n");
							return 1; 
						}
						printf("Mando al vicino le mie entry\n(%s)\n\n", unsized_buffer);
						free(unsized_buffer);
						free(peer_request.buffer);
					}
				}
			}
			continue;
		}

		if(FD_ISSET(sd_neigh, &check_set)){
			/*Aggancio richiesto dal vicino dx al sinistro*/
			memset(&peer_addr, 0, sizeof(peer_addr));
			addrlen = sizeof(peer_addr);
	
			if(max_sd == sd_left){
				max_sd = STDIN_FILENO;
				max_sd = (max_sd >= sd_neigh ? max_sd : sd_neigh);
				max_sd = (max_sd >= sd_peer ? max_sd : sd_peer);
				max_sd = (max_sd >= sd_ds ? max_sd : sd_ds);
				max_sd = (max_sd >= sd_right ? max_sd : sd_right);
			}
			
			
			if(flag_left == 1){
				FD_CLR(sd_left, &complete_set);
				close(sd_left);
			}else
				flag_left = 1;
	
			sd_left = accept(sd_neigh, (struct sockaddr*) &peer_addr, (socklen_t *)&addrlen);
			if(sd_left < 0){
				perror("Errore in fase di connessione con neighbour: \n");
				return 1;
			}
			
			max_sd = (max_sd >= sd_left ? max_sd : sd_left);
			FD_SET(sd_left, &complete_set);	
			printf("Aggiornamento vicino sx: %d\n", ntohs(left_addr.sin_port));
			continue;
		}

		if(FD_ISSET(sd_ds, &check_set)){
			/*Il DS mi sta notificando che un peer (mio vicino) si è connesso e quindi devo aggiornare il mio 'vicinato'*/
			ret = recv(sd_ds, (void*)message_buffer, MESSAGE_BUFFER, 0);
			if(ret < 0){
				perror("Errore in fase di ricezione dal ds: \n");
				exit(-1);
			}
			sscanf(message_buffer, "%u %s %d %d %d", &message.type, message.address, &message.port1, &message.port2, &message.num_peer);
			
			if(message.type == NEW_NEIGHBOR){
				left_addr.sin_family = AF_INET;
				left_addr.sin_port = htons(message.port1);
				inet_pton(AF_INET, "127.0.0.1", &left_addr.sin_addr);
				
				if(right_addr.sin_port != htons(message.port2)){
					right_addr.sin_family = AF_INET;
					right_addr.sin_port = htons(message.port2);
					inet_pton(AF_INET, "127.0.0.1", &right_addr.sin_addr);
					
					if(max_sd == sd_right){
						max_sd = STDIN_FILENO;
						max_sd = (max_sd >= sd_neigh ? max_sd : sd_neigh);
						max_sd = (max_sd >= sd_peer ? max_sd : sd_peer);
						max_sd = (max_sd >= sd_ds ? max_sd : sd_ds);
						max_sd = (max_sd >= sd_left ? max_sd : sd_left);
					}
					
					if(flag_right == 1){
						FD_CLR(sd_right, &complete_set);
						close(sd_right);
						sd_right = socket(AF_INET, SOCK_STREAM, 0);
					} else{
						flag_right = 1;
					}
					
					ret = connect(sd_right, (struct sockaddr*)&right_addr, sizeof(right_addr));
					if(ret < 0){
						perror("Errore in fase di connessione con il vicino destro: \n");
						exit(-1);
					}
					
					max_sd = (max_sd >= sd_right ? max_sd : sd_right);
					FD_SET(sd_right, &complete_set);
					
				}
				
				printf("Aggiornamento vicini -> sx: %d, dx: %d\n", message.port1, message.port2);
				continue;
			}

			if(message.type == ESC){
				ret = send(sd_ds, (void*)message_buffer, MESSAGE_BUFFER, 0);
				if(ret < 0){
					perror("Errore in fase di comunicazione con il client: \n"); 
				}
				printf("Il server ha richiesto la chiusura dell'applicazione\n");
				break;
			}
		}
		
		if((flag_left == 1 && FD_ISSET(sd_left, &check_set)) || (flag_right == 1 && FD_ISSET(sd_right, &check_set))){
			flag_all = 0;
			if(FD_ISSET(sd_left, &check_set)){
				ret = recv(sd_left, (void*)message_buffer, MESSAGE_BUFFER, 0);
				if(ret < 0){
					perror("Errore in fase di ricezione dal vicino sinistro: \n");
					exit(-1);
				}
				left_right = 0;
			}
			else{
				ret = recv(sd_right, (void*)message_buffer, MESSAGE_BUFFER, 0);
				if(ret < 0){
					perror("Errore in fase di ricezione dal vicino destro: \n");
					exit(-1);
				}
				left_right = 1;
			}
			if(ret < 0){
				perror("Errore in fase di ricezione dal vicino: \n");
				exit(-1);
			}
			sscanf(message_buffer, "%u %u %c %s %s %c %d %d %d", &peer_request.msg_type, &peer_request.srv_type, &peer_request.type, 
				peer_request.date1, peer_request.date2, &peer_request.dir, &peer_request.requester_port, &peer_request.size, &peer_request.ttl);
				
			printf("Messaggio dal vicino\n(%s)\n\n", message_buffer);

			peer_request.buffer = (char *) malloc(sizeof(char) * (peer_request.size + 1));
			if(peer_request.buffer == NULL){
				printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
				return 0;
			}
			sscanf(message_buffer, "%u %u %c %s %s %c %d %d %d %s", &peer_request.msg_type, &peer_request.srv_type, &peer_request.type, 
				peer_request.date1, peer_request.date2, &peer_request.dir, &peer_request.requester_port, &peer_request.size, &peer_request.ttl, peer_request.buffer);
			
			if((peer_request.size != 1 && peer_request.msg_type != STOP) || (peer_request.msg_type == STOP && strcmp(peer_request.date1, "-") != 0 && strcmp(peer_request.date2, "-") != 0)){
				unsized_buffer = (char *) malloc(sizeof(char) * (atoi(peer_request.buffer) + 1));
				if(unsized_buffer == NULL){
					printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
					return 0;
				}
				count = atoi(peer_request.buffer);
				free(peer_request.buffer);
								
				if(left_right == 0){
					ret = recv(sd_left, (void*)unsized_buffer, (count + 1), 0);
					if(ret < 0){
						perror("Errore in fase di ricezione dal vicino sinistro: \n");
						exit(-1);
					}
				}
				else{
					ret = recv(sd_right, (void*)unsized_buffer, (count + 1), 0);
					if(ret < 0){
						perror("Errore in fase di ricezione dal vicino destro: \n");
						exit(-1);
					}
				}
				if(ret < 0){
					perror("Errore in fase di ricezione REQ_ENTRIES dal vicino: \n");
					exit(-1);
				}
				
				sscanf(unsized_buffer, "%u %u %c %s %s %c %d %d %d", &peer_request.msg_type, &peer_request.srv_type, &peer_request.type, 
					peer_request.date1, peer_request.date2, &peer_request.dir, &peer_request.requester_port, &peer_request.size, &peer_request.ttl);
					
				peer_request.buffer = (char *) malloc(sizeof(char) * (peer_request.size + 1));
				if(peer_request.buffer == NULL){
					printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
					return 0;
				}
				sscanf(unsized_buffer, "%u %u %c %s %s %c %d %d %d %s", &peer_request.msg_type, &peer_request.srv_type, &peer_request.type, 
					peer_request.date1, peer_request.date2, &peer_request.dir, &peer_request.requester_port, &peer_request.size, &peer_request.ttl, peer_request.buffer);
			
			} 
			
			if(strcmp(peer_request.date1, "*") == 0)
				flag_all = 1;
				
			/*Accodo le entry che conosco al messaggio che sta tornando verso il peer richiedente*/
			if(peer_request.msg_type == REQ_ENTRIES){
				if(unsized_buffer != NULL)
					printf("Devo accodare le entry che ho relative alla richiesta\n(%s)\n\n", unsized_buffer);
				if(peer_request.requester_port != my_data.port_peer){
					previous_part = NULL;
					if(peer_request.size != 1){
						previous_part = (char *) malloc(sizeof(char) * (peer_request.size + 1));
						if(previous_part == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						count = peer_request.size;
						strcpy(previous_part, peer_request.buffer);
					}
					free(peer_request.buffer);
					peer_request.buffer = NULL;
					if(list_of_entries != NULL){
						for(prec_entries = NULL, cur_entries = list_of_entries; cur_entries != NULL; cur_entries = cur_entries->next){
							if(cur_entries->port_requester == peer_request.requester_port){
								peer_request.buffer = (char *) malloc(sizeof(char) * (strlen(cur_entries->buffer) + 1));
								if(peer_request.buffer == NULL){
									printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
									return 0;
								}
								strcpy(peer_request.buffer, cur_entries->buffer);
								garbage_entries = cur_entries;
								if(prec_entries == NULL)
									list_of_entries = garbage_entries->next;
								else
									prec_entries->next = garbage_entries->next;
								free(garbage_entries);
								peer_request.size = strlen(peer_request.buffer);
								break;
							}
							prec_entries = cur_entries;
						}
					}else
						peer_request.size = 1;
						
					if(peer_request.size == 1 && previous_part == NULL){
						peer_request.buffer = (char *) malloc(sizeof(char) * (peer_request.size + 1));
						if(peer_request.buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						strcpy(peer_request.buffer, "-");
					}else{
						if(peer_request.size == 1 && previous_part != NULL){
							peer_request.size = count;
							peer_request.buffer = (char *) malloc(sizeof(char) * (peer_request.size + 1));
							if(peer_request.buffer == NULL){
								printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
								return 0;
							}
							strcpy(peer_request.buffer, previous_part);
						}
						else{
							if(peer_request.size != 1 && previous_part != NULL){
								unsized_buffer = (char *) malloc(sizeof(char) * (peer_request.size + 1));
								if(unsized_buffer == NULL){
									printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
									return 0;
								}
								strcpy(unsized_buffer, peer_request.buffer);
								peer_request.size = peer_request.size + count;
								peer_request.buffer = (char *) malloc(sizeof(char) * (peer_request.size + 1));
								if(peer_request.buffer == NULL){
									printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
									return 0;
								}
								strcpy(peer_request.buffer, previous_part);
								strcat(peer_request.buffer, unsized_buffer);
								free(unsized_buffer);
							}
						}	
					}
					
					if(previous_part != NULL)
						free(previous_part);
					if(peer_request.size != 1){
						peer_request.size = strlen(peer_request.buffer);
						previous_part = (char *) malloc(sizeof(char) * (peer_request.size + 1));
						if(previous_part == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						count = peer_request.size;
						strcpy(previous_part, peer_request.buffer);
						free(peer_request.buffer);
						sprintf(number_string, "%d", (int)(42 + strlen(previous_part)));
						peer_request.size = strlen(number_string);
						peer_request.buffer = (char *) malloc(sizeof(char) * (peer_request.size + 1));
						if(peer_request.buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						strcpy(peer_request.buffer, number_string);
					}
					sprintf(message_buffer, "%u %u %c %s %s %c %d %d %d %s", peer_request.msg_type, peer_request.srv_type, peer_request.type, 
						peer_request.date1, peer_request.date2, peer_request.dir, peer_request.requester_port, peer_request.size, peer_request.ttl, peer_request.buffer);
									
					if(peer_request.dir == 'l')
						ret = send(sd_left, (void *)message_buffer, MESSAGE_BUFFER, 0);
					else
						ret = send(sd_right, (void *)message_buffer, MESSAGE_BUFFER, 0);
					if(ret < 0){
						perror("Errore in fase di invio della dimensione del messaggio REQ_ENTRIES al vicino: \n");
						return 1; 
					}
					
					printf("Inoltro il messaggio\n(%s)\n\n", message_buffer);
					
					if(peer_request.size != 1){
						free(peer_request.buffer);
						peer_request.size = count;
						peer_request.buffer = (char *) malloc(sizeof(char) * (peer_request.size + 1));
						if(peer_request.buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						strcpy(peer_request.buffer, previous_part);
						free(previous_part);
						unsized_buffer = (char *) malloc(sizeof(char) * (atoi(number_string) + 1));
						if(unsized_buffer == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						sprintf(unsized_buffer, "%u %u %c %s %s %c %05d %d %d %s", peer_request.msg_type, peer_request.srv_type, peer_request.type, 
							peer_request.date1, peer_request.date2, peer_request.dir, peer_request.requester_port, peer_request.size, peer_request.ttl, peer_request.buffer);
						
						if(peer_request.dir == 'l'){
							ret = send(sd_left, (void *)unsized_buffer, (atoi(number_string) + 1), 0);
						}
						else{
							ret = send(sd_right, (void *)unsized_buffer, (atoi(number_string) + 1), 0);
							
						}
						if(ret < 0){
							perror("Errore in fase di invio REQ_ENTRIES al vicino: \n");
							return 1; 
						}
						
						printf("Mando il messaggio con le entry\n(%s)\n\n", unsized_buffer);
						
						free(unsized_buffer);
						free(peer_request.buffer);
					}
					
				}
				else{
					/*Sono il peer che ha richiesto l'aggregazione dei dati, attendo che le entry necessarie per calcolare il dato aggregato siano arrivate a destinazione lo calcolo e me lo salvo per future richieste*/
					if(peer_request.dir == 'r'){
						buffer_left = (char *) malloc(sizeof(char) * (peer_request.size + 1));
						if(buffer_left == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						strncpy(buffer_left, peer_request.buffer, strlen(peer_request.buffer) + 1);
					}
					if(peer_request.dir == 'l'){
						buffer_right = (char *) malloc(sizeof(char) * (peer_request.size + 1));
						if(buffer_right == NULL){
							printf("Memoria esaurita; non è possibile allocare spazione necessario\n");
							return 0;
						}
						strncpy(buffer_right, peer_request.buffer, strlen(peer_request.buffer) + 1);
					}
					
					if((buffer_right != NULL && buffer_left != NULL && left_addr.sin_port != right_addr.sin_port && flag_right == 1 && flag_left == 1)
							|| ((buffer_right != NULL || buffer_left != NULL) && left_addr.sin_port == right_addr.sin_port && flag_right == 1 && flag_left == 1)){
						counter = 0;
						previous_part = NULL;
						return_buffer = NULL;
						if(flag_all == 0)
							return_buffer = search_entries(peer_request.date1, peer_request.date2, &entries_peer, my_data.port_peer, peer_request.type, peer_request.requester_port, first_date);
						else
							return_buffer = search_entries(first_date, peer_request.date2, &entries_peer, my_data.port_peer, peer_request.type, peer_request.requester_port, first_date);
						if(return_buffer != NULL)
							insert_head_entries(&list_of_entries, return_buffer, peer_request.requester_port);
						previous_part = complete_list(&list_of_entries, peer_request.requester_port);
						
						if(peer_request.srv_type == TOTALE){
							total = calculate_total(buffer_left, buffer_right, previous_part, peer_request.date1, peer_request.date2);
							printf("Risultato ricerca totale: %d\n", total);
							sprintf(number_string, "%d", total);
							insert_head_data(&result_list, peer_request.srv_type, peer_request.type, peer_request.date1, peer_request.date2, number_string);
							free(buffer_right);
							free(buffer_left);
							free(previous_part);
							buffer_right = NULL;
							buffer_left = NULL;
							previous_part = NULL;
						}
						
						if(peer_request.srv_type == VARIAZIONE){
							temporary_buffer = calculate_variation(buffer_left, buffer_right, previous_part, peer_request.date1, peer_request.date2, peer_request.type);
							printf("Risultato aggregazione: %s\n", temporary_buffer);
							insert_head_data(&result_list, peer_request.srv_type, peer_request.type, peer_request.date1, peer_request.date2, temporary_buffer);
							free(previous_part);
							previous_part = NULL;
							free(buffer_left);
							buffer_left = NULL;
							free(buffer_right);
							buffer_right = NULL;
						}	
					}
					
				}
			}
			
			/*Il peer sta richiedendo la disconnessione dalla rete quindi se ha delle entries me le manda e le aggiungo a quelle 'conosciute ma non mie'*/
			if(peer_request.msg_type == STOP && strcmp(peer_request.date1, "-") != 0 && strcmp(peer_request.date2, "-") != 0){
				printf("Un mio vicino sta richiedendo la disconnessione dalla rete quindi mi manda le sue entry\n(%s)\n\n", unsized_buffer);
				token = strtok(peer_request.buffer, terminator);
				while(token != NULL) {
					result = strtok(token, coma_str);
					while(result != NULL){
						while((buffer_data = strchr(result, pipe)) != NULL){
							result[strlen(result) - strlen(buffer_data)] = ' ';
						}
						sscanf(result, "%s %d %d %c", day_entry.date, &day_entry.sender_port, &day_entry.total, &day_entry.type);
						if(day_entry.total != 0)
							insert_head_date(&entries_peer, day_entry.date, day_entry.sender_port, day_entry.total, day_entry.type);
						
						result = strtok(NULL, coma_str);
					}
					token = strtok(NULL, terminator);
				}
			}
			
			/*Aggiorno il 'vicinato' in modo che l'anello si ricomponga tramite le informazioni passatemi dal peer che sta abbandonado la rete*/
			if(peer_request.msg_type == STOP && strcmp(peer_request.date1, "-") == 0 && strcmp(peer_request.date2, "-") == 0){
				if(left_right == 1 && strcmp(peer_request.buffer, "-1") != 0){					
					right_addr.sin_family = AF_INET;
					right_addr.sin_port = htons(atoi(peer_request.buffer));
					inet_pton(AF_INET, "127.0.0.1", &right_addr.sin_addr);
					
					if(max_sd == sd_right){
						max_sd = STDIN_FILENO;
						max_sd = (max_sd >= sd_neigh ? max_sd : sd_neigh);
						max_sd = (max_sd >= sd_peer ? max_sd : sd_peer);
						max_sd = (max_sd >= sd_ds ? max_sd : sd_ds);
						max_sd = (max_sd >= sd_left ? max_sd : sd_left);
					}
					
					if(flag_right == 1){
						FD_CLR(sd_right, &complete_set);
						close(sd_right);
						sd_right = socket(AF_INET, SOCK_STREAM, 0);
					} else{
						flag_right = 1;
					}
					
					ret = connect(sd_right, (struct sockaddr*)&right_addr, sizeof(right_addr));
					if(ret < 0){
						perror("Errore in fase di connessione con il vicino destro: \n");
						exit(-1);
					}
				
					max_sd = (max_sd >= sd_right ? max_sd : sd_right);
					FD_SET(sd_right, &complete_set);
					
					printf("Aggiornamento vicino di dx: %d\n", atoi(peer_request.buffer));				
				}
				else{
					if(left_right == 0 && strcmp(peer_request.buffer, "-1") != 0){
						left_addr.sin_family = AF_INET;
						left_addr.sin_port = htons(atoi(peer_request.buffer));
						inet_pton(AF_INET, "127.0.0.1", &left_addr.sin_addr);
						//printf("Aggiornamento vicino sx: %s\n", peer_request.buffer);
					}
					else{
						/*Il peer è rimasto senza vicini*/
						FD_CLR(sd_right, &complete_set);
						close(sd_right);
						sd_right = socket(AF_INET, SOCK_STREAM, 0);
						FD_CLR(sd_left, &complete_set);
						close(sd_left);
						flag_left = 0; 
						flag_right = 0; 
					}
				}
			
			}
					
		}
		
	}
	
	close(sd_right);
	if(flag_left == 1)
		close(sd_left);
	close(sd_ds);
	close(sd_peer);
	close(sd_neigh);
	while(result_list != NULL){
		garbage_result = result_list;
		result_list = result_list->next;
		free(garbage_result);
	}
	
	return 0;
}