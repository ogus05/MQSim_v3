#ifndef STATS2_H
#define STATS2_H

#include <iostream>
#include <vector>
#include <algorithm>
#include <map>
#include <sys/stat.h>
#include <sys/types.h>
#include "FlashTypes.h"


class Stats2
{
private:

    static int printStats2;

    static FILE* F_CACHE_HIT;

    static FILE* F_COR_READ;
    static std::map<uint32_t, uint32_t> corReadMap;

    static FILE* F_TOTAL_COUNT;
    static uint64_t flushCount;
    static uint64_t mergeCount;
    static uint64_t clusteringCount;

    static FILE* F_READ_AND_MODIFY;
    static std::map<uint32_t, uint32_t> RAMCount;

    static FILE* F_SHORT_TERM;
    static sim_time_type loggingMilestone;
    static sim_time_type nextLoggingTime;

public:
    static void Init_Stats2();
    static void Reset_Stats2();
    static void cacheHit(uint32_t pageCacheHit, uint32_t sectCacheHit, uint32_t cacheMiss);
    static void corRead(uint32_t trCount);
    static void addFlushCount();
    static void addMergeCount();
    static void addClusteringCount();
    static void addReadAndModify(uint32_t sectSizeToRead);

    static void setLoggingMilestone(sim_time_type loggingMilestone);
    static bool addShortTermLogging(sim_time_type STAT_sum_device_response_time_read_short_term, sim_time_type STAT_sum_device_response_time_write_short_term, sim_time_type STAT_sum_device_response_time_short_term,
        uint32_t STAT_serviced_read_request_count_short_term, uint32_t STAT_serviced_write_request_count_short_term, uint32_t STAT_serviced_request_count_short_term);
};

#endif