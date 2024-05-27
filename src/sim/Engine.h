#ifndef ENGINE_H
#define ENGINE_H

#include <iostream>
#include <unordered_map>
#include <vector>
#include "Sim_Defs.h"
#include "EventTree.h"
#include "Sim_Object.h"
namespace MQSimEngine {
	class Engine
	{
		friend class EventTree;
	public:
		Engine()
		{
			this->_EventList = new EventTree;
			started = false;
			waitingLoadPhaseFinish = false;
		}

		~Engine() {
			delete _EventList;
		}
		
		static Engine* Instance();
		sim_time_type Time();
		Sim_Event* Register_sim_event(sim_time_type fireTime, Sim_Object* targetObject, void* parameters = NULL, int type = 0);
		void Ignore_sim_event(Sim_Event*);
		void Reset();
		void AddObject(Sim_Object* obj);
		Sim_Object* GetObject(sim_object_id_type object_id);
		void RemoveObject(Sim_Object* obj);
		void Start_simulation();
		void Stop_simulation();
		bool Has_started();
		bool Is_integrated_execution_mode();
		void AttachClearStats(void(*clearStatsFnc)());

		void Finish_LoadPhase(sim_time_type time, Sim_Object* io_flow);

		void AttachPerodicalFnc(void(*fnc)());

		sim_time_type loadMileStone;
		bool loadPhase = false;

		void SetLogFilePath(std::string ssd_config_file_path, std::string workload_defs_file_path);

		std::string GetLogFilePath();
	private:
		sim_time_type _sim_time;
		EventTree* _EventList;
		std::unordered_map<sim_object_id_type, Sim_Object*> _ObjectList;
		bool stop;
		bool started;
		static Engine* _instance;

		std::vector<std::pair<sim_time_type, Sim_Object*>> waitingRunPhaseFlowList;
		bool waitingLoadPhaseFinish;
		void Start_RunPhase();
		
		std::vector<void(*)()> clearStatsFncList;
		void ExecuteClearStatsFnc();

		std::vector<void(*)()> periodicalFncList;
		void ExecutePeriodicalFnc();

		std::string logFilePath;
	};
}

#define Simulator MQSimEngine::Engine::Instance()
#endif // !ENGINE_H
