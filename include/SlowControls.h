#ifndef SLOW_CONTROLS_H
#define SLOW_CONTROLS_H
#include <iostream>
#include <chrono>
#include <stdlib.h>     /* srand, rand */
#include "IDODHit.h"
#include "IDODSync.h"
#include "DAQInterface.h"
 
using namespace ToolFramework;

class SlowControls{
	
	public:
	
	SlowControls(DAQInterface* in_DAQ_inter){
		DAQ_inter=in_DAQ_inter;
	}
	
	std::string SetMonitoringPeriod(const char* controlname){
		int new_period = DAQ_inter->sc_vars[controlname]->GetValue<int>();
		monitoring_period = std::chrono::seconds{new_period};
		return "monitoring period set to "+std::to_string(new_period);
	}
	
	std::string GetMonitoringPeriod(const char* controlname){
		return std::to_string(monitoring_period.count());
	}
	
	std::string SetSendPeriod(const char* controlname){
		int new_period = DAQ_inter->sc_vars[controlname]->GetValue<int>();
		send_period = std::chrono::milliseconds{new_period};
		return "send period set to "+std::to_string(new_period);
	}
	
	std::string GetSendPeriod(const char* controlname){
		return std::to_string(send_period.count());
	}
	
	std::string SetResendPeriod(const char* controlname){
		int new_period = DAQ_inter->sc_vars[controlname]->GetValue<int>();
		resend_period = std::chrono::milliseconds{new_period};
		return "resend period set to "+std::to_string(new_period);
	}
	
	std::string GetResendPeriod(const char* controlname){
		return std::to_string(resend_period.count());
	}
	
	std::string SetStart(const char* controlname){
		paused = false;
		return "data generation started";
	}
	
	std::string SetStop(const char* controlname){
		paused = true;
		return "data generation paused";
	}
	
	std::string SetQuit(const char* controlname){
		quit=true;
		return "quitting";
	}
	
	std::string ClearBuffer(const char* controlname){
		clear_buffer=true;
		return "clearing message buffer";
	}
	
	bool ChangeConfigSignal(const char* alertname, const char* payload){
		Store new_config;
		new_config.JsonParser(DAQ_inter->GetLocalConfig());
		return ChangeConfig(new_config);
	}
	
	bool ChangeConfig(Store& config_store){
		
		time_t t0_unix;
		int monitoring_period_s;
		int send_period_ms;
		int resend_period_ms;
		
		config_store.Get("cout_verbosity",cout_verbosity);
		config_store.Get("logging_verbosity",log_verbosity);
		config_store.Get("use_static_data",use_static_data);
		config_store.Get("timespan_ms",timespan_ms);
		config_store.Get("n_boards", n_boards);
		config_store.Get("hitrate_hz",hitrate_hz);
		config_store.Get("random_seed",random_seed);
		config_store.Get("max_resends",max_resends);
		config_store.Get("max_send_errors",max_send_errors);
		
		if(config_store.Get("t0",t0_unix)) t0 = std::chrono::system_clock::from_time_t(t0_unix);
		if(config_store.Get("send_period_ms",send_period_ms)) send_period = std::chrono::milliseconds{send_period_ms};
		if(config_store.Get("resend_period_ms",resend_period_ms)) resend_period = std::chrono::milliseconds{resend_period_ms};
		if(config_store.Get("monitoring_period_s", monitoring_period_s)) monitoring_period = std::chrono::seconds{monitoring_period_s};
		
		srand(time(&random_seed)); // XXX not used for anything
		
		if(use_static_data) GenerateFakeData(static_data, t0);
		
		return true;
	}
	
	bool ExportConfigSignal(const char* control, const char* payload){
		return DAQ_inter->SetLocalConfig(ExportConfig());
	}
	
	std::string ExportConfig(const char* control=nullptr){
		Store tmp;
		
		tmp.Set("use_static_data", use_static_data);
		tmp.Set("timespan_ms",timespan_ms);
		tmp.Set("n_boards",n_boards);
		tmp.Set("hitrate_hz",hitrate_hz);
		tmp.Set("random_seed",random_seed);
		tmp.Set("max_resends",max_resends);
		tmp.Set("max_send_errors",max_send_errors);
		
		tmp.Set("t0", std::chrono::system_clock::to_time_t(t0));
		tmp.Set("send_period_ms", send_period.count());
		tmp.Set("resend_period_ms", resend_period.count());
		tmp.Set("monitoring_period_s", monitoring_period.count());
		
		std::string tmp_json;
		tmp >> tmp_json;
		return tmp_json;
	}
	
