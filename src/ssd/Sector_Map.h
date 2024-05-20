#ifndef SECTOR_MAP_H
#define SECTOR_MAP_H

#include "Sector_Log.h"

namespace SSD_Components{
    
    class Sector_Map_Entry;
    class SectorMapBlock{
    private:
    public:
        NVM::FlashMemory::Physical_Page_Address* blockAddr;
        PlaneBookKeepingType* planeRecord;
        Block_Pool_Slot_Type* blockRecord;
        
        uint64_t remainReadCountForMerge;

        std::list<Sector_Map_Entry*> entryList;

        bool ongoingMerge;

        uint32_t mergeID;

        std::unordered_map<LPA_type, page_status_type> mergingLSAList;

        void setTrAddr(NVM_Transaction_Flash_WR* tr);

        SectorMapBlock(NVM::FlashMemory::Physical_Page_Address* in_blockAddr, PlaneBookKeepingType* in_planeRecord,
            Block_Pool_Slot_Type* in_blockRecord);

        ~SectorMapBlock();
        
    };

    class Sector_Map_Entry{
    private:
    public:
        PPA_type ppa;
        sim_time_type writtenTime;

        std::list<Sector_Map_Entry*>::iterator list_itr;

        SectorMapBlock* block;

        std::list<std::pair<LPA_type, page_status_type>> storedSectors;

        Sector_Map_Entry(const PPA_type& in_ppa, SectorMapBlock* in_block);
    };

    class Sector_Map{
    private:
        Sector_Log* sectorLog;
        std::unordered_map<LHA_type, Sector_Map_Entry*> mapTable;

        std::list<SectorMapBlock*> sectorMapBlockList;

        uint32_t maxBlockSize;

        void setMapTable(std::list<std::pair<LPA_type, page_status_type>>& sectorsList, Sector_Map_Entry* mapEntry);
    
        void checkMergeIsRequired();
        void Merge(uint32_t mergeID);



    public:
        Sector_Map(Sector_Log* in_sectorLog, uint32_t in_maxBlockSize)
            :sectorLog(in_sectorLog), maxBlockSize(in_maxBlockSize) {};
        ~Sector_Map();
        std::vector<PPA_type>* getAllRelatedPPAsInLPA(const LPA_type& lpa);
        void allocateAddr(std::list<std::pair<LPA_type, page_status_type>>& sectorsList, NVM_Transaction_Flash_WR *transaction);
        void Remove(const LPA_type& lpa, const page_status_type& sectors);

        void handleMergeReadArrived(uint32_t mergeID);
        void eraseVictimBlock(uint32_t mergeID);

        void createNewBlock();

    };
}

#endif  //  SECTOR_MAP__H