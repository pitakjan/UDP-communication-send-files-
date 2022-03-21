// UDP_Communication_Framework.cpp : Defines the entry point for the console application.
/* Author: Jan Pitak
 * Subject: KDS
 * Date: 3.1.2022
 */ 

//lot of useless includes
#include "stdafx.h"
#include <winsock2.h>
#include "ws2tcpip.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <iostream>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <windows.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <math.h>
#include <synchapi.h>
#include <stdlib.h>
#include <stdio.h>
#include <psapi.h>
#include <time.h>

#define START_BIT 1 // 8bit, 1 on MSB
#define BEETWEEN_BIT 2 // 8bit, 1 on MSB-1
#define END_BIT 4 // 8bit, 1 on MSB-2

//#define TARGET_IP	"127.0.0.1"
#define TARGET_IP	"192.168.30.16"

#define BUFFERS_LEN 1024

#define SENDER

#ifdef SENDER
//#define TARGET_PORT 15001
#define TARGET_PORT 14001

#define LOCAL_PORT 15115
//#define LOCAL_PORT 15005
#endif // SENDER


#define DELAY_BEETWEEN_PACKETS 0

#define TIMEOUT 5 // multiplied by UPDATE_PERIOD
#define UPDATE_PERIOD 1 // ms

#define MAX_PACKETS_ON_PATH 100 // maximum packets on path
#define PRINT false

const int number_data_bytes_in_packet = 1012; // 1012 max; 1012 + 12 head bytes = 1024 


const int number_head_bytes = 12;
const int total_bytes_in_packet = number_data_bytes_in_packet + number_head_bytes;
CRITICAL_SECTION cs;

volatile bool running = true;

// CRC______________________________CRC____________________________________________CRC______________
struct crc32
{
	static void generate_table(uint32_t(&table)[256])
	{
		uint32_t polynomial = 0xEDB88320;
		for (uint32_t i = 0; i < 256; i++)
		{
			uint32_t c = i;
			for (size_t j = 0; j < 8; j++)
			{
				if (c & 1) {
					c = polynomial ^ (c >> 1);
				}
				else {
					c >>= 1;
				}
			}
			table[i] = c;
		}
	}

	static uint32_t update(uint32_t(&table)[256], uint32_t initial, const void* buf, size_t len)
	{
		uint32_t c = initial ^ 0xFFFFFFFF;
		const uint8_t* u = static_cast<const uint8_t*>(buf);
		for (size_t i = 0; i < len; ++i)
		{
			c = table[(c ^ u[i]) & 0xFF] ^ (c >> 8);
		}
		return c ^ 0xFFFFFFFF;
	}
};

typedef struct {
	SOCKET socketS;
	struct sockaddr_in local;
	struct sockaddr_in from;
	int fromlen;

	sockaddr_in addrDest;
}Connection_Data;
Connection_Data connection_data;

// main struct for holding data
typedef struct {
	uint8_t RND = 0;
	uint32_t ID_accepted = UINT32_MAX;
	uint32_t ID_max = UINT32_MAX;
	uint32_t number_of_packets = UINT32_MAX;
	uint64_t file_size_in_Bytes = 1;
	uint32_t HASH;
}Data_Struct;
volatile Data_Struct Data;




// some Init
void InitWinsock()
{
	WSADATA wsaData;
	int control = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (control != 0)
		printf("ERROR: line 83");
}

void read_file(FILE* fileptr, uint64_t* file_len_p, uint32_t* number_of_packets_p)
{
	uint64_t file_len;
	uint32_t number_of_packets;
	fseek(fileptr, 0, SEEK_END); //set pointer to end of file
	file_len = ftell(fileptr);
	if (file_len > 4294967295)
		printf("ERROR: Cannot send bigger file than 4.29GB");

	rewind(fileptr); // set pointer back to start of the file

	Data.file_size_in_Bytes = file_len;
	number_of_packets = ceil((file_len) / (number_data_bytes_in_packet)) + 2; // round up + 1 start packet 
	Data.number_of_packets = number_of_packets;

	char* tmp = (char*)malloc(file_len * sizeof(char));
	fread(tmp, 1, file_len, fileptr);

	uint32_t table[256];
	crc32::generate_table(table);
	Data.HASH = crc32::update(table, 0, tmp, file_len);
	rewind(fileptr);

	uint32_t number_data_bytes_in_last_packet = (file_len) % (number_data_bytes_in_packet);
	if (number_data_bytes_in_last_packet == 0) {
		number_of_packets--;
	}

	EnterCriticalSection(&cs);

	Data.ID_max = number_of_packets - 1;

	LeaveCriticalSection(&cs);

	*file_len_p = file_len; //save variables
	*number_of_packets_p = number_of_packets;

	return;
}


