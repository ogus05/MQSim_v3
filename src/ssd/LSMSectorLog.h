#ifndef NVM_SECTORLOG_H
#define NVM_SECTORLOG_H

#include "LSMSectorMap.h"
#include "Address_Mapping_Unit_Page_Level.h"

namespace SSD_Components{
    class Memory_Transfer_Info;
    class NVM_Transaction;
    class NVM_Transaction_Flash;
    class NVM_Transaction_Flash_RD;

    class CompactionInformation{
    public:
        std::list<LSMSectorMapEntry*>* victimGroup;
        std::list<LSA_type>* dataToCompaction;
        uint32_t levelToInsert;
        uint32_t remainReadCount;
        uint32_t remainWriteCount;
        uint32_t remainEraseCount;
        CompactionInformation(std::list<LSMSectorMapEntry*>* victimGroup, std::list<LSA_type>* dataToCompaction, uint32_t levelToInsert);
        ~CompactionInformation();
    };

    class BarrierInformation{
    public:
        page_status_type compactionSectors;
        page_status_type sectorsToInvalidate;
        std::list<std::pair<page_status_type, NVM_Transaction_Flash_RD*>> pendingTr;

        BarrierInformation(page_status_type compactionSectors);
        ~BarrierInformation();
    };


    class LSMSectorLog
    {
    private:
        static LSMSectorLog* instance;

        const uint32_t pagesPerBlock;
        const uint32_t sectorsPerPage;
        const stream_id_type streamID;

        Address_Mapping_Unit_Page_Level* amu;
        Data_Cache_Manager_Base* dcm;
        TSU_Base* tsu;

        WriteBlockBuffer* writeBlockBuffer;
        ReadBlockBuffer* readBlockBuffer;

        std::vector<LSMSectorMap*> sectorMap;

        std::unordered_map<NVM_Transaction_Flash*, uint32_t> userReadTrBuffer;
        std::unordered_map<LPA_type, std::list<BarrierInformation*>> barrier;
        std::unordered_map<PPA_type, std::list<NVM_Transaction_Flash_RD*>> ppaOngoingRead;
        
        std::unordered_set<CompactionInformation*> compactionInfoList;
        void(*DCMHandleTrServicedSignal)(NVM_Transaction_Flash*);
        
        void invalidateInSectorMap(const LSA_type& lsa);
        void writeToSectorGroupArea(CompactionInformation* compactioningInfo);
        void writeToPageGroupArea(CompactionInformation* compactioningInfo);

        void checkFlushIsRequired();
        void checkCompactionIsRequired(const uint32_t level);


        std::list<LSA_type> makeSectorGroupPage(std::list<LSA_type>* remainingEntries);

        void handleCompactionIsFinished(CompactionInformation* compactioningInfo);

        void sortLSAList(std::list<LSA_type>* entries);

        void handleUserReadBuffer(NVM_Transaction_Flash_RD * transaction);
        void handleUserReadDataIsArrived(NVM_Transaction_Flash_RD * transaction);
        void handleCompactionTrIsArrived(NVM_Transaction_Flash* transaction);

        void setBarrier(LSA_type& lsa);
        void removeBarrier(LSA_type &lsa);
        page_status_type checkBehindBarrier(NVM_Transaction_Flash_RD* tr);
        void invalidateBarrierSectors(LSA_type& lsa);

    public:
        LSMSectorLog(const stream_id_type streamID, const uint32_t sectorsPerPage, const uint32_t pagesPerBlock, const uint32_t writeBlockBufferSizeInBlocks, const uint32_t readBlockBufferSizeInBlocks, 
            const std::vector<uint32_t> blocksInLevel, Address_Mapping_Unit_Page_Level* amu, TSU_Base* tsu, Data_Cache_Manager_Base* dcm);
        ~LSMSectorLog();
        void transferEraseTr(CompactionInformation* compactioningInfo);

        void connectDCMService(void(*DCMHandleTrServicedSignal)(NVM_Transaction_Flash*));

        
        void handleInputTr(std::list<NVM_Transaction *>& transaction_list);
        void handleDRAMServiced(Memory_Transfer_Info* info);

        static void handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash* tr);

        CompactionInformation* addCompactioningGroup(std::list<LSMSectorMapEntry*>* compactioningGroup, std::list<LSA_type>* lsaToRead, uint32_t levelToInsert);
    };  
}

#endif