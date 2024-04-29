#include "Stats2.h"
#include "Engine.h"



FILE* Stats2::OFS_EXTERNALTRANSACTION = nullptr;
FILE* Stats2::OFS_CACHE = nullptr;
FILE* Stats2::OFS_MAPPING = nullptr;
FILE* Stats2::OFS_GC = nullptr;
FILE* Stats2::OFS_READANDMODIFY = nullptr;
FILE* Stats2::OFS_CLEANING = nullptr;
FILE* Stats2::OFS_SECTOR = nullptr;
int Stats2::printStats2 = 0;

std::vector<uint64_t> Stats2::mappingRelatedToGC = std::vector<uint64_t>();
std::unordered_map<PPA_type, SectorData> Stats2::sectorDataList = std::unordered_map<PPA_type, SectorData>();

void Stats2::Init_Stats2(std::string ssd_config_file_path, std::string workload_defs_file_path)
{
    std::cout << "Stats2 - 0. skip / 1. EXTERNAL TRANSACTION / 2. CACHE / 4. MAPPING / 8. GC /"\
                    " 16. READ AND MODIFY / 32. CLEANING / 64. SECTORLOG" << std::endl;
    std::cin >> printStats2;
    if(printStats2 > 0){
        if(printStats2 > (0b10000000 - 1)){
            std::cout << "Stats - error in stats2 code" << std::endl;
        }

        if(ssd_config_file_path.find("_") == std::string::npos || workload_defs_file_path.find("_") == std::string::npos){
            std::cout << "If you want to print Stats2, ssdconfig and workload file path must include \"_\"" << std::endl;
            std::cout << "e.g) ssdconfig_test.xml / workload_test.xml" << std::endl;
            exit(0);
        }
        std::string workloadName = ssd_config_file_path.substr(ssd_config_file_path.find_last_of("_") + 1, ssd_config_file_path.find_last_of(".") - ssd_config_file_path.find_last_of("_") - 1) \
			+ workload_defs_file_path.substr(workload_defs_file_path.find_last_of("_"), workload_defs_file_path.find_last_of(".") - workload_defs_file_path.find_last_of("_"));
        
        struct stat info;
        if(stat("Stats2", &info) != 0){
            if(mkdir("Stats2", 0777) == -1){
                std::cout << "Stats2 - error in create directory" << std::endl;
                exit(0);
            }
        }

        if(printStats2 & 0b1){
            OFS_EXTERNALTRANSACTION = fopen(("Stats2/ET_" + workloadName).c_str(), "w");
            std::cout << "Start printing external transaction" << std::endl;
        } if(printStats2 & 0b10){
            OFS_CACHE = fopen(("Stats2/CACHE_" + workloadName).c_str(), "w");
            std::cout << "Start printing cache" << std::endl;
        } if(printStats2 & 0b100){
            OFS_MAPPING = fopen(("Stats2/MAPPING_" + workloadName).c_str(), "w");
            std::cout << "Start printing cleaning" << std::endl;
        } if(printStats2 & 0b1000){
            OFS_GC = fopen(("Stats2/GC_" + workloadName).c_str(), "w");
            std::cout << "Start printing garbage collection" << std::endl;
        } if(printStats2 & 0b10000){
            OFS_READANDMODIFY = fopen(("Stats2/RAM_" + workloadName).c_str(), "w");
            std::cout << "Start printing read and modify" << std::endl;
        } if(printStats2 & 0b100000){
            OFS_CLEANING = fopen(("Stats2/CLEANING_" + workloadName).c_str(), "w");
            std::cout << "Start printing cleaning" << std::endl;
        } if(printStats2 & 0b1000000){
            OFS_SECTOR = fopen(("Stats2/SECTOR_" + workloadName).c_str(), "w");
            std::cout << "Start printing sector log" << std::endl;
        }
    } else{
        std::cout << "Stop printing Stats2" << std::endl;
    }
}

void Stats2::Clear_Stats2(){
    mappingRelatedToGC.clear();
    if(OFS_EXTERNALTRANSACTION){
        fclose(OFS_EXTERNALTRANSACTION);
    } if(OFS_CACHE){
        fclose(OFS_CACHE);
    } if(OFS_MAPPING){
        fclose(OFS_MAPPING);
    } if(OFS_GC){
        fclose(OFS_GC);
    } if(OFS_READANDMODIFY){
        fclose(OFS_READANDMODIFY);
    } if(OFS_CLEANING){
        fclose(OFS_CLEANING);
    } if(OFS_SECTOR){
        fclose(OFS_SECTOR);
    }
}

// param 1. Sector based size of a transaction.
void Stats2::handleExternalTransaction(unsigned int trSizeInSectors, bool isWrite)
{
    if(!Simulator->loadPhase && OFS_EXTERNALTRANSACTION){
        fprintf(OFS_EXTERNALTRANSACTION, "%d %d\n", trSizeInSectors, isWrite ? 1 : 0);
    }
}

// param 1. Hit size in sectors of a cache hit transaction.
void Stats2::handleCache(uint32_t cacheHitSizeInSectors)
{
    if(!Simulator->loadPhase && OFS_CACHE){
        fprintf(OFS_CACHE, "%d\n", cacheHitSizeInSectors);
    }
}

void Stats2::handleCleaningCache(uint32_t countCleaningEntries){
    if(!Simulator->loadPhase && OFS_CLEANING){
        fprintf(OFS_CLEANING, "%d\n", countCleaningEntries);
    }
}

// param 1. # of cleaning entries when a mapping read transaction issued.
// param 2. Latency of a mapping transaction.
// param 3. Exclude a transaction related to GC.
void Stats2::handleMapping(uint32_t cleaningEntryCount, uint64_t mappingLatency, uint64_t ppn)
{
    if(!Simulator->loadPhase && OFS_MAPPING){
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
    if(!Simulator->loadPhase && OFS_MAPPING){
        mappingRelatedToGC.push_back(ppn);
    }
}

// param 1. Latency of a GC transaction.
void Stats2::handleGarbageCollection(uint64_t GCLatency, bool holdsMappingData)
{
    if(!Simulator->loadPhase && OFS_GC){
        fprintf(OFS_GC, "%llu %d\n", (unsigned long long)GCLatency, holdsMappingData ? 1 : 0);
    }
}

// param 1. # of sectors needed for read and modify.
void Stats2::handleReadAndModify(uint32_t readSectorsRAM)
{
    if(!Simulator->loadPhase && OFS_READANDMODIFY){
        fprintf(OFS_READANDMODIFY, "%d\n", readSectorsRAM);
    }
}

void Stats2::storeSectorData(LPA_type lpa, PPA_type ppa, sim_time_type writtenTime)
{
    if(!Simulator->loadPhase && OFS_SECTOR){
        auto curSectorData = sectorDataList.find(ppa);
        if(curSectorData == sectorDataList.end()){
            curSectorData = sectorDataList.insert({ppa, SectorData(lpa, writtenTime)}).first;
        }
        curSectorData->second.sectorsCount += 1;
    }
}

void Stats2::handleSector()
{
    if(!Simulator->loadPhase && OFS_SECTOR){
        for(auto sectorData : sectorDataList){
            fprintf(OFS_SECTOR, "%llu %llu %llu %u\n", (unsigned long long)sectorData.second.lpa, (unsigned long long)sectorData.second.writtenTime, (unsigned long long)CurrentTimeStamp, 
                sectorData.second.sectorsCount);
        }
        sectorDataList.clear();
    }
}