void split_to_packets(FILE* fileptr, char** packets, uint32_t number_of_packets, char* file_name, int* total_bytes_in_first_packet_p, uint64_t file_size)
{
	srand(time(NULL)); //init random
	uint16_t RND_index = rand() % 254 + 1;
	printf("random index %lu\n", RND_index);
	EnterCriticalSection(&cs);
	Data.RND = RND_index;
	LeaveCriticalSection(&cs);
	uint32_t ID;
	uint32_t table[256];
	uint32_t crc;


	uint32_t number_data_bytes_in_last_packet = (file_size) % (number_data_bytes_in_packet);

	for (int i = 0; i < number_of_packets; i++) {
		// TODO first and last packet modify
		if (i == 0) {
			//					     	 num. packets, standard len of packets, hash, filename
			int total_bytes_in_first_packet = (number_head_bytes + 4 + 4 + 4 + 0 + strlen(file_name) + 1 + 1); // +\0 + EOF
			*total_bytes_in_first_packet_p = total_bytes_in_first_packet;
			char* buffer = (char*)malloc((total_bytes_in_first_packet) * sizeof(char)); // create packet
			if (buffer == NULL)
			{
				printf("cannot do malloc 1");
			}
			buffer[total_bytes_in_first_packet - 1] = EOF; //set EOF or \0 at the end of the file

			ID = i;

			uint32_t* CRC_p = (uint32_t*)(buffer + 0);
			*CRC_p = 0;

			uint32_t* ID_p = (uint32_t*)(buffer + 4); // 4 = offset for ID
			*ID_p = ID;

			uint32_t* zero = (uint32_t*)(buffer + 8); // 8 = offset also to make all another bits zero
			*zero = 0;

			uint16_t* packet_len = (uint16_t*)(buffer + 8); // 8 offset for packet len
			*packet_len = total_bytes_in_first_packet; //+1

			uint8_t* RND_index_p = (uint8_t*)(buffer + 10); // 8 = offset for RND index
			*RND_index_p = RND_index;

			uint8_t* logic_bits_p = (uint8_t*)(buffer + 11); // 9 = offset for running bits

			*logic_bits_p = *logic_bits_p | START_BIT;
			/*END OF HEAD
			* START OF BODY
			*/

			uint32_t* num_of_packets_p = (uint32_t*)(buffer + 12); // 12 = offset for total number of packets
			*num_of_packets_p = number_of_packets;

			uint32_t* standart_len_of_packets_p = (uint32_t*)(buffer + 16); // 12 = offset for total number of packets
			*standart_len_of_packets_p = total_bytes_in_packet;

			uint32_t* hash_p = (uint32_t*)(buffer + 20); // 20 = offset for total number of packets
			*hash_p = Data.HASH;


			char* file_name_p = (char*)(buffer + 24); // 12 = offset for total number of packets
			if (file_name_p == NULL) {
				printf("CANNOT ALOCATE MEMORY\n");
				return;
			}
			for (int o = 0; o < strlen(file_name) + 1; o++) {
				*(buffer + 24 + o) = file_name[o]; // including /0
			}

			// create CRC
			crc32::generate_table(table);
			printf("total bytes %i\n", total_bytes_in_first_packet);
			crc = crc32::update(table, 0, buffer, total_bytes_in_first_packet);
			*CRC_p = crc;

			packets[i] = buffer; // save packet do packets list
			if (PRINT) {
				printf("%lu, %u, %u %u, %lu %lu %s \n", ID, total_bytes_in_first_packet, RND_index, *logic_bits_p, number_of_packets, total_bytes_in_packet, file_name);

			}
		}
		else if (i != number_of_packets - 1) { // middle packets

			char* buffer = (char*)malloc((number_head_bytes + number_data_bytes_in_packet) * sizeof(char)); // create packet
			if (buffer == NULL)
			{
				printf("cannot do malloc 1");
			}
			
			fread(buffer + number_head_bytes, 1, number_data_bytes_in_packet, fileptr); // read bytes from file

			ID = i;

			uint32_t* CRC_p = (uint32_t*)(buffer + 0);
			*CRC_p = 0;
			uint32_t a = *CRC_p;

			uint32_t* ID_p = (uint32_t*)(buffer + 4); // 4 = offset for ID
			*ID_p = ID;


			uint32_t* zero = (uint32_t*)(buffer + 8); // 8 = offset also to make all another bits zero
			*zero = 0;

			uint16_t* packet_len = (uint16_t*)(buffer + 8); // 8 offset for packet len
			*packet_len = total_bytes_in_packet;

			uint8_t* RND_index_p = (uint8_t*)(buffer + 10); // 8 = offset for RND index
			*RND_index_p = RND_index;

			uint8_t* logic_bits_p = (uint8_t*)(buffer + 11); // 9 = offset for running bits

			*logic_bits_p = *logic_bits_p | BEETWEEN_BIT;

			crc32::generate_table(table);
			crc = crc32::update(table, 0, buffer, number_head_bytes + number_data_bytes_in_packet);
			*CRC_p = crc;
			packet_len = (uint16_t*)(buffer + 8); // 8 offset for packet len
			packets[i] = buffer; // save packet do packets list

		}
		else if (i == number_of_packets - 1) {
			uint32_t number_data_bytes_in_last_packet = (file_size) % (number_data_bytes_in_packet);

			if (number_data_bytes_in_last_packet == 0) {
				number_data_bytes_in_last_packet = number_data_bytes_in_packet;
			}

			printf("data B in last packet %lu\n", number_data_bytes_in_last_packet);

			char* buffer = (char*)malloc((number_head_bytes + number_data_bytes_in_last_packet) * sizeof(char)); // create packet
			if (buffer == NULL)
			{
				printf("cannot do malloc 1");
			}

			fread(buffer + number_head_bytes, 1, number_data_bytes_in_last_packet, fileptr); // read bytes from file

			ID = i;

			uint32_t* CRC_p = (uint32_t*)(buffer + 0);
			*CRC_p = 0;
			uint32_t a = *CRC_p;

			uint32_t* ID_p = (uint32_t*)(buffer + 4); // 4 = offset for ID
			*ID_p = ID;


			uint32_t* zero = (uint32_t*)(buffer + 8); // 8 = offset also to make all another bits zero
			*zero = 0;

			uint16_t* packet_len = (uint16_t*)(buffer + 8); // 8 offset for packet len
			*packet_len = number_head_bytes + number_data_bytes_in_last_packet;

			uint8_t* RND_index_p = (uint8_t*)(buffer + 10); // 8 = offset for RND index
			*RND_index_p = RND_index;

			uint8_t* logic_bits_p = (uint8_t*)(buffer + 11); // 9 = offset for running bits

			*logic_bits_p = *logic_bits_p | END_BIT;

			crc32::generate_table(table);
			crc = crc32::update(table, 0, buffer, number_head_bytes + number_data_bytes_in_last_packet);
			*CRC_p = crc;

			packets[i] = buffer; // save packet do packets list
		}
	}
	return;
}


