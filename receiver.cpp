// UDP_Communication_Framework.cpp : Defines the entry point for the console application.
// created by Hrdina and Jan Pitak for KDS

#pragma comment(lib, "ws2_32.lib")
#include "stdafx.h"
#include <winsock2.h>
#include "ws2tcpip.h"
#include <stdint.h>
#include <iostream>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>
#include <windows.h>
#include<stdio.h>
#include<iostream>
#include <Windows.h>
#include <string.h>

#pragma once
#define TARGET_IP	"127.0.0.1"
#define BUFFERS_LEN 1024
#define START_BIT 1 // 8bit, 1 on MSB
#define BEETWEEN_BIT 2 // 8bit, 1 on MSB-1
#define END_BIT 4 // 8bit, 1 on MSB-2
CRITICAL_SECTION CriticalSection;
#define TARGET_PORT 14000
#define LOCAL_PORT 15001
//#define LOCAL_PORT 15555
#define TIMEOUT 1

#define PRINT true

struct crc32 //crc. Code was downloaded from internet
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
void InitWinsock()	//multihreading
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
}

struct S_head {	//struct used for everything, mostly for head of every packet
	char* head;
	uint32_t* CRC_p;	//0 CRC
	uint32_t crc;	//crc of packet
	uint32_t ID;	//ID of packet
	uint16_t LEN_packet;	//8 len of packet
	uint8_t RND_index; //10 offset RND index
	uint8_t logic_bits;//11 offset running bit
	uint32_t last_ACK_ID = UINT32_MAX;
	uint32_t HASH;
	char* filename;
};
volatile S_head My_head;

struct comunication { 
	SOCKET socketS;
	struct sockaddr_in local;
	struct sockaddr_in from;
	int fromlen = sizeof(from);
	sockaddr_in addrDest;
};

void join_together(char** array, char* name, uint32_t count) { //function to join packets into a file
	FILE* fp;
	fp = fopen(name, "wb");
	if (PRINT) {
		printf("Delka retezce %i\n", My_head.LEN_packet);
	}
	for (int i = 1; i < count; i++) {
		My_head.LEN_packet = (*(uint16_t*)(array[i] + 8));
	}

	for (int i = 1; i < count; i++) {
		My_head.LEN_packet = *(uint16_t*)(array[i] + 8);
		for (int q = 0; q < My_head.LEN_packet - 12; q++) {
			fwrite(&array[i][12 + q], 1, 1, fp);
		}
	}

	fflush(fp);
	fclose(fp);
};

comunication Port_com;
volatile uint32_t COUNT_packet = 0;
char** data_array = NULL;
volatile bool send_b = 0;
volatile int latest_packet = 0;

bool running = true;
//-------------------------------------------------------------------------------------------------------------------------------------------
//RECIEVE
DWORD WINAPI RX(void* data) {
	uint32_t table[256];
	crc32::generate_table(table);
	uint32_t CRC_memory;
	My_head.crc = 0;
	My_head.ID = UINT32_MAX;
	
	//start packet inicialization
	uint32_t SIZE_packet;
	uint32_t HASH;
	char* name_file = NULL;
	char* tmp_packet = NULL;
	bool first_packet_again = false;
	uint8_t counter = 0;
	bool last_packet_received = false;
	while (1) {
		//**********************************************************************
		int len = 0;
		if (My_head.ID == UINT32_MAX || first_packet_again) {	//read first packet
			printf("ID 0\n");
			counter = 0;
			char buffer_rx[BUFFERS_LEN];
			if (first_packet_again) {
				strncpy(tmp_packet, buffer_rx, SIZE_packet);
				first_packet_again = false;
			}
			else {
				if (recvfrom(Port_com.socketS, buffer_rx, sizeof(buffer_rx), 0, (sockaddr*)&Port_com.from, &Port_com.fromlen) == SOCKET_ERROR) {
					printf("Socket error! 1\n");
					free(buffer_rx);
					continue;
				}
			}
			while (true) {
				if (buffer_rx[len] == EOF) {
					len++;
					break;
				}
				len++;
			}
			if (PRINT) {
				printf("Length is:%i \n", len);
			}
			//crc
			CRC_memory = *(uint32_t*)buffer_rx;
			uint32_t* tmp = (uint32_t*)buffer_rx;
			*tmp = 0;
			crc32::generate_table(table);
			printf("len %lu\n", len);
			My_head.crc = crc32::update(table, 0, buffer_rx, len);
			*(uint32_t*)buffer_rx = (uint32_t)My_head.crc;
			uint32_t CRC_p = *(uint32_t*)buffer_rx;
			if (CRC_p != CRC_memory) {
				if (PRINT) {
					printf("CRC failed, pop packet before\n");
				}
				My_head.ID == UINT32_MAX;
				continue;
			}
			//assign head to local variables
			EnterCriticalSection(&CriticalSection);
			if (data_array != NULL) {
				for (int i = 0; i < COUNT_packet + 1; i++) {
					free(data_array[i]);
				}
				free(data_array);
			}

			My_head.ID = *(uint32_t*)(buffer_rx + 4);
			My_head.LEN_packet = *(uint16_t*)(buffer_rx + 8);
			My_head.RND_index = *(uint8_t*)(buffer_rx + 10);
			My_head.logic_bits = *(uint8_t*)(buffer_rx + 11);
			LeaveCriticalSection(&CriticalSection);
			//if first packet assign contecnt of packet
			COUNT_packet = *(uint32_t*)(buffer_rx + 12);
			SIZE_packet = *(uint32_t*)(buffer_rx + 16);
			My_head.HASH = *(uint32_t*)(buffer_rx + 20);

			int size_name = len - 1 - 24;
			name_file = (char*)malloc(size_name);
			for (int i = 24; i < len -1; i++) {
				name_file[i - 24] = buffer_rx[i];
			}

			EnterCriticalSection(&CriticalSection);
			My_head.filename = name_file;

			data_array = (char**)calloc(COUNT_packet + 1, sizeof(char*));
			if (data_array == NULL) {
				printf("cannot allocate memory\n");
				LeaveCriticalSection(&CriticalSection);

				return -1;
			}
			data_array[0] = name_file;
			send_b = 1;
			LeaveCriticalSection(&CriticalSection);
			if (PRINT) {
				printf("ID %lu Len %lu Count %lu Size %lu\n", My_head.ID, My_head.LEN_packet, COUNT_packet, SIZE_packet);

			}
		}
		else {
			//after first packet
			tmp_packet = (char*)malloc(1024 * sizeof(char));

			if (recvfrom(Port_com.socketS, tmp_packet, ((1024) * sizeof(char)), 0, (sockaddr*)&Port_com.from, &Port_com.fromlen) == SOCKET_ERROR) {
				int a = WSAGetLastError();
				printf("Socket error 5, num %i\n", a);
				continue;
			}

			uint16_t* packet_len = (uint16_t*)(tmp_packet + 8); 
			CRC_memory = *(uint32_t*)tmp_packet;
			uint32_t* tmp = (uint32_t*)tmp_packet;
			*tmp = 0;
			EnterCriticalSection(&CriticalSection);
			My_head.logic_bits = *(uint8_t*)(tmp_packet + 11);

			if (My_head.logic_bits == END_BIT) {
				My_head.LEN_packet = *(uint16_t*)(tmp_packet + 8);
				SIZE_packet = My_head.LEN_packet;
			}
			My_head.LEN_packet = *(uint16_t*)(tmp_packet + 8);
			LeaveCriticalSection(&CriticalSection);

			crc32::generate_table(table);
			My_head.crc = crc32::update(table, 0, tmp_packet, My_head.LEN_packet);
			if (My_head.crc != CRC_memory) {
				if (PRINT) {
					printf("CRC failed, pop packet after\n");
				}
				continue;
			}

			//assign head to local variables

			EnterCriticalSection(&CriticalSection);
			My_head.ID = *(uint32_t*)(tmp_packet + 4);


			uint8_t RND = *(uint8_t*)(tmp_packet + 10);
			LeaveCriticalSection(&CriticalSection);
			//check if ID 0 was send, must be send 2 packet with ID 0 to cancel this transfer
			if (My_head.ID == 0) {
				counter++;
				if (counter == 2) {
					first_packet_again = true;
					counter = 0;

				}
				continue;
			}
			else {
				counter = 0;
			}
			if (RND != My_head.RND_index) {
				printf("RND index does not correspod, pop packet\n");
				continue;
			}
			if (PRINT) {
				printf("ID received %lu\n", My_head.ID);
			}
			if (My_head.logic_bits != END_BIT) {
				EnterCriticalSection(&CriticalSection);
				if (data_array[My_head.ID] != NULL) {
					free(data_array[My_head.ID]);
				}
				send_b = 1;
				data_array[My_head.ID] = tmp_packet;
				LeaveCriticalSection(&CriticalSection);
			}
			else { //end packet
				

				EnterCriticalSection(&CriticalSection);
				if (data_array[My_head.ID] != NULL) {
					free(data_array[My_head.ID]);
				}
				data_array[My_head.ID] = tmp_packet;
				last_packet_received = true;
				send_b = 1;
				
				LeaveCriticalSection(&CriticalSection);
				
			}
			EnterCriticalSection(&CriticalSection);

			if (last_packet_received && My_head.last_ACK_ID == COUNT_packet - 1 && running) {
				running = false;
			}
			LeaveCriticalSection(&CriticalSection);
		}
	}
	printf("Exiting thread RX\n");
	return 0;
}
//--------------------------------------------------------------------------------------------------------------------------------------------------
//SEND
DWORD WINAPI TX(void* data) {
	uint32_t table[256];
	uint32_t crc;
	char* buffer = (char*)malloc((12) * sizeof(char)); // create packet
	uint32_t counter = 0;
	while (1) {
		buffer[12 - 1] = EOF; //set EOF or \0 at the end of the file
		uint32_t* CRC_p = (uint32_t*)(buffer + 0);
		*CRC_p = 0;
		bool last_ACK_send = false;
		counter = 0;
		while (true) {
			Sleep(1);
			EnterCriticalSection(&CriticalSection);
			if (data_array != NULL) {
				counter++;
				if (counter > 1500) {
					last_ACK_send = true;
					break;
				}
			}
			if (send_b == 1) { // || !running) {
				break;
			}
			else {
				LeaveCriticalSection(&CriticalSection);
			}
		}
		uint32_t* ID_p = (uint32_t*)(buffer + 4);
		*ID_p = My_head.ID; // 4 = offset for ID
		send_b = 0;
		*ID_p = (uint32_t)0;

		for (int i = My_head.last_ACK_ID; i < COUNT_packet + 1; i++) {
			//printf("i %i\n", i);
			if (data_array[i] == NULL) {
				*ID_p = (uint32_t)(i - 1);
				break;
			}

		}

		if (last_ACK_send) {
			printf("try to join file\n");
			join_together(data_array, My_head.filename, COUNT_packet);
			LeaveCriticalSection(&CriticalSection);
			break;

		}
		My_head.last_ACK_ID = *ID_p;

		uint16_t* LEN_p = (uint16_t*)(buffer + 8);
		*LEN_p = My_head.LEN_packet; // 8 offset for packet len
		uint8_t* RND_p = (uint8_t*)(buffer + 10);
		*RND_p = My_head.RND_index; // 8 = offset for RND index
		uint8_t* LOGIC_p = (uint8_t*)(buffer + 11);
		*LOGIC_p = My_head.logic_bits; // 9 = offset for running bits
		LeaveCriticalSection(&CriticalSection);

		crc32::generate_table(table);
		crc = crc32::update(table, 0, buffer, 12);
		*CRC_p = crc;

		sendto(Port_com.socketS, buffer, 12, 0, (sockaddr*)&Port_com.addrDest, sizeof(Port_com.addrDest));
		if (PRINT) {
			printf("ACK SEND id %lu\n", *(uint32_t*)(buffer + 4));
		}
		
		
	}

	printf("Exiting thread TX\n");
	return 0;
}
//**********************************************************************
int main()
{

	InitWinsock();
	Port_com.local.sin_family = AF_INET;
	Port_com.local.sin_port = htons(LOCAL_PORT);
	Port_com.local.sin_addr.s_addr = INADDR_ANY;

	Port_com.addrDest.sin_family = AF_INET;
	Port_com.addrDest.sin_port = htons(TARGET_PORT);
	InetPton(AF_INET, _T(TARGET_IP), &Port_com.addrDest.sin_addr.s_addr);
	Port_com.socketS = socket(AF_INET, SOCK_DGRAM, 0);

	if (bind(Port_com.socketS, (sockaddr*)&Port_com.local, sizeof(Port_com.local)) != 0) {
		printf("Binding error!\n");
		getchar(); //wait for press Enter
		return 1;
	}
	//**********************************************************************


	InitializeCriticalSectionAndSpinCount(&CriticalSection, 0x00000400);
	//Threads
	const int number_of_thread = 2;
	HANDLE threads[number_of_thread];
	threads[0] = (HANDLE)CreateThread(NULL, 0, TX, NULL, 0, NULL);
	threads[1] = (HANDLE)CreateThread(NULL, 0, RX, NULL, 0, NULL);

	WaitForMultipleObjects(number_of_thread-1, threads, TRUE, INFINITE);
	std::kill_dependency(threads[1]);
	printf("Transfer complete\n");

	

	printf("name %s\n", My_head.filename);
	FILE* fileptr = fopen(My_head.filename, "rb");
	if (fileptr == NULL) {
		printf("Cannot open file\n");
		
	}
	else {


		uint64_t file_len;
		uint32_t number_of_packets;
		fseek(fileptr, 0, SEEK_END); //set pointer to end of file
		file_len = ftell(fileptr);
		if (file_len > 4294967295)
			printf("ERROR: Cannot send bigger file than 4.29GB");

		rewind(fileptr); // set pointer back to start of the file

		char* tmp = (char*)malloc(file_len * sizeof(char));
		fread(tmp, 1, file_len, fileptr);

		//check hash
		uint32_t table[256];
		crc32::generate_table(table);
		uint32_t tmp_crc = crc32::update(table, 0, tmp, file_len);
		rewind(fileptr);

		if (tmp_crc == My_head.HASH) {
			printf("HASH is OK\n");
		}
		else {
			printf("ERROR: HASH failed\n");
		}
	}
	if (data_array != NULL) {
		for (int i = 0; i < COUNT_packet + 1; i++) {
			free(data_array[i]);
		}
		free(data_array);
	}


	printf("All done\n");
	return 0;
	
	
}
