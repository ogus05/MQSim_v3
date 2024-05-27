#pragma once

#include "Sim_Object.h"
#include <vector>
#include <iostream>
#include <fstream>
class LogBF
{
private:
    static LogBF* _instance;

    sim_time_type logMilestone;
    std::vector<uint64_t> bitFilter;
    
    uint64_t bitFilterCount;
    
    uint64_t currentTermReadCount;
    sim_time_type currentMilestone;

    uint32_t sectorsPerPage;
    
    std::ofstream loggingFile;
public:
    void addReadCount();
    void checkRead(uint64_t lpa, uint64_t sectors);

    static void setMilstone();

    static void logging(); 

    LogBF(sim_time_type logMilestone, std::string logFilePath, uint64_t sectorsPerPage);
    ~LogBF();
};