// TX______________________________________TX____________________________________________TX___________________
DWORD WINAPI TX(void* data)
{
	/* OPEN and READ file
	*  COPY file to buffer
	*/


	char* file_name = "test.txt";
	//file_name = "png.PNG";
	//file_name = "onepixel.png";
	file_name = "noname.jpg";


	file_name = "4K.jpg";
	file_name = "1K.jpg";
	file_name = "16K.jpg";
	FILE* fileptr = fopen(file_name, "rb"); // Open the file
	uint64_t file_len;
	uint32_t number_of_packets;
	int total_bytes_in_first_packet;

	read_file(fileptr, &file_len, &number_of_packets); // get file_len and number of packets
	char** packets = (char**)malloc(number_of_packets * sizeof(char*)); // allocate memory for all packets
	if (packets == NULL)
	{
		printf("cannot do malloc 1");
	}

	split_to_packets(fileptr, packets, number_of_packets, file_name, &total_bytes_in_first_packet, file_len); //fill memory with data from the file
	fclose(fileptr); // Close the file

	// send first packet
SEND_FIRST_PACKET:

	sendto(connection_data.socketS, packets[0], total_bytes_in_first_packet, 0, (sockaddr*)&connection_data.addrDest, sizeof(connection_data.addrDest));
	printf("First packet send\n");
	uint32_t counter = 0;
	uint32_t tmp = 0;
	uint32_t ID_ack = 0;

	while (1) {
		EnterCriticalSection(&cs);
		tmp = Data.ID_accepted;
		LeaveCriticalSection(&cs);
		if (tmp == 0) {
			break;
		}
		else if (counter++ >= TIMEOUT) {
			printf("Sending first packet AGAIN.\n");
			goto SEND_FIRST_PACKET;
		}
		Sleep(UPDATE_PERIOD);
	}

	printf("First ACK received after %lu ms\n", counter);

	//Sending all packets (except last one)
	counter = 0;
	uint16_t packet_on_path = 0;
	uint32_t last_send_packet = 0;
	while (running) {
		EnterCriticalSection(&cs);
		ID_ack = Data.ID_accepted;
		LeaveCriticalSection(&cs);
		Sleep(DELAY_BEETWEEN_PACKETS);
		if (last_send_packet == number_of_packets - 1) {
			Sleep(UPDATE_PERIOD); //last was already send                   //change to 1
			if (ID_ack == last_send_packet)
			{
				Sleep(UPDATE_PERIOD);
				continue;
			}
		}
		else
		{
			last_send_packet++;
			if (last_send_packet == number_of_packets - 1) {
				uint16_t LEN_packet = *(uint16_t*)(packets[last_send_packet] + 8);
				sendto(connection_data.socketS, packets[last_send_packet], LEN_packet, 0, (sockaddr*)&connection_data.addrDest, sizeof(connection_data.addrDest));
			}
			else {
				uint16_t LEN_packet = *(uint16_t*)(packets[last_send_packet] + 8);
				sendto(connection_data.socketS, packets[last_send_packet], total_bytes_in_packet, 0, (sockaddr*)&connection_data.addrDest, sizeof(connection_data.addrDest));
			}
			if (PRINT || true) {
				printf("Send ID packet %lu\n", last_send_packet);
			}
		}

		counter = 0;
		while (1) {
			EnterCriticalSection(&cs);
			ID_ack = Data.ID_accepted;
			LeaveCriticalSection(&cs);

			if (last_send_packet - ID_ack < MAX_PACKETS_ON_PATH && !(last_send_packet == number_of_packets - 1))
			{
				break; //send next packet
			}
			counter++;
			Sleep(UPDATE_PERIOD);
			if (counter >= TIMEOUT) {
				last_send_packet = ID_ack;
				break;
			}
		}
	}

	for (uint64_t i = 0; i < number_of_packets; i++) {
		free(packets[i]);
	}
	free(packets);
	printf("Exiting thread TX.\n");

	return 0;
}


// RX____________________________________________RX_____________________________________RX_________________
DWORD WINAPI RX(void* data)
{
	uint32_t table[256];

	char buffer_rx[number_head_bytes];

	//alloc memory for compare crc
	uint32_t CRC_memory;

	//alloc head
	char* head;
	head = (char*)calloc(4, sizeof(char));
	uint32_t* CRC_p = (uint32_t*)(head);	//0 CRC
	uint32_t crc = 0;
	uint32_t ID;
	uint16_t LEN_packet;	//8 len of packet
	uint8_t RND_index; //10 offset RND index
	uint8_t logic_bits;//11 offset running bit



	uint8_t tmp_RND;
	while (running) {

		if (recvfrom(connection_data.socketS, buffer_rx, sizeof(buffer_rx), 0, (sockaddr*)&connection_data.from, &connection_data.fromlen) == SOCKET_ERROR) {
			printf("Socket error!\n");
			continue;
		}
		else
		{

			//copy crc to Compare, then do crc on whole buffer. When finished update CRC_p 
			CRC_memory = *(uint32_t*)(buffer_rx + 0);
			uint32_t* tmp = (uint32_t*)(buffer_rx + 0);
			*tmp = 0;

			crc32::generate_table(table);
			crc = crc32::update(table, 0, buffer_rx, number_head_bytes);

			//check if crc is fine, if not throw away the whole packet
			if (crc != CRC_memory) {
				printf("CRC failed, pop packet\n");				
				continue;
			}
			// CRC OK, continue
			//assign head to local variables
			ID = *(uint32_t*)(buffer_rx + 4);
			//LEN_packet = *(uint16_t*)(buffer_rx + 8); //not needed
			RND_index = *(uint8_t*)(buffer_rx + 10);
			//logic_bits = *(uint8_t*)(buffer_rx + 11); //not needed

			EnterCriticalSection(&cs);
			tmp_RND = Data.RND;
			LeaveCriticalSection(&cs);
			if (RND_index != tmp_RND) {
				printf("received packet with different RND index");
				continue;
			}
			EnterCriticalSection(&cs);

			if (Data.ID_accepted < ID || Data.ID_accepted == UINT32_MAX) {
				Data.ID_accepted = ID;
			} 

			if (ID == Data.number_of_packets - 1) {
				if (PRINT) {
					printf("Received ACK ID %lu\n", ID);
				}
				running = false;
				LeaveCriticalSection(&cs);
				break;
			}
			LeaveCriticalSection(&cs);
			if (PRINT || true) {
				printf("Received ACK ID %lu\n", ID);
			}
		}


	}
	printf("Exiting thread RX\n");
	return 0;

}

