#include <stdexcept>
#include "Engine.h"
#include "../utils/Logical_Address_Partitioning_Unit.h"

namespace MQSimEngine
{
	Engine* Engine::_instance = NULL;

	Engine* Engine::Instance() {
		if (_instance == 0) {
			_instance = new Engine;
		}
		return _instance;
	}

	void Engine::Reset()
	{
		_EventList->Clear();
		_ObjectList.clear();
		_sim_time = 0;
		stop = false;
		started = false;
		Utils::Logical_Address_Partitioning_Unit::Reset();
	}


	//Add an object to the simulator object list
	void Engine::AddObject(Sim_Object* obj)
	{
		if (_ObjectList.find(obj->ID()) != _ObjectList.end()) {
			throw std::invalid_argument("Duplicate object key: " + obj->ID());
		}
		_ObjectList.insert(std::pair<sim_object_id_type, Sim_Object*>(obj->ID(), obj));
	}
	
	Sim_Object* Engine::GetObject(sim_object_id_type object_id)
	{
		auto itr = _ObjectList.find(object_id);
		if (itr == _ObjectList.end()) {
			return NULL;
		}

		return (*itr).second;
	}

	void Engine::RemoveObject(Sim_Object* obj)
	{
		std::unordered_map<sim_object_id_type, Sim_Object*>::iterator it = _ObjectList.find(obj->ID());
		if (it == _ObjectList.end()) {
			throw std::invalid_argument("Removing an unregistered object.");
		}
		_ObjectList.erase(it);
	}

	/// This is the main method of simulator which starts simulation process.
	void Engine::Start_simulation()
	{
		started = true;

		for(std::unordered_map<sim_object_id_type, Sim_Object*>::iterator obj = _ObjectList.begin();
			obj != _ObjectList.end();
			++obj) {
			if (!obj->second->IsTriggersSetUp()) {
				obj->second->Setup_triggers();
			}
		}

		for (std::unordered_map<sim_object_id_type, Sim_Object*>::iterator obj = _ObjectList.begin();
			obj != _ObjectList.end();
			++obj) {
			obj->second->Validate_simulation_config();
		}
		
		for (std::unordered_map<sim_object_id_type, Sim_Object*>::iterator obj = _ObjectList.begin();
			obj != _ObjectList.end();
			++obj) {
			obj->second->Start_simulation();
		}
		
		Sim_Event* ev = NULL;
		while (true) {
			if (_EventList->Count == 0 || stop) {
				if(waitingLoadPhaseFinish){
					Start_RunPhase();
				} else{
					break;
				}
			}

			EventTreeNode* minNode = _EventList->Get_min_node();
			ev = minNode->FirstSimEvent;

			_sim_time = ev->Fire_time;

			while (ev != NULL) {
				if(!ev->Ignore) {
					ev->Target_sim_object->Execute_simulator_event(ev);
					if(!loadPhase && !waitingLoadPhaseFinish){
						ExecutePeriodicalFnc();
					}
				}
				Sim_Event* consumed_event = ev;
				ev = ev->Next_event;
				delete consumed_event;
			}
			_EventList->Remove(minNode);
		}
	}

	void Engine::Stop_simulation()
	{
		stop = true;
	}

	bool Engine::Has_started()
	{
		return started;
	}

	sim_time_type Engine::Time()
	{
		return _sim_time;
	}

	Sim_Event* Engine::Register_sim_event(sim_time_type fireTime, Sim_Object* targetObject, void* parameters, int type)
	{
		Sim_Event* ev = new Sim_Event(fireTime, targetObject, parameters, type);
		DEBUG("RegisterEvent " << fireTime << " " << targetObject)
		_EventList->Insert_sim_event(ev);
		return ev;
	}

	void Engine::Ignore_sim_event(Sim_Event* ev)
	{
		ev->Ignore = true;
	}

	bool Engine::Is_integrated_execution_mode()
	{
		return false;
	}

    void Engine::AttachClearStats(void(*clearStatsFnc)())
    {
		this->clearStatsFncList.push_back(clearStatsFnc);
    }

    void Engine::Finish_LoadPhase(sim_time_type time, Sim_Object* io_flow)
    {
        this->waitingRunPhaseFlowList.push_back({time, io_flow});
		waitingLoadPhaseFinish = true;
    }
    void Engine::AttachPerodicalFnc(void (*fnc)())
    {
		periodicalFncList.push_back(fnc);
    }
    void Engine::SetLogFilePath(std::string ssd_config_file_path, std::string workload_defs_file_path)
    {
		if(ssd_config_file_path.find("_") == std::string::npos || workload_defs_file_path.find("_") == std::string::npos){
			std::cout << "ssdconfig file name and workload file name should be included under bar (\"_\")" << std::endl;
			std::cout << "e.g) ssdconfig_test.xml / workload_test.xml" << std::endl;
			exit(0);
		} else{
			this->logFilePath = ssd_config_file_path.substr(ssd_config_file_path.find_last_of("_") + 1, ssd_config_file_path.find_last_of(".") - ssd_config_file_path.find_last_of("_") - 1) \
			+ workload_defs_file_path.substr(workload_defs_file_path.find_last_of("_"), workload_defs_file_path.find_last_of(".") - workload_defs_file_path.find_last_of("_"));
		}
    }

    std::string Engine::GetLogFilePath()
    {
        return logFilePath;
    }

    void Engine::Start_RunPhase()
    {
		waitingLoadPhaseFinish = false;
		loadMileStone = CurrentTimeStamp;
		ExecuteClearStatsFnc();
		for(auto io_flow : waitingRunPhaseFlowList){
			this->Register_sim_event(loadMileStone + io_flow.first, io_flow.second);
		}

		PRINT_MESSAGE("Start Run Phase....")
    }
    
	void Engine::ExecuteClearStatsFnc()
    {
		for(auto& fnc : clearStatsFncList){
			fnc();
		}
    }

    void Engine::ExecutePeriodicalFnc()
    {
		for(auto& fnc : periodicalFncList){
			fnc();
		}
    }
}