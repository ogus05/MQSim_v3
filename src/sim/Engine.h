#ifndef ENGINE_H
#define ENGINE_H

#include <iostream>
#include <unordered_map>
#include <vector>
#include <functional>
#include "Sim_Defs.h"
#include "EventTree.h"
#include "Sim_Object.h"
#include "QTSender.h"

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
		void AttatchClearStats(void(*ClearStats)());

		void Start_LoadPhase();
		void Finish_LoadPhase(sim_time_type time, Sim_Object* io_flow);
		
        void AddQTComp(QTComp* comp);

		// Set QT Milestone which is indicating the time of executing send func.
		void QT_SetMilestone(const uint64_t& totalReqs, const uint64_t& QTSendCount);

		// Execute Send function of QT Sender.
		// Executed when the time milestone was reached.
		void QT_SendInfo(const uint64_t& curReqCount);


		sim_time_type loadMileStone;
		bool loadPhase;
	private:
		// Initialization QT Sender.
		// Executed when the Engine Simulation was started.
		void QT_InitSender();

		sim_time_type _sim_time;
		EventTree* _EventList;
		std::unordered_map<sim_object_id_type, Sim_Object*> _ObjectList;
		bool stop;
		bool started;
		static Engine* _instance;

		QTSender* QT_Sender;

		std::vector<std::pair<sim_time_type, Sim_Object*>> waitingRunPhaseFlowList;
		bool waitingLoadPhaseFinish;
		void Start_RunPhase();
		void(*ClearStats)();
	};
}

#define Simulator MQSimEngine::Engine::Instance()
#endif // !ENGINE_H
