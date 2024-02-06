#ifndef SECTOR_LOG_H
#define SECTOR_LOG_H

#include "Flash_Block_Manager_Base.h"


#define TO_FULL_PAGE(sectorsPerPage) (((uint64_t)1 << sectorsPerPage) - (uint64_t)1)

namespace SSD_Components{

    class Sector_Log;
    class Sector_Log_WF_Entry;
    class Address_Mapping_Unit_Page_Level;
    class TSU_Base;
    struct Memory_Transfer_Info;

    
    class Page_Buffer_Entry{
    private:    
    public:
        page_status_type sectorsBitmap;

    Page_Buffer_Entry(const page_status_type& in_sectorsBitmap)
        :sectorsBitmap(in_sectorsBitmap) {};
    };

    class Page_Buffer{
    private:
        std::unordered_map<LPA_type, Page_Buffer_Entry*> entry;

        //Indicates free size of the Page Buffer.
        uint32_t remainBufferSizeInSectors;

        Sector_Log* sectorLog;
    public:
        Page_Buffer(const uint32_t page_size_in_sectors, Sector_Log* sector_log)
            :remainBufferSizeInSectors(page_size_in_sectors), sectorLog(sector_log) {}; 
        ~Page_Buffer();
        uint64_t Exists(const LPA_type& lpa);
        void Insert(const LPA_type& lpa, const page_status_type& sectorsBitmap, page_status_type& remainSectorsBitmap);
        void Remove(const LPA_type& lpa, const page_status_type& sectorsBitmap);
        void RemoveAll();

        const std::unordered_map<LPA_type, Page_Buffer_Entry*> GetAll();
    };

    class Sector_Map_Entry{
    private:
    public:
        PPA_type ppa;
        Sector_Log_WF_Entry* storedBlock;

        Sector_Map_Entry();
    };

    class Sector_Map{
    private:
        Sector_Log* sectorLog;
        std::unordered_map<LPA_type, std::vector<Sector_Map_Entry>*> entry;
    public:
        Sector_Map(Sector_Log* in_sectorLog)
            :sectorLog(in_sectorLog){};
        ~Sector_Map();
        std::vector<Sector_Map_Entry>* getAllRelatedPPAsInLPA(const LPA_type& lpa);
        void Insert(const LPA_type& lpa, const uint32_t& sector, const PPA_type& ppa, Sector_Log_WF_Entry* WFEntry);
        void Remove(const LPA_type& lpa);
    };

    class Sector_Log_WF_Entry{
    private:
    public:
        uint64_t remainReadCountForMerge;
        std::unordered_map<LPA_type, uint32_t> storeSectors;
        std::unordered_map<LPA_type, page_status_type> LSAOnMerge;

        bool prepareMerge;
        bool ongoingMerge;

        NVM::FlashMemory::Physical_Page_Address* blockAddr;
        PlaneBookKeepingType* planeRecord;
        Block_Pool_Slot_Type* blockRecord;

        Sector_Log_WF_Entry(NVM::FlashMemory::Physical_Page_Address* in_blockAddr, PlaneBookKeepingType* in_planeRecord,
        Block_Pool_Slot_Type* in_blockRecord);
        
    };

    class Sector_Log
    {
    friend class Page_Buffer;
    friend class Sector_Map;
    private:
        static Sector_Log* instance;
        Page_Buffer* pageBuffer;
        Sector_Map* sectorMap;
        std::list<Sector_Log_WF_Entry*> sectorLogWF;

        Address_Mapping_Unit_Page_Level* amu;
        TSU_Base* tsu;
        Data_Cache_Manager_Base* dcm;


        uint32_t maxBlockSize;
        uint32_t sectorsPerPage;
        uint32_t pagesPerBlock;
        stream_id_type streamID;

        //If a new transaction has sector addr same as reading page, it would be handled after ongoing read is processed.
        std::unordered_map<PPA_type, std::list<NVM_Transaction_Flash_RD*>*> waitingTrPrepareMerge;
        std::list<std::pair<NVM_Transaction_Flash_WR *, page_status_type>> waitingTrPageBufferFreeSpace;
        std::unordered_map<NVM_Transaction_Flash_RD *, uint64_t> userTrBuffer;

        bool ongoingFlush;

        void allocate_page_in_sector_area(NVM_Transaction_Flash_WR* transaction);

        void checkMergeIsRequired();
        void Merge(Sector_Log_WF_Entry* in_entry);

        void flushPageBuffer();
        void flushTrServicedHandler(const PPA_type& ppa);


        void eraseSectorLogWFEntry(Sector_Log_WF_Entry* entryToRemove);

        void waitingPrepareMergeTrHandler(const PPA_type& ppa);
        void waitingPageBufferFreeSpaceTrHandler();
        void userTrBufferHandler(NVM_Transaction_Flash_RD* transaction);
    public:
        void(*dcmServicedTransactionHandler)(NVM_Transaction_Flash*);
        Sector_Log(const stream_id_type& in_streamID, const uint32_t& in_sectorsPerPage, const uint32_t& in_pagesPerBlock, const uint32_t& in_maxBlockSize,
        Address_Mapping_Unit_Page_Level* in_amu, TSU_Base* in_tsu, Data_Cache_Manager_Base* in_dcm);
        ~Sector_Log();
        void setCompleteTrHandler(void(*transferCompletedTrToDCM)(NVM_Transaction_Flash*));
        void handleInputTransaction(std::list<NVM_Transaction*> transaction_list);
        void servicedFromDRAMTrHandler(Memory_Transfer_Info* info);
        static void handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash* transaction);
    };
}


#endif  //  SECTOR_LOG__H