//MAIN_______________________________MAIN______________________________________________________MAIN________________
int main()
{


	// init connection
	InitWinsock();
	connection_data.fromlen = sizeof(connection_data.from);
	connection_data.local.sin_family = AF_INET;
	connection_data.local.sin_port = htons(LOCAL_PORT);
	connection_data.local.sin_addr.s_addr = INADDR_ANY;



	connection_data.addrDest.sin_family = AF_INET;
	connection_data.addrDest.sin_port = htons(TARGET_PORT);
	InetPton(AF_INET, _T(TARGET_IP), &connection_data.addrDest.sin_addr.s_addr);

	connection_data.socketS = socket(AF_INET, SOCK_DGRAM, 0);
	if (bind(connection_data.socketS, (sockaddr*)&connection_data.local, sizeof(connection_data.local)) != 0) {
		printf("Binding error!\n");
		getchar(); //wait for press Enter
		return 1;
	}
	//**********************************************************************


	// init critical section
	InitializeCriticalSectionAndSpinCount(&cs, 0x00000400);


	clock_t start = clock();
	//Do threads
	const int number_of_thread = 2;

	// Create threads
	HANDLE threads[number_of_thread];
	threads[0] = (HANDLE)CreateThread(NULL, 0, TX, NULL, 0, NULL);
	threads[1] = (HANDLE)CreateThread(NULL, 0, RX, NULL, 0, NULL);
	//wait for threads ends
	WaitForMultipleObjects(number_of_thread, threads, TRUE, INFINITE);
	closesocket(connection_data.socketS);

	printf("All done corecctly\n\n");

	clock_t end = clock();
	double elapsed = double(end - start) / CLOCKS_PER_SEC;

	printf("Time for transfer: %.3f seconds.\n", elapsed);
	printf("Average transfer speed is %.3f kb/s\n", (Data.file_size_in_Bytes / 1000) / elapsed);
	return 0;



}
