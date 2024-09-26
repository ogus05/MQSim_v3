#include <stdexcept>
#include "Engine.h"
#include "../utils/Logical_Address_Partitioning_Unit.h"

namespace MQSimEngine
{
	Engine* Engine::_instance = NULL;

	Engine* Engine::Instance() {
		if (_instance == 0) {
			_instance = new Engine;
			_instance->QT_InitSender();
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

		QT_Sender->SendConnPacket();
		QT_Sender->SendCompInfoPacket(0);
		
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
				}
				Sim_Event* consumed_event = ev;
				ev = ev->Next_event;
				delete consumed_event;
			}
			_EventList->Remove(minNode);
		}


		if(QT_Sender != nullptr){
			QT_Sender->SendCompInfoPacket();
			QT_Sender->SendDisconnPacket();
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

    void Engine::AttatchClearStats(void (*ClearStats)())
    {
		this->ClearStats = ClearStats;
    }

    void Engine::Start_LoadPhase()
    {
		loadPhase = true;
    }

    void Engine::Finish_LoadPhase(sim_time_type time, Sim_Object *io_flow)
    {
		loadPhase = false;
        this->waitingRunPhaseFlowList.push_back({time, io_flow});
		waitingLoadPhaseFinish = true;
    }
    void Engine::AddQTComp(QTComp *comp)
    {
		if(QT_Sender != nullptr){
			QT_Sender->AddQTComp(comp);
		}
    }

    void Engine::QT_SendInfo(const uint64_t &curReqCount)
    {
		if(QT_Sender != nullptr && !(loadPhase || waitingLoadPhaseFinish)){
			QT_Sender->SendCompInfoPacket(curReqCount);
		}
    }
    void Engine::QT_InitSender()
    {
		QT_Sender = new QTSender();
		int isQTDisplay = QT_Sender->Start();

		switch(isQTDisplay){
			case DONT_SEND:
				delete QT_Sender;
				QT_Sender = nullptr;
				break;
			case SEND_BLOCKED:
				delete QT_Sender;
				exit(1);
				break;
			case SEND_ALLOWED:
				break;
			default:
				PRINT_ERROR("Setting up process of QT Sender has been crashed.")
		}
    }

    void Engine::QT_SetMilestone(const uint64_t &totalReqs, const uint64_t& QTSendCount)
    {
		if(QT_Sender != nullptr){
			QT_Sender->SetMilestone(totalReqs, QTSendCount);
		}
    }

    void Engine::Start_RunPhase()
    {
		waitingLoadPhaseFinish = false;
		loadMileStone = CurrentTimeStamp;
		ClearStats();
		for(auto io_flow : waitingRunPhaseFlowList){
			this->Register_sim_event(loadMileStone + io_flow.first, io_flow.second);
		}

		PRINT_MESSAGE("Start Run Phase....")
    }
}