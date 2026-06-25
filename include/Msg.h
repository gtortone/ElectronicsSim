#ifndef MSG_H
#define MSG_H
#include <iostream>
#include <chrono>
#include "DAQHeader.h"
#include "SlowControls.h"
 
struct Msg {
	
	std::chrono::time_point<std::chrono::system_clock> last_send;
	int send_attempts;
	int send_errors;
	DAQHeader header;
	std::vector<uint32_t> data; // TODO replace with vector retrieved from memory Pool
	std::vector<uint32_t>* data_ptr;
	
	Msg(uint32_t msg_num, std::chrono::time_point<std::chrono::system_clock> start, SlowControls* slow_controls){
		
		uint32_t coarse_counts = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now()-start).count() * 4;  //number of ticks i.e. of 250ps
		
		uint16_t card_id = rand()%slow_controls->n_boards;
		header.SetMessageNum(msg_num);
		header.SetCardID(card_id);
		header.SetCoarseCounter(coarse_counts);
		
		if(slow_controls->use_static_data){
			data_ptr = &slow_controls->static_data;
		} else {
			slow_controls->GenerateFakeData(data, start);
			data_ptr = &data;
		}
		
		send_attempts=0;
		send_errors=0;
		last_send=std::chrono::system_clock::from_time_t(0);
		
	}
	
};


#endif
