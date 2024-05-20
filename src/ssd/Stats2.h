#ifndef STATS2_H
#define STATS2_H

#include <iostream>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <sys/stat.h>
#include <sys/types.h>
#include "FlashTypes.h"

struct SectorData{
    LPA_type lpa;
    sim_time_type writtenTime;
    uint32_t sectorsCount;

    SectorData(LPA_type in_lpa, sim_time_type in_writtenTime)
        :lpa(in_lpa), writtenTime(in_writtenTime), sectorsCount(0){};
};


class Stats2
{
private:
    static FILE* OFS_EXTERNALTRANSACTION;
    static FILE* OFS_CACHE;
    static FILE* OFS_MAPPING;
    static FILE* OFS_GC;
    static FILE* OFS_READANDMODIFY;
    static FILE* OFS_CLEANING;
    static FILE* OFS_SECTOR;

    static std::vector<uint64_t> mappingRelatedToGC;

    static std::vector<uint64_t> holdsMappingDataGC;

    static std::unordered_map<PPA_type, SectorData> sectorDataList;
    static int printStats2;

public:
    static void Init_Stats2();
    static void Clear_Stats2();
    static void handleExternalTransaction(unsigned int trSizeInSectors, bool isWrite);
    static void handleCache(uint32_t cacheHitSizeInSectors);
    static void handleCleaningCache(uint32_t countCleaningEntries);
    static void handleMapping(uint32_t cleaningEntryCount, uint64_t mappingLatency, uint64_t ppn);
    static void handleMappingRelatedToGC(uint64_t ppn);
    static void handleGarbageCollection(uint64_t GCLatency, bool holdsMappingData);
    static void handleReadAndModify(uint32_t readSectorsRAM);
    static void storeSectorData(LPA_type lpa, PPA_type ppa, sim_time_type writtenTime);
    static void handleSector();
};

#endif