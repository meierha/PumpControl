#include "timeprogramrunner.h"
#include <easylogging++.h>
using namespace std;
TimeProgramRunner::TimeProgramRunner(TimeProgramRunnerCallback* callback_client, PumpDriverInterface *pump_driver){
    callback_client_ = callback_client;
    pump_driver_ = pump_driver;
    // pump_definitions_ = pump_driver_->GetPumps();
}
TimeProgramRunner::~TimeProgramRunner(){
}
void TimeProgramRunner::Run(){
    chrono::steady_clock::time_point wakeup_time_point= chrono::steady_clock::now();
    chrono::steady_clock::time_point start_time;
    std::unique_lock<std::mutex> lk(time_lock_mutex_);
    
    int timeprogram_time = 0;
    while(timeprogramrunner_state_ != TIME_PROGRAM_STOPPING){
        condition_variable_.wait_until(lk, wakeup_time_point);
        {
            std::lock_guard<std::mutex> guard(state_machine_mutex_);

            if(timeprogramrunner_target_state_ != timeprogramrunner_state_){
                switch(timeprogramrunner_target_state_){
                case TIME_PROGRAM_INIT:
                    LOG(FATAL) << "the init state should never be actively set";
                    break;
                case TIME_PROGRAM_ACTIVE:
                    if(timeprogramrunner_state_ == TIME_PROGRAM_IDLE){
                        start_time = chrono::steady_clock::now();
                        timeprogram_time = timeprogram_.begin()->first;
                    }
                    break;
                case TIME_PROGRAM_IDLE:
                    if(timeprogramrunner_state_ == TIME_PROGRAM_ACTIVE){
                        callback_client_->TimeProgramRunnerProgramEnded(programm_id_);
                    }
                    break;
                default:
                    //do nothing
                    break;
                }
                timeprogramrunner_state_ = timeprogramrunner_target_state_;
                callback_client_->TimeProgramRunnerStateUpdate(timeprogramrunner_state_);
            }

            switch(timeprogramrunner_state_){
            case TIME_PROGRAM_ACTIVE:
                {
                    //progress update
                    int end_time = timeprogram_.rbegin()->first;
                    int current_time = chrono::duration_cast<std::chrono::milliseconds>(chrono::steady_clock::now() - start_time).count();
                    callback_client_->TimeProgramRunnerProgressUpdate(programm_id_,(100 *current_time / end_time));
                    
                    //check if we already are ready to enable or disable pumps
                    if(current_time >= timeprogram_time){
                        auto it = timeprogram_.find(timeprogram_time);
                        for(auto i: it->second){
                            pump_driver_->SetPump(i.first, i.second);
                        } 
                        // callback_client_->TimeProgramRunnerProgressUpdate(programm_id_,(100 * it->first) / (--timeprogram_.end())->first);
                        it++;
                        if(it != timeprogram_.end()){
                            timeprogram_time = it->first;
                            int wakeup_time = min(current_time + 1000,timeprogram_time);
                            wakeup_time_point = start_time + chrono::milliseconds(wakeup_time);
                        }else{
                            timeprogramrunner_target_state_ = TIME_PROGRAM_IDLE;
                            callback_client_->TimeProgramRunnerProgressUpdate(programm_id_,100);
                            wakeup_time_point = chrono::steady_clock::now();
                        }
                    }else{
                        int wakeup_time = min(current_time + 1000,timeprogram_time);
                        wakeup_time_point = start_time + chrono::milliseconds(wakeup_time);
                    }                    
                }   
                break;
            case TIME_PROGRAM_IDLE:
                {
                    // for(auto i : pumpdefinitions_){
                    //     pump_driver_->SetPump(i.first, 0);
                    // }
                    wakeup_time_point = chrono::steady_clock::now() + chrono::hours(1);
                } 
                break;
            case TIME_PROGRAM_STOPPING:
                {
                    int pumpCount = pump_driver_->GetPumpCount();
                    for(int i = 1; i<= pumpCount; i++){
                        pump_driver_->SetPump(i, 0);
                    }
                }
                break;
            case TIME_PROGRAM_INIT:
                LOG(FATAL) << "init state should not be met here...";
            }
        }
    }   
}

void TimeProgramRunner::Shutdown(){
    std::lock_guard<std::mutex> guard(state_machine_mutex_);
    if(timeprogramrunner_state_ != TIME_PROGRAM_STOPPING){
        timeprogramrunner_target_state_ = TIME_PROGRAM_STOPPING;
        condition_variable_.notify_one();
    }
}
void TimeProgramRunner::StartProgram(std::string id, TimeProgram time_program){
    std::lock_guard<std::mutex> guard(state_machine_mutex_);
    if(timeprogramrunner_state_ == TIME_PROGRAM_IDLE){
        timeprogramrunner_target_state_ = TIME_PROGRAM_ACTIVE;
        timeprogram_ = time_program;
        programm_id_ = id;
        condition_variable_.notify_one();
    }
}
void TimeProgramRunner::EmergencyStopProgram(){
    std::lock_guard<std::mutex> guard(state_machine_mutex_);
    if(timeprogramrunner_state_ == TIME_PROGRAM_ACTIVE){
        timeprogramrunner_target_state_ = TIME_PROGRAM_IDLE;
        condition_variable_.notify_one();
    }
}

const char* TimeProgramRunner::NameForState(TimeProgramRunner::TimeProgramRunnerState state){
  switch(state){
    case TIME_PROGRAM_ACTIVE: return "active";
    case TIME_PROGRAM_INIT: return "init";
    case TIME_PROGRAM_IDLE: return "idle";
    case TIME_PROGRAM_STOPPING: return "stopping";
  }
  return "internal problem";
}