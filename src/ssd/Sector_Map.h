#ifndef SECTOR_MAP_H
#define SECTOR_MAP_H

#include "Sector_Log.h"

namespace SSD_Components{
    
    class SectorMapPage;
    class SectorMapBlock{
    private:
    public:
        NVM::FlashMemory::Physical_Page_Address* blockAddr;
        PlaneBookKeepingType* planeRecord;
        Block_Pool_Slot_Type* blockRecord;
        uint64_t remainReadCountForMerge;

        std::list<SectorMapPage*> pageList;

        bool ongoingMerge;

        uint32_t mergeID;

        std::list<key_type> mergingKeyList;

        void setTrAddr(NVM_Transaction_Flash_WR* tr);

        SectorMapBlock(NVM::FlashMemory::Physical_Page_Address* in_blockAddr, PlaneBookKeepingType* in_planeRecord,
            Block_Pool_Slot_Type* in_blockRecord);

        ~SectorMapBlock();
        
    };

    class SectorMapPage{
    private:
    public:
        PPA_type ppa;
        sim_time_type writtenTime;

        std::list<SectorMapPage*>::iterator list_itr;

        SectorMapBlock* block;

        std::list<key_type> storedSubPages;

        SectorMapPage(const PPA_type& in_ppa, SectorMapBlock* in_block);
    };

    class SectorMap{
    private:
        SectorLog* sectorLog;
        std::unordered_map<key_type, SectorMapPage*> mapTable;

        std::list<SectorMapBlock*> sectorMapBlockList;

        uint32_t maxBlockSize;

        void setMapTable(std::list<key_type>& subPagesList, SectorMapPage* mapEntry);
    
        void Merge(uint32_t mergeID);

    public:
        SectorMap(SectorLog* in_sectorLog, uint32_t in_maxBlockSize)
            :sectorLog(in_sectorLog), maxBlockSize(in_maxBlockSize) {};
        ~SectorMap();
        SectorMapPage* getPageForKey(key_type key);
        void allocatePage(std::list<key_type>& subPagesList, NVM_Transaction_Flash_WR *transaction);
        void Remove(key_type key);

        void handleMergeReadArrived(uint32_t mergeID);
        void eraseVictimBlock(uint32_t mergeID);

        void createNewBlock();
        void checkMergeIsRequired();

    };
}

#endif  //  SECTOR_MAP__H