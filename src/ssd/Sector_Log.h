#ifndef SECTOR_LOG_H
#define SECTOR_LOG_H

#include "Flash_Block_Manager_Base.h"
#include "Address_Mapping_Unit_Page_Level.h"
#include "TSU_Base.h"
#include "BitFilter/LogBF.h"


#define TO_FULL_PAGE(sectorsPerPage) (((uint64_t)1 << sectorsPerPage) - (uint64_t)1)

namespace SSD_Components{

    class Page_Buffer;
    class Sector_Map;
    struct Memory_Transfer_Info;

    class Sector_Log
    {
    friend class Page_Buffer;
    friend class Sector_Map;
    friend class BitFilter;
    private:
        static Sector_Log* instance;
        Page_Buffer* pageBuffer;
        Sector_Map* sectorMap;

        Address_Mapping_Unit_Page_Level* amu;
        TSU_Base* tsu;
        Data_Cache_Manager_Base* dcm;

        uint32_t maxBlockSize;
        uint32_t sectorsPerPage;
        uint32_t pagesPerBlock;
        stream_id_type streamID;

        //If a new transaction has sector addr same as reading page, it would be handled after ongoing read is processed.
        
        std::unordered_map<NVM_Transaction_Flash_RD *, uint64_t> userTrBuffer;

        std::unordered_map<LPA_type, std::list<NVM_Transaction_Flash*>> lockedTr;

        LogBF* logBF;

        bool readingDRAMForFlushing;

        void checkFlushIsRequired();

        void sendSubPageWriteForFlush(std::list<std::pair<LPA_type, page_status_type>>& sectorsList, uint32_t flushingID);
        void sendAMUWriteForMerge(std::list<std::pair<LPA_type, page_status_type>>& sectorsList, NVM_Transaction_Flash_ER* eraseTr);
        void sendReadForMerge(std::list<PPA_type> ppaToRead, uint32_t mergeID);

        void userTrBufferHandler(std::list<NVM_Transaction_Flash_RD*> originTr);
        void handleUserReadTr(std::list<NVM_Transaction*> transaction_list);

        void lockLPA(std::list<LPA_type>& lpaToLock);
        void unlockLPA(LPA_type lpaToUnlock);
        bool handleIsLockedLPA(NVM_Transaction_Flash* tr);


        static uint32_t getNextID();


    public:
        void(*dcmServicedTransactionHandler)(NVM_Transaction_Flash*);
        Sector_Log(const stream_id_type& in_streamID, const uint32_t& in_sectorsPerPage, const uint32_t& in_pagesPerBlock, const uint32_t& in_maxBlockSize,
        Address_Mapping_Unit_Page_Level* in_amu, TSU_Base* in_tsu, Data_Cache_Manager_Base* in_dcm, sim_time_type BF_Milestone, uint64_t numberOfLogicalSectors);
        ~Sector_Log();
        void setCompleteTrHandler(void(*transferCompletedTrToDCM)(NVM_Transaction_Flash*));
        void handleInputTransaction(std::list<NVM_Transaction*> transaction_list);
        void servicedFromDRAMTrHandler(Memory_Transfer_Info* info);
        static void handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash* transaction);
        std::vector<uint32_t> convertBitmapToSectorLocation(const page_status_type& sectorsBitmap);
    };
}


#endif  //  SECTOR_LOG__H