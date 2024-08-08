#ifndef SECTOR_MAP_H
#define SECTOR_MAP_H

#include "Sector_Log.h"

namespace SSD_Components{

    class MergingEntry{
    private:
    public:
        PPA_type blockAddr;
        std::list<key_type>* mergingKeyList;
        uint64_t remainReadCount;
        uint64_t remainWriteCount;
        MergingEntry(PPA_type in_blockAddr, std::list<key_type>* in_mergingKeyList);
    };
    
    class SectorMapPage{
    private:
    public:
        PPA_type ppa;
        sim_time_type writtenTime;

        std::list<SectorMapPage*>::iterator list_itr;

        std::list<key_type> storedSubPages;

        SectorMapPage(const PPA_type& in_ppa);
    };

    class SectorMap{
    private:
        SectorLog* sectorLog;
        std::unordered_map<key_type, SectorMapPage*> mapTable;

        std::unordered_map<PPA_type, std::list<SectorMapPage*>*> physicalBlockTable;

        std::unordered_map<PPA_type, MergingEntry*> mergingEntryList;
        uint32_t maxBlockSize;

        void setMapTable(std::list<key_type>& subPagesList, SectorMapPage* mapEntry);
        void addPhysicalBlockTable(SectorMapPage* pageEntry);
        PPA_type getMergeBlock(std::vector<PPA_type>& sectorLogBlockList);
        void Merge(PPA_type blockAddr);

        void erasePhysicalBlockTableEntry(SectorMapPage* pageEntry);

    public:
        SectorMap(SectorLog* in_sectorLog, uint32_t in_maxBlockSize)
            :sectorLog(in_sectorLog), maxBlockSize(in_maxBlockSize) {};
        ~SectorMap();
        SectorMapPage* getPageForKey(key_type key);
        void allocatePage(std::list<key_type>& subPagesList, NVM_Transaction_Flash_WR *transaction);
        void Remove(key_type key);

        void handleMergeReadArrived(PPA_type blockAddr);
        void eraseVictimBlock(PPA_type blockAddr);

        bool checkMergeIsRequired();

    };
}

#endif  //  SECTOR_MAP__H