#include "LogBF.h"
#include "SSD_Defs.h"
#include "Logical_Address_Partitioning_Unit.h"

void LogBF::addReadCount()
{
    currentTermReadCount++;
}

void LogBF::checkRead(uint64_t lpa, uint64_t sectors)
{
    if((bitFilter.at(((lpa * sectorsPerPage) + sectors) / 64) & (1 << (((lpa * sectorsPerPage) + sectors) % 64))) == 0){
        bitFilter.at(((lpa * sectorsPerPage) + sectors) / 64) |= (1 << (((lpa * sectorsPerPage) + sectors) % 64));
        bitFilterCount += 1;
    }
    
}

void LogBF::logging(sim_time_type currentTime)
{
    while(currentMilestone < currentTime){
        loggingFile << bitFilterCount << " " << currentTermReadCount << std::endl;
        
        for(auto& e : bitFilter){
            e = 0;
        }
        bitFilterCount = 0;
        currentMilestone += logMilestone;
        currentTermReadCount = 0;
    }
}

void LogBF::setMilstone(sim_time_type settingValue)
{
    this->currentMilestone = settingValue + logMilestone;
    for(auto& e : bitFilter){
        e = 0;
    }
    bitFilterCount = 0;
    currentTermReadCount = 0;
}

LogBF::LogBF(sim_time_type logMilestone, std::string ssdconfFileName, std::string workloadFileName, uint64_t sectorsPerPage)
{
    std::string workloadName = ssdconfFileName.substr(ssdconfFileName.find_last_of("_") + 1, ssdconfFileName.find_last_of(".") - ssdconfFileName.find_last_of("_") - 1) \
			+ workloadFileName.substr(workloadFileName.find_last_of("_"), workloadFileName.find_last_of(".") - workloadFileName.find_last_of("_"));
        
    this->logMilestone = logMilestone;
    bitFilter.resize((Utils::Logical_Address_Partitioning_Unit::Get_total_device_lha_count() / 64) + 1);

    bitFilterCount = 0;

    currentTermReadCount = 0;
    currentMilestone = logMilestone;

    this->sectorsPerPage = sectorsPerPage;

    loggingFile.open("LOG_" + workloadName);
    loggingFile << Utils::Logical_Address_Partitioning_Unit::Get_total_device_lha_count() << std::endl;
}

LogBF::~LogBF()
{
    loggingFile.close();
}