	DAQInterface* DAQ_inter;
	
	// application control
	bool quit = false;
	bool paused = true;
	int log_verbosity=1; // verbosity level for remote Logging calls
	int cout_verbosity=1; // verbosity level for cout
	bool change_config = false;
	bool clear_buffer = false;
	std::chrono::seconds monitoring_period{1}; // time between sending monitoring data
	std::chrono::milliseconds send_period{5000}; // time between sending new data
	std::chrono::milliseconds resend_period{5000}; // time between re-sending if no ack is received
	int max_resends=5; // how many successful sends without getting an ack before giving up
	int max_send_errors=5; // how many zmq errors when trying to send before giving up
	
	// fake data generation
	bool use_static_data=true;
	std::chrono::time_point<std::chrono::system_clock> t0 = std::chrono::system_clock::now();
	int timespan_ms=100;
	int n_boards=1;
	int hitrate_hz = 10E3;
	long int random_seed=0;
	std::vector<uint32_t> static_data;
	void GenerateFakeData(std::vector<uint32_t>& data, std::chrono::time_point<std::chrono::system_clock> start);
	
	// monitoring
	Store monitoring_data;
	std::string monitoring_json;
	int successful_sends=0; // successful calls to zmq::send
	int failed_sends=0; // failed calls to zmq::send
	int ackd_msgs=0; // messages that were sent and whose ack arrived in time
	int recvd_acks=0; // total number of acks received (including those that arrived after we gave up waiting and potentially duplicates)
	int discarded_msgs_error=0; // number of messages we gave up on sending because zmq::send errored too many times
	int discarded_msgs_noack=0; // number of messages we gave up on because no ack arrived despite multiple resends
	
	void SendMonitoring(){
		monitoring_data.Set("successful_sends",std::to_string(successful_sends));
		monitoring_data.Set("failed_sends",std::to_string(failed_sends));
		monitoring_data.Set("ackd_msgs",std::to_string(ackd_msgs));
		monitoring_data.Set("recvd_acks",std::to_string(recvd_acks));
		monitoring_data.Set("discarded_msgs_error",std::to_string(discarded_msgs_error));
		monitoring_data.Set("discarded_msgs_noack",std::to_string(discarded_msgs_noack));
		monitoring_data >> monitoring_json;
		DAQ_inter->SendMonitoringData(monitoring_json, "stats");
	}
	
};

void SlowControls::GenerateFakeData(std::vector<uint32_t>& data, std::chrono::time_point<std::chrono::system_clock> start){
	
	// generate fake data for given number of boards spanning given number of milliseconds,
	// assuming a given rate of hits on each channel
	// ------------------------------------------------------------------------------------
	
	size_t n_words;
	
	// a sync packet is generated by each board every ms
	n_words = timespan_ms*n_boards;
	
	// each board has 24 tubes, each of which generates hits at hitrate_hz
	size_t n_hits_per_ms = n_boards * 24 * hitrate_hz/1000;
	n_words += timespan_ms*n_hits_per_ms;
	
	// both sync packets and hits are 2 words each
	n_words *= 2;
	
	data.reserve(n_words);
	data.clear(); // in case we're re-using Msg structs from a Pool
	
	uint64_t ps;
	 
	for(size_t i=0; i < timespan_ms; i++){
		
		// generate sync packet. 
		// should have one per board, incrementing by 4000000 ticks per ms from t0 (1ms/250ps) 
		// but for now just generate some number
		ps=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now()-start).count() * 4;
		IDODSync tmp((uint32_t)(ps >>16));
		
		// add to data packet
		data.push_back(tmp.GetData()[0]);
		data.push_back(tmp.GetData()[1]);
		
		// generate hits for this ms
		for( size_t j=0; j< n_hits_per_ms; j++){
			
			// again hit times from each board should span the time period,
			// but for now just generate some numbers
			ps=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now()-start).count() * 4;
			IDODHit tmp2((uint8_t)ps, (uint16_t)(ps>>16));
			tmp2.SetPed(false);
			
			// add to data packet
			data.push_back(tmp2.GetData()->at(0));
			data.push_back(tmp2.GetData()->at(1));
			
			if(cout_verbosity>10 && j<10 && i==0) tmp2.Print(); // debug
		}
		
	}
	
};

#endif
