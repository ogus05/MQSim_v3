#include "LogBF.h"
#include "SSD_Defs.h"
#include "Logical_Address_Partitioning_Unit.h"
#include "Engine.h"

LogBF* LogBF::_instance = NULL;

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

void LogBF::logging()
{
    while(_instance->currentMilestone < CurrentTimeStamp){
        _instance->loggingFile << _instance->bitFilterCount << " " << _instance->currentTermReadCount << std::endl;
        
        for(auto& e : _instance->bitFilter){
            e = 0;
        }
        _instance->bitFilterCount = 0;
        _instance->currentMilestone += _instance->logMilestone;
        _instance->currentTermReadCount = 0;
    }
}

void LogBF::setMilstone()
{
    _instance->currentMilestone = CurrentTimeStamp + _instance->logMilestone;
    for(auto& e : _instance->bitFilter){
        e = 0;
    }
    _instance->bitFilterCount = 0;
    _instance->currentTermReadCount = 0;
}

LogBF::LogBF(sim_time_type logMilestone, std::string logFilePath, uint64_t sectorsPerPage)
{
    this->logMilestone = logMilestone;
    bitFilter.resize((Utils::Logical_Address_Partitioning_Unit::Get_total_device_lha_count() / 64) + 1);

    bitFilterCount = 0;

    currentTermReadCount = 0;
    currentMilestone = logMilestone;

    this->sectorsPerPage = sectorsPerPage;

    loggingFile.open("LOG_" + logFilePath);
    loggingFile << Utils::Logical_Address_Partitioning_Unit::Get_total_device_lha_count() << std::endl;

    _instance = this;

    Simulator->AttachPerodicalFnc(logging);
    Simulator->AttachClearStats(setMilstone);
}

LogBF::~LogBF()
{
    loggingFile.close();
}
