#include "Stats2.h"



FILE* Stats2::OFS_EXTERNALTRANSACTION = nullptr;
FILE* Stats2::OFS_CACHE = nullptr;
FILE* Stats2::OFS_MAPPING = nullptr;
FILE* Stats2::OFS_GC = nullptr;
FILE* Stats2::OFS_READANDMODIFY = nullptr;
FILE* Stats2::OFS_CLEANING = nullptr;
bool Stats2::printStats2 = false;

std::vector<uint64_t> Stats2::mappingRelatedToGC = std::vector<uint64_t>();

void Stats2::Init_Stats2(std::string workloadName, bool isPrint)
{
    printStats2 = isPrint;
    if(printStats2){
        std::cout << printStats2 << std::endl;
        OFS_EXTERNALTRANSACTION = fopen(("traces/ET/" + workloadName).c_str(), "w");
        OFS_CACHE = fopen(("traces/CACHE/" + workloadName).c_str(), "w");
        OFS_MAPPING = fopen(("traces/MAPPING/" + workloadName).c_str(), "w");
        OFS_GC = fopen(("traces/GC/" + workloadName).c_str(), "w");
        OFS_READANDMODIFY = fopen(("traces/RAM/" + workloadName).c_str(), "w");
        OFS_CLEANING = fopen(("traces/CLEANING/" + workloadName).c_str(), "w");
    }
}

void Stats2::Close_Stats2(){
    if(printStats2){
        fclose(OFS_EXTERNALTRANSACTION);
        fclose(OFS_CACHE);
        fclose(OFS_MAPPING);
        fclose(OFS_GC);
        fclose(OFS_READANDMODIFY);
        fclose(OFS_CLEANING);
        mappingRelatedToGC.clear();
    }
}

// param 1. Sector based size of a transaction.
void Stats2::handleExternalTransaction(unsigned int trSizeInSectors, bool isWrite)
{
    if(printStats2){
        fprintf(OFS_EXTERNALTRANSACTION, "%d %d\n", trSizeInSectors, isWrite ? 1 : 0);
    }
}

// param 1. Hit size in sectors of a cache hit transaction.
void Stats2::handleCache(uint32_t cacheHitSizeInSectors)
{
    if(printStats2){
        fprintf(OFS_CACHE, "%d\n", cacheHitSizeInSectors);
    }
}

void Stats2::handleCleaningCache(uint32_t countCleaningEntries){
    if(printStats2){
        fprintf(OFS_CLEANING, "%d\n", countCleaningEntries);
    }
}

// param 1. # of cleaning entries when a mapping read transaction issued.
// param 2. Latency of a mapping transaction.
// param 3. Exclude a transaction related to GC.
void Stats2::handleMapping(uint32_t cleaningEntryCount, uint64_t mappingLatency, uint64_t ppn)
{
    if(printStats2){
        auto it = std::find(mappingRelatedToGC.begin(), mappingRelatedToGC.end(), ppn);
        if(it != mappingRelatedToGC.end()){
            mappingRelatedToGC.erase(it);
        } else{
            fprintf(OFS_MAPPING, "%d %llu\n", cleaningEntryCount, (unsigned long long)mappingLatency);
        }
    }
}

void Stats2::handleMappingRelatedToGC(uint64_t ppn)
{
    if(printStats2){
        mappingRelatedToGC.push_back(ppn);
    }
}

// param 1. Latency of a GC transaction.
void Stats2::handleGarbageCollection(uint64_t GCLatency, bool holdsMappingData)
{
    if(printStats2){
        fprintf(OFS_GC, "%llu %d\n", (unsigned long long)GCLatency, holdsMappingData ? 1 : 0);
    }
}

// param 1. # of sectors needed for read and modify.
void Stats2::handleReadAndModify(uint32_t readSectorsRAM)
{
    if(printStats2){
        fprintf(OFS_READANDMODIFY, "%d\n", readSectorsRAM);
    }
}
