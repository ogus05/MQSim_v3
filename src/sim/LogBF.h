#include "Sim_Object.h"
#include <vector>
#include <iostream>
#include <fstream>
class LogBF
{
private:
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
    void logging(sim_time_type currentTime);

    void setMilstone(sim_time_type settingValue);
    LogBF(sim_time_type logMilestone, std::string ssdconfFileName, std::string workloadFileName, uint64_t sectorsPerPage);
    ~LogBF();
};
