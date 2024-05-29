#ifndef SECTOR_LOG_H
#define SECTOR_LOG_H

#include "Flash_Block_Manager_Base.h"
#include "Address_Mapping_Unit_Page_Level.h"
#include "TSU_Base.h"
#include "BitFilter/BitFilter.h"


#define TO_FULL_PAGE(sectorsPerPage) (((uint64_t)1 << sectorsPerPage) - (uint64_t)1)

namespace SSD_Components{

    class SubPageCalculator{
    public:
        static uint32_t subPageUnit;
        static key_type makeKey(LPA_type lpa, uint32_t subPageOffset);
        static LPA_type keyToLPA(key_type key);
        static page_status_type keyToSectorsBitmap(key_type key);
    };

    class PageBuffer;
    class SectorMap;
    struct Memory_Transfer_Info;

    class SectorLog
    {
    friend class PageBuffer;
    friend class SectorMap;
    friend class BitFilter;
    private:
        static SectorLog* instance;
        PageBuffer* pageBuffer;
        SectorMap* sectorMap;

        Address_Mapping_Unit_Page_Level* amu;
        TSU_Base* tsu;
        Data_Cache_Manager_Base* dcm;

        uint32_t maxBlockSize;
        uint32_t subPagesPerPage;
        uint32_t pagesPerBlock;
        stream_id_type streamID;

        //If a new transaction has sector addr same as reading page, it would be handled after ongoing read is processed.
        
        std::unordered_map<NVM_Transaction_Flash_RD *, uint64_t> userTrBuffer;

        std::unordered_map<PPA_type, std::list<NVM_Transaction_Flash_RD*>> readingPageList;

        std::unordered_map<LPA_type, std::list<NVM_Transaction_Flash*>> lockedTr;


        BitFilter* bitFilter;

        bool readingDRAMForFlushing;

        void checkFlushIsRequired();

        void sendSubPageWriteForFlush(std::list<key_type>& subPageList);
        void sendAMUWriteForMerge(std::list<key_type>& subPageList, NVM_Transaction_Flash_ER* eraseTr);
        void sendSubPageWriteForClustering(std::list<key_type>& subPageList);
        void sendReadForMerge(std::list<PPA_type> ppaToRead, uint32_t mergeID);
        void sendReadForClustering(std::list<key_type>& subPageList);

        void userTrBufferHandler(NVM_Transaction_Flash_RD* originTr);
        void handleUserReadTr(std::list<NVM_Transaction*> transaction_list);

        void lockLPA(std::list<LPA_type>& lpaToLock);
        void unlockLPA(LPA_type lpaToUnlock);
        bool checkLPAIsLocked(LPA_type lpa);

        void sectorGroupAreaReadHandler(NVM_Transaction_Flash_RD* tr);

        static uint32_t getNextID();


    public:
        void(*dcmServicedTransactionHandler)(NVM_Transaction_Flash*);
        SectorLog(const stream_id_type in_streamID, const uint32_t in_subPagesPerPage, const uint32_t in_pagesPerBlock, const uint32_t in_maxBlockSize, const uint32_t in_maxBufferSize, const uint32_t in_subPageUnit,
        Address_Mapping_Unit_Page_Level* in_amu, TSU_Base* in_tsu, Data_Cache_Manager_Base* in_dcm, sim_time_type BF_Milestone, const uint64_t numberOfLogicalSectors);
        ~SectorLog();
        void setCompleteTrHandler(void(*transferCompletedTrToDCM)(NVM_Transaction_Flash*));
        void handleInputTransaction(std::list<NVM_Transaction*>& transaction_list);
        void servicedFromDRAMTrHandler(Memory_Transfer_Info* info);
        static void handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash* transaction);
    };
}


#endif  //  SECTOR_LOG__H