

#include <iostream>
#include <vector>
#include <algorithm>
#include <sys/stat.h>
#include <sys/types.h>
#include "FlashTypes.h"


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
    static int printStats2;

public:
    static void Init_Stats2(std::string ssd_config_file_path, std::string workload_defs_file_path);
    static void Clear_Stats2();
    static void handleExternalTransaction(unsigned int trSizeInSectors, bool isWrite);
    static void handleCache(uint32_t cacheHitSizeInSectors);
    static void handleCleaningCache(uint32_t countCleaningEntries);
    static void handleMapping(uint32_t cleaningEntryCount, uint64_t mappingLatency, uint64_t ppn);
    static void handleMappingRelatedToGC(uint64_t ppn);
    static void handleGarbageCollection(uint64_t GCLatency, bool holdsMappingData);
    static void handleReadAndModify(uint32_t readSectorsRAM);
    static void handleSector(LPA_type lpa);
};
