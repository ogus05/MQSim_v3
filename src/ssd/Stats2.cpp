#include "Stats2.h"
#include "Engine.h"



int Stats2::printStats2 = 0;

FILE* Stats2::F_CACHE_HIT = NULL;

FILE* Stats2::F_COR_READ = NULL;
std::map<uint32_t, uint32_t> Stats2::corReadMap;

FILE* Stats2::F_TOTAL_COUNT = NULL;
uint64_t Stats2::flushCount = 0;
uint64_t Stats2::mergeCount = 0;
uint64_t Stats2::clusteringCount = 0;

FILE* Stats2::F_READ_AND_MODIFY = NULL;
std::map<uint32_t, uint32_t> Stats2::RAMCount;

FILE* Stats2::F_SHORT_TERM = NULL;
sim_time_type Stats2::loggingMilestone = 0;
sim_time_type Stats2::nextLoggingTime = 0;

FILE* Stats2::F_REMAIN_FREE_BLOCK = NULL;


void Stats2::Init_Stats2()
{
    struct stat info;
    if(stat("Stats2", &info) != 0){
        if(mkdir("Stats2", 0777) == -1){
            std::cout << "Stats2 - error in create directory" << std::endl;
            exit(0);
        }
    }

    std::string logFilePath = Simulator->GetLogFilePath();

    F_CACHE_HIT = fopen(("Stats2/CH" + logFilePath).c_str(), "w");
    F_SHORT_TERM = fopen(("Stats2/ST" + logFilePath).c_str(), "w");
    F_COR_READ = fopen(("Stats2/CR" + logFilePath).c_str(), "w");
    F_TOTAL_COUNT = fopen(("Stats2/CC" + logFilePath).c_str(), "w");
    F_READ_AND_MODIFY = fopen(("Stats2/RM" + logFilePath).c_str(), "w");
    F_REMAIN_FREE_BLOCK = fopen(("Stats2/RFB" + logFilePath).c_str(), "w");
}

void Stats2::Reset_Stats2(){

    std::string logFilePath = Simulator->GetLogFilePath();

    if(F_CACHE_HIT != NULL){
        fclose(F_CACHE_HIT);
    }

    if(F_SHORT_TERM != NULL){
        fclose(F_SHORT_TERM);
    }

    if(F_COR_READ != NULL){
        if(corReadMap.size() > 0){
            if(F_COR_READ){
                for(auto corReadEntry : corReadMap){
                    fprintf(F_COR_READ, "%d %d\n", corReadEntry.first, corReadEntry.second);
                }
            }
            fclose(F_COR_READ);
            corReadMap.clear();
        }
    }

    if(F_TOTAL_COUNT != NULL){
        if(((flushCount + mergeCount + clusteringCount) > 0)){
            fprintf(F_TOTAL_COUNT, "Flush : %llu\n", flushCount);
            fprintf(F_TOTAL_COUNT, "Merge : %llu\n", mergeCount);
            fprintf(F_TOTAL_COUNT, "Clustering : %llu\n", clusteringCount);
            fclose(F_TOTAL_COUNT);
            flushCount = 0;
            mergeCount = 0;
            clusteringCount = 0;
        }
    }

    if(F_READ_AND_MODIFY != NULL){
        for(auto RAMCountEntry : RAMCount){
            fprintf(F_READ_AND_MODIFY, "%d %d\n", RAMCountEntry.first, RAMCountEntry.second);
        }
        fclose(F_READ_AND_MODIFY);
    }

    if(F_REMAIN_FREE_BLOCK != NULL){
        fclose(F_REMAIN_FREE_BLOCK);
    }
}

void Stats2::cacheHit(uint32_t pageCacheHit, uint32_t sectCacheHit, uint32_t cacheMiss)
{
    if(F_CACHE_HIT != NULL){
        if(!(Simulator->loadPhase || Simulator->waitingLoadPhaseFinish) && F_CACHE_HIT){
            fprintf(F_CACHE_HIT, "%d %d %d\n", pageCacheHit, sectCacheHit, cacheMiss);
        }
    }
}

void Stats2::corRead(uint32_t trCount)
{
    if(F_COR_READ != NULL){
        if(!(Simulator->loadPhase || Simulator->waitingLoadPhaseFinish)){
            if(corReadMap.find(trCount) == corReadMap.end()){
                corReadMap.insert({trCount, 0});
            }
            corReadMap.at(trCount)++;
        }
    }
}

void Stats2::addFlushCount()
{
    flushCount++;
}

void Stats2::addMergeCount()
{
    mergeCount++;
}

void Stats2::addClusteringCount()
{
    if(F_SHORT_TERM != NULL){
        clusteringCount++;
        if(!(Simulator->loadPhase || Simulator->waitingLoadPhaseFinish)){
            fprintf(F_SHORT_TERM, "C %llu\n", CurrentTimeStamp);
        }
    }
}

void Stats2::addReadAndModify(uint32_t sectSizeToRead)
{
    RAMCount[sectSizeToRead]++;
}

void Stats2::setLoggingMilestone(sim_time_type loggingMilestone)
{
    Stats2::loggingMilestone = loggingMilestone;
}

bool Stats2::addShortTermLogging(sim_time_type STAT_sum_device_response_time_read_short_term, sim_time_type STAT_sum_device_response_time_write_short_term, sim_time_type STAT_sum_device_response_time_short_term,
    uint32_t STAT_serviced_read_request_count_short_term, uint32_t STAT_serviced_write_request_count_short_term, uint32_t STAT_serviced_request_count_short_term)
{
    if(F_SHORT_TERM != NULL){
        if(!(Simulator->loadPhase || Simulator->waitingLoadPhaseFinish)){
            fprintf(F_SHORT_TERM, "%llu %llu %llu %lu %lu %lu %llu\n", STAT_sum_device_response_time_read_short_term, STAT_sum_device_response_time_write_short_term, STAT_sum_device_response_time_short_term,
                STAT_serviced_read_request_count_short_term, STAT_serviced_write_request_count_short_term, STAT_serviced_request_count_short_term, CurrentTimeStamp);
            return true;
        }
    }
    return false;
}

void Stats2::addBlockCount(uint64_t channelID, uint64_t chipID, uint64_t dieID, uint64_t planeID, uint64_t fullPageMappedBlockCount, uint64_t partialPageMappedBlockCount, uint64_t freeBlockCount)
{
    if(F_REMAIN_FREE_BLOCK != NULL){
        fprintf(F_REMAIN_FREE_BLOCK, "%llu %llu %llu %llu %llu %llu %llu\n", channelID, chipID, dieID, planeID, fullPageMappedBlockCount, partialPageMappedBlockCount, freeBlockCount);
    }
}
