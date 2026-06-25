#include <iostream>
#include <DAQHeader.h>
#include <IDODHit.h>

#include <stddef.h>
#include <cstdint>
#include <map>
#include <chrono>
#include <stdlib.h>     /* srand, rand */
#include <zmq.hpp>
#include <functional>

#include "DAQInterface.h"
#include "SlowControls.h"
#include "Msg.h"

using namespace ToolFramework;

int main(int argc, char *argv[]){
	
	if(argc==1){
		std::cout<<"usage: "<<argv[0]<<" [configuration_file]"<<std::endl;
		return 0;
	}
	Store initial_config;
	initial_config.Initialise(argv[1]);
	
	//////////////////////////////////////// Initialise DAQ interface /////////////////////////////////
	
	std::string Interface_configfile = "./InterfaceConfig";
	if(initial_config.Get<int>("cout_verbosity")) std::cout<<"Constructing DAQInterface"<<std::endl;
	DAQInterface DAQ_inter(Interface_configfile);
	
	///////////////////////////////////////// make slow controls //////////////////////////////////////
	
	SlowControls slow_controls(&DAQ_inter);
	
	//load initial values
	slow_controls.ChangeConfig(initial_config);
	
	// register web controls
	bool lockable=false; // false=this control can be modified even outside of testing runs
	
	DAQ_inter.sc_vars.Add("MonitoringPeriod",
	                      VARIABLE,
	                      std::bind(&SlowControls::SetMonitoringPeriod, &slow_controls, std::placeholders::_1),
	                      std::bind(&SlowControls::GetMonitoringPeriod, &slow_controls, std::placeholders::_1),
	                      lockable);
	
	DAQ_inter.sc_vars.Add("DataSendPeriod",
	                      VARIABLE,
	                      std::bind(&SlowControls::SetSendPeriod, &slow_controls, std::placeholders::_1),
	                      std::bind(&SlowControls::GetSendPeriod, &slow_controls, std::placeholders::_1),
	                      lockable);
	
	DAQ_inter.sc_vars.Add("DataResendPeriod",
	                      VARIABLE,
	                      std::bind(&SlowControls::SetResendPeriod, &slow_controls, std::placeholders::_1),
	                      std::bind(&SlowControls::GetResendPeriod, &slow_controls, std::placeholders::_1),
	                      lockable);
	
	DAQ_inter.sc_vars.Add("ClearBuffer",
	                      BUTTON,
	                      std::bind(&SlowControls::ClearBuffer, &slow_controls, std::placeholders::_1),
	                      0,
	                      lockable);
	
	DAQ_inter.sc_vars.Add("Start",
	                      BUTTON,
	                      std::bind(&SlowControls::SetStart, &slow_controls, std::placeholders::_1),
	                      0,
	                      lockable);
	
	DAQ_inter.sc_vars.Add("Stop",
	                      BUTTON,
	                      std::bind(&SlowControls::SetStop, &slow_controls, std::placeholders::_1),
	                      0,
	                      lockable);
	
	DAQ_inter.sc_vars.Add("Quit",
	                      BUTTON,
	                      std::bind(&SlowControls::SetQuit, &slow_controls, std::placeholders::_1),
	                      0,
	                      lockable);
	
	
	// register function to update configuration on ChangeConfig alert
	DAQ_inter.AlertSubscribe("ChangeConfig", std::bind(&SlowControls::ChangeConfigSignal, &slow_controls, std::placeholders::_1, std::placeholders::_2));
	
	// register function to okurn current configuration on ExportConfiguration alert
	// DAQ_inter.AlertSubscribe("ExportConfig", std::bind(&SlowControls::ExportConfig, &slow_controls, std::placeholders::_1, std::placeholders::_1)); // where do we send return to?
	DAQ_inter.sc_vars.Add("ExportConfig",
	                      BUTTON,
	                      std::bind(&SlowControls::ExportConfig, &slow_controls, std::placeholders::_1),
	                      std::bind(&SlowControls::ExportConfig, &slow_controls, std::placeholders::_1),
	                      lockable);
	
	//////////////////////////////////////Setting up data socket ///////////////////////////////////////
	
	zmq::context_t context(1);
	zmq::socket_t sock(context, ZMQ_DEALER);
	zmq::pollitem_t polls[] = {{sock,0,ZMQ_POLLIN,0}, {sock,0,ZMQ_POLLOUT,0}};
	try {
		sock.setsockopt(ZMQ_SNDHWM, 1);                  // messages to buffer in send socket
		sock.setsockopt(ZMQ_RCVHWM, 1000);               // messages to buffer in receive socket
		sock.setsockopt(ZMQ_LINGER, 0);                  // immediately discard pending messages when socket is closed
		sock.setsockopt(ZMQ_BACKLOG, 1000);              // remember and automatically re-connect to up to 1000 clients if they get disconnected
		sock.setsockopt(ZMQ_RCVTIMEO, 100);              // block socket::receive calls for 100ms if no messages are already in the receive buffer
		sock.setsockopt(ZMQ_SNDTIMEO, 100);              // block send calls for 100ms if there are no connected peers
		sock.setsockopt(ZMQ_IMMEDIATE, 1);               // on send, only queue messages to connected peers. If there are no connected peers, block.
		sock.setsockopt(ZMQ_TCP_KEEPALIVE, 1);           // set SO_KEEPALIVE option of underlying socket
		sock.setsockopt(ZMQ_TCP_KEEPALIVE_IDLE, 5);      // set TCP_KEEPALIVE option of underlying socket
		sock.setsockopt(ZMQ_TCP_KEEPALIVE_CNT, 12);      // set TCP_KEEPCNT option on underlying socket
		sock.setsockopt(ZMQ_TCP_KEEPALIVE_INTVL, 5);     // set TCP_KEEPINTVL option on underlying socket
		sock.setsockopt(ZMQ_IDENTITY, DAQ_inter.GetDeviceName().c_str(),DAQ_inter.GetDeviceName().length());
		
		
		std::string data_port;
		initial_config.Get("data_port",data_port);
		sock.bind("tcp://*:" + data_port);
		 
	} catch(zmq::error_t& e){
		std::cerr<<"caught "<<e.what()<<" configuring data socket"<<std::endl;
		exit(1);
	}
	
	////////////////////////////////////// Application Loop  ///////////////////////////////////////
	
	
	uint32_t message_num=0;
	std::map<uint32_t, Msg> msg_buffer;
	std::chrono::system_clock::time_point last_generate = std::chrono::system_clock::from_time_t(0);
	std::chrono::system_clock::time_point last_monitoring = std::chrono::system_clock::from_time_t(0);
	std::map<uint32_t, Msg>::iterator it;
	zmq::message_t rec;
	bool ok;
	int errornum;
	
	while(!slow_controls.quit){
		
		// if we haven't quit but are paused, just sleep for a bit
		if(slow_controls.paused){
			std::this_thread::sleep_for(std::chrono::seconds(1));
			if(slow_controls.cout_verbosity) std::cout<<"."<<std::flush;
			continue;
		}
		
		
		// while running...
		while (!slow_controls.quit && !slow_controls.paused){
			
			// if asked to clear the message buffer, clear it
			if(slow_controls.clear_buffer){
				msg_buffer.clear();
				slow_controls.clear_buffer = false;
			}
			
			// send monitoring data every monitoring_period
			if( (std::chrono::system_clock::now()-last_monitoring) > slow_controls.monitoring_period){
					
					last_monitoring = std::chrono::system_clock::now();
					slow_controls.SendMonitoring();
					
			}
			
			// add new data to the message buffer every send_period
			if( (std::chrono::system_clock::now()-last_generate) > slow_controls.send_period){
				
				last_generate = std::chrono::system_clock::now();
				
				// generate new data and push it into the message buffer
				msg_buffer.emplace(std::piecewise_construct,
				                   std::forward_as_tuple(message_num),
				                   std::forward_as_tuple(message_num, slow_controls.t0, &slow_controls));
				++message_num;
				
			}
			
			/*
			// ensure we don't OOM if there are no listeners
			if(msg_buffer.size()>slow_controls.max_buffer_size){
				size_t n_to_discard = msg_buffer.size()-slow_controls.max_buffer_size;
				std::string logmsg="Discarding messages "+std::to_string((msg_buffer.begin()+n_to_discard)->header.GetMessageNum())+"-"
				                   std::to_string(msg_buffer.begin()->header.GetMessageNum())+" due to buffer size limit";
				DAQ_inter.SendAlarm(logmsg);
				std::cerr<<logmsg<<std::endl;
				slow_controls.discarded_msgs_error += n_to_discard;
				msg_buffer.erase(msg_buffer.begin(), msg_buffer.begin()+n_to_discard);
				}
			}
			*/
			
			// read any acks on receive socket
			while(sock.recv(&rec, ZMQ_NOBLOCK)){
				
				++slow_controls.recvd_acks;
				
				uint32_t msg_num = *(reinterpret_cast<uint32_t*>(rec.data()));
				if(slow_controls.cout_verbosity>1) std::cout<<"received ack for message "<<(msg_num)<<std::endl;
				
				// remove acknowledged messages from the buffer
				slow_controls.ackd_msgs += msg_buffer.erase(msg_num);
				
			}
			
			// if message queue is empty we have nothing to do until our next scheduled send (but break early if we receive a message)
			if(msg_buffer.empty()){
				
				int sleep_ms = std::chrono::duration_cast<std::chrono::milliseconds>(last_generate+slow_controls.send_period-std::chrono::system_clock::now()).count();
				try {
					zmq::poll(&polls[0], 1, sleep_ms);
				} catch(zmq::error_t& e){
					std::cerr<<"zmq_poll caught "<<e.what()<<std::endl;
					DAQ_inter.SendLog("zmq::poll caught "+std::string{e.what()},LogLevel::Error);
				}
				continue;
				
			}
			
			// otherwise we have messages buffered; check if anything needs (re)sending
			for(it=msg_buffer.begin(); it!=msg_buffer.end(); ){
				
				// due for resend?
				if(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()-it->second.last_send) > slow_controls.resend_period){
					
					// actually do we want to resend it, or is it time to give up?
					if(it->second.send_attempts >= slow_controls.max_resends){
						DAQ_inter.SendAlarm("Discarding message "+std::to_string(it->first)+" due to no acknowledgement");
						std::cerr<<"Discarding message "<<it->first<<" due to no acknowledgement"<<std::endl;
						slow_controls.discarded_msgs_noack++;
						it = msg_buffer.erase(it);
						continue;
					}
					// else send it
					break;
					
				}
				++it;
				
			}
			
			// if nothing needs sending, check again in 1ms (but break early if we receive a message)
			if(it==msg_buffer.end()){
				
				try {
					zmq::poll(&polls[0], 1, 1);
				} catch(zmq::error_t& e){
					std::cerr<<"zmq_poll caught "<<e.what()<<std::endl;
					DAQ_inter.SendLog("zmq::poll caught "+std::string{e.what()},LogLevel::Error);
				}
				continue;
				
			}
			
			// something needs sending: send it
			Msg& msg = it->second;
			try {
				//printf("try send %lu attempt %lu, resend %lu\n", msg.header.GetMessageNum(),msg.send_attempts, msg.send_errors);
				ok = sock.send(&msg.header.GetData()[0], sizeof(msg.header), ZMQ_SNDMORE);
				if(ok) ok = sock.send(msg.data_ptr->data(), msg.data_ptr->size()*sizeof(uint32_t));
				if(!ok) errornum = EAGAIN;
			} catch(zmq::error_t& e){
				ok = false;
				errornum = e.num();
				// N.B. failure due to EAGAIN (e.g. receiver buffers are full) doesn't throw but okurns 0
			}
			
			// check for errors
			if(ok){
				
				slow_controls.successful_sends++;
				msg.last_send = std::chrono::system_clock::now();
				msg.send_attempts++;
				if(slow_controls.cout_verbosity>1){
					std::cout<<"sent msg_num: "<<msg.header.GetMessageNum()
					         <<" at "<<std::chrono::duration_cast<std::chrono::milliseconds>(msg.last_send-slow_controls.t0).count()
					         <<" ms"<<std::endl;  // XXX send time is time from t0 to now, so not application start if t0 is configured
				}
				
			} else {
				
				std::cerr<<"send failed with "<<zmq_strerror(errornum)<<std::endl;
				DAQ_inter.SendLog("zmq::send failed with "+std::string{zmq_strerror(errornum)},LogLevel::Error);
				slow_controls.failed_sends++;
				msg.send_errors++;
				if(msg.send_errors > (slow_controls.max_send_errors-1)){
					DAQ_inter.SendAlarm("Discarding message "+std::to_string(it->first)+" due to no repeated send errors");
					std::cerr<<"Discarding message "<<it->first<<" due to repeated send errors"<<std::endl;
					msg_buffer.erase(it);
					slow_controls.discarded_msgs_error++;
				}
				
			}
			
		} // < while running
		
		std::cout<<"data generation stopped"<<std::endl;
		
	} // < while !quit
	
	std::cout<<"application terminated"<<std::endl;
	msg_buffer.clear();
	
	return 0;
	
}
