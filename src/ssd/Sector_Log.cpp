#include "Sector_Log.h"
#include "Address_Mapping_Unit_Page_Level.h"
#include "Data_Cache_Flash.h"
#include "Page_Buffer.h"
#include "Sector_Map.h"
#include "Stats2.h"
#include "BitFilter/BitFilter.cpp"

namespace SSD_Components
{
    SectorLog* SectorLog::instance = NULL;
    SectorLog::SectorLog(const stream_id_type in_streamID, const uint32_t in_subPagesPerPage, const uint32_t in_pagesPerBlock, const uint32_t in_maxBlockSize, const uint32_t in_maxBufferSize, const uint32_t in_subPageUnit,
    Address_Mapping_Unit_Page_Level *in_amu, TSU_Base* in_tsu, Data_Cache_Manager_Base* in_dcm, sim_time_type BF_Milestone, const uint64_t numberOfLogicalSectors){
        
        SubPageCalculator::subPageUnit = in_subPageUnit;
        maxBlockSize = in_maxBlockSize;        //in_maxBlockSize;
        subPagesPerPage = in_subPagesPerPage;
        pagesPerBlock = in_pagesPerBlock;
        streamID = in_streamID;
        amu = in_amu;
        tsu = in_tsu;
        dcm = in_dcm;

        instance = this;
        sectorMap = new SectorMap(this, maxBlockSize);
        pageBuffer = new PageBuffer(in_maxBufferSize / (in_subPagesPerPage * in_subPageUnit * SECTOR_SIZE_IN_BYTE), this);

        bitFilter = new BitFilter(BF_Milestone, this);
    }

    SectorLog::~SectorLog()
    {
        delete bitFilter;
        delete sectorMap;
        delete pageBuffer;
    }

    void SectorLog::setCompleteTrHandler(void (*completeTrHandler)(NVM_Transaction_Flash *))
    {
        this->dcmServicedTransactionHandler = completeTrHandler;
    }

    page_status_type SectorLog::ExistsInSectorCache(LPA_type lpa, page_status_type sectorsBitmap)
    {
        page_status_type availableSectorsBitmap = 0;
        for(uint32_t subPageOffset = 0; subPageOffset < subPagesPerPage; subPageOffset++){
            if(SubPageCalculator::isSectorMapIncludeOffset(sectorsBitmap, subPageOffset)){
                key_type key = SubPageCalculator::makeKey(lpa, subPageOffset);
                if(pageBuffer->Exists(key, true)){
                    availableSectorsBitmap |= SubPageCalculator::keyToSectorsBitmap(key);
                }
                bitFilter->addBit(key);
            }
        }
        return availableSectorsBitmap;
    }

    void SectorLog::removeSectorGroupArea(LPA_type lpa, page_status_type sectorsBitmap)
    {
        for(uint32_t subPageOffset = 0; subPageOffset < subPagesPerPage; subPageOffset++){
            if(SubPageCalculator::isSectorMapIncludeOffset(sectorsBitmap, subPageOffset)){
                key_type key = SubPageCalculator::makeKey(lpa, subPageOffset);
                if(pageBuffer->Exists(key, 0)){
                    pageBuffer->RemoveByWrite(key);
                }

                if(sectorMap->getPageForKey(key)){
                    sectorMap->Remove(key);
                }

                bitFilter->removeBit(key);
            }
        }
    }

    bool SectorLog::insertSectorCache(NVM_Transaction_Flash_WR *tr)
    {
        if(!(bitFilter->isClusteringProcessing() && !pageBuffer->hasFreeSpace())){
            for(uint32_t subPageOffset = 0; subPageOffset < subPagesPerPage; subPageOffset++){
                if(SubPageCalculator::isSectorMapIncludeOffset(tr->write_sectors_bitmap, subPageOffset)){
                    key_type key = SubPageCalculator::makeKey(tr->LPA, subPageOffset);
                    pageBuffer->insertData(key, 1);
                }
            }
            return true;
        } else{
            return false;
        }
    }

    void SectorLog::handleReadTransaction(std::list<NVM_Transaction *> &transactionList)
    {
        if(transactionList.front()->Type != Transaction_Type::READ){
            PRINT_ERROR("Handle Read Transaction - its a list of write transactions.")
        }

        std::unordered_map<LPA_type, NVM_Transaction_Flash_RD *> transactionListForTransferAMU;
        
        std::list<NVM_Transaction_Flash*> transactionListForTransferTSU;

        for (auto it = transactionList.begin(); it != transactionList.end(); it++){
            NVM_Transaction_Flash_RD* tr = (NVM_Transaction_Flash_RD*)(*it);
            if(checkLPAIsLocked(tr->LPA)){
                lockedTr.at(tr->LPA).push_back(tr);
                continue;
            }

            userTrBuffer.insert({tr, 0});
            for(auto subPageOffset = 0; subPageOffset < subPagesPerPage; subPageOffset++){
                if(SubPageCalculator::isSectorMapIncludeOffset(tr->read_sectors_bitmap, subPageOffset)){
                    key_type key = SubPageCalculator::makeKey(tr->LPA, subPageOffset);
                    SectorMapPage* pageInSectorGroupArea =  sectorMap->getPageForKey(key);
                    
                    if(pageInSectorGroupArea != NULL){
                        if(readingSectorGroupAreaList.find(pageInSectorGroupArea->ppa) == readingSectorGroupAreaList.end()){
                            readingSectorGroupAreaList.insert({pageInSectorGroupArea->ppa, std::list<NVM_Transaction_Flash_RD*>()});
                            NVM_Transaction_Flash_RD* newTr = new NVM_Transaction_Flash_RD(Transaction_Source_Type::SECTORLOG_USER,
                                    streamID, subPagesPerPage * SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE, NO_LPA, pageInSectorGroupArea->ppa, NULL, tr->Priority_class, 0, TO_FULL_PAGE(subPagesPerPage * SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE), CurrentTimeStamp);
                            newTr->Address = amu->Convert_ppa_to_address(pageInSectorGroupArea->ppa);
                            transactionListForTransferTSU.push_back(newTr);
                            newTr->readingSubPages = pageInSectorGroupArea->storedSubPages;
                        }
                        readingSectorGroupAreaList.at(pageInSectorGroupArea->ppa).push_back(tr);
                        tr->read_sectors_bitmap &= ~SubPageCalculator::keyToSectorsBitmap(key);
                        tr->Data_and_metadata_size_in_byte -= SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE;
                        bitFilter->addBit(key);
                    } else{
                        auto curTr = transactionListForTransferAMU.find(tr->LPA);
                        if(curTr == transactionListForTransferAMU.end()){
                            curTr = transactionListForTransferAMU.insert({tr->LPA, new NVM_Transaction_Flash_RD(Transaction_Source_Type::SECTORLOG_USER,
                                streamID, 0, tr->LPA, NO_PPA, NULL, tr->Priority_class, 0, 0, CurrentTimeStamp)}).first;
                        }
                        curTr->second->Data_and_metadata_size_in_byte += SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE;
                        curTr->second->read_sectors_bitmap |= SubPageCalculator::keyToSectorsBitmap(key);
                        curTr->second->originTr.push_back(tr);
                    }
                    userTrBuffer.at(tr)++;
                }
            }

        }

        if(transactionListForTransferTSU.size() > 0){
            if(bitFilter->isClusteringProcessing()){
                bitFilter->addPendingTrListUntilClustering(transactionListForTransferTSU);
            } else{
                tsu->Prepare_for_transaction_submit();
                for(auto& tr: transactionListForTransferTSU){
                    tsu->Submit_transaction(tr);
                }
                tsu->Schedule();
            }
        }
        
        if(transactionListForTransferAMU.size() > 0){
            std::list<NVM_Transaction*> trList;
            for(auto tr : transactionListForTransferAMU){
                trList.push_back(tr.second);
            }
            amu->Translate_lpa_to_ppa_and_dispatch(trList);
        }
    }

    bool SectorLog::checkFlushIsRequired()
    {
        if(!bitFilter->isClusteringProcessing()){
            while(!pageBuffer->hasFreeSpace()){
                if(pageBuffer->isLastEntryDirty()){
                    std::list<key_type> subPageList = pageBuffer->getLastEntries(subPagesPerPage);
                    NVM_Transaction_Flash_WR *sectorGroupAreaWrite = new NVM_Transaction_Flash_WR(Transaction_Source_Type::SECTORLOG_USER,
                                                                                        streamID, subPagesPerPage * SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE, NO_LPA, NULL, IO_Flow_Priority_Class::URGENT, 0, TO_FULL_PAGE(subPagesPerPage * SubPageCalculator::subPageUnit), CurrentTimeStamp);
                    sectorMap->allocatePage(subPageList, sectorGroupAreaWrite);

                    Memory_Transfer_Info* flushTransferInfo = new Memory_Transfer_Info;
                    flushTransferInfo->Size_in_bytes = subPagesPerPage * SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE;
                    flushTransferInfo->Related_request = sectorGroupAreaWrite;
                    flushTransferInfo->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_SECTORLOG_FLUSH_FINISHED;
                    flushTransferInfo->Stream_id = streamID;
                    dcm->service_dram_access_request(flushTransferInfo);
                    return true;
                } else{
                    pageBuffer->RemoveLastEntry();
                }
            }
        }
        return false;
    }

    void SectorLog::sendReadForMerge(std::list<PPA_type> ppaToRead, uint32_t mergeID)
    {
        tsu->Prepare_for_transaction_submit();
        for (auto &ppa : ppaToRead)
        {
            NVM_Transaction_Flash_RD *readSectorAreaTr = new NVM_Transaction_Flash_RD(Transaction_Source_Type::SECTORLOG_MERGE, streamID,
                                                                                    subPagesPerPage * SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE, NO_LPA, ppa, NULL, 0, TO_FULL_PAGE(subPagesPerPage * SubPageCalculator::subPageUnit), CurrentTimeStamp);
            readSectorAreaTr->mergeID = mergeID;
            readSectorAreaTr->Address = amu->Convert_ppa_to_address(ppa);
            tsu->Submit_transaction(readSectorAreaTr);
        }
        tsu->Schedule();
    }

    void SectorLog::sendAMUWriteForMerge(std::list<key_type>& subPageList, NVM_Transaction_Flash_ER* eraseTr)
    {
        std::list<NVM_Transaction*> transferToAMUList;
        // If the sectors in the sector area can be merged to the full page, transferred to the Address Mapping Unit.

        std::unordered_map<LPA_type, page_status_type> lsaToWrite;

        for(auto& key : subPageList){
            lsaToWrite[SubPageCalculator::keyToLPA(key)] |= SubPageCalculator::keyToSectorsBitmap(key);
        }

        for(auto & lsa : lsaToWrite){
            bool b = true;
            NVM_Transaction_Flash_WR *pageWriteTr = new NVM_Transaction_Flash_WR(
                    Transaction_Source_Type::SECTORLOG_MERGE, streamID, count_sector_no_from_status_bitmap(lsa.second) * SECTOR_SIZE_IN_BYTE, lsa.first, NULL, 0, lsa.second, CurrentTimeStamp);
            eraseTr->Page_movement_activities.push_back(pageWriteTr);
            pageWriteTr->RelatedErase = eraseTr;
            transferToAMUList.push_back(pageWriteTr);
        }  

        if(transferToAMUList.size() > 0){
            amu->Translate_lpa_to_ppa_and_dispatch(transferToAMUList);
        } else{
            tsu->Prepare_for_transaction_submit();
            tsu->Submit_transaction(eraseTr);
            tsu->Schedule();
        }
    }

    void SectorLog::sendReadForClustering(std::list<key_type>& subPageList)
    {
        std::unordered_map<PPA_type, NVM_Transaction_Flash_RD*> trListForTransferTSU;
        uint32_t DRAMReadSize = 0;
        uint32_t totalReadCount = 0;
        for(auto key : subPageList){
            if(pageBuffer->Exists(key, false)){
                if(pageBuffer->isDirty(key)){
                    DRAMReadSize += SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE;
                } else{
                    if(sectorMap->getPageForKey(key) == NULL){
                        bitFilter->removeBit(key);
                    }
                }
            } else{
                SectorMapPage* pageInSectorGroupArea = sectorMap->getPageForKey(key);
                if(pageInSectorGroupArea != NULL){
                    NVM_Transaction_Flash_RD* curTr = NULL;
                    if(trListForTransferTSU.find(pageInSectorGroupArea->ppa) == trListForTransferTSU.end()){
                        NVM_Transaction_Flash_RD* newTr = new NVM_Transaction_Flash_RD(Transaction_Source_Type::SECTORLOG_CLUSTER,
                                        streamID, 0, NO_LPA, pageInSectorGroupArea->ppa, NULL, IO_Flow_Priority_Class::URGENT, 0, 0, CurrentTimeStamp);
                        newTr->Address = amu->Convert_ppa_to_address(pageInSectorGroupArea->ppa);
                        trListForTransferTSU.insert({pageInSectorGroupArea->ppa, newTr});
                        curTr = newTr;
                    } else{
                        curTr = trListForTransferTSU.at(pageInSectorGroupArea->ppa);
                    }
                    curTr->Data_and_metadata_size_in_byte += SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE;
                    curTr->read_sectors_bitmap = (curTr->read_sectors_bitmap << (page_status_type)SubPageCalculator::subPageUnit) | 
                            ((page_status_type)1 << SubPageCalculator::subPageUnit) - (page_status_type)1;
                } else{
                    bitFilter->removeBit(key);
                }
            }
        }

        if(totalReadCount >= subPagesPerPage){
            if(DRAMReadSize > 0){
                totalReadCount += 1;
                Memory_Transfer_Info* readTransferInfo = new Memory_Transfer_Info;
                readTransferInfo->Size_in_bytes = DRAMReadSize;
                readTransferInfo->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_SECTORLOG_CLUSTERING_FINISHED;
                readTransferInfo->Stream_id = streamID;
                dcm->service_dram_access_request(readTransferInfo);
            }

            if(trListForTransferTSU.size() > 0){
                totalReadCount += trListForTransferTSU.size();
                tsu->Prepare_for_transaction_submit();
                for(auto& tr : trListForTransferTSU){
                    tsu->Submit_transaction(tr.second);
                }
                tsu->Schedule();
            }
            bitFilter->setRemainRead(totalReadCount);
        } else{
            bitFilter->endClustering();
        }
    }

    void SectorLog::sendSubPageWriteForClustering(std::list<key_type> &subPageList)
    {
        for(key_type key : subPageList){
            if(pageBuffer->Exists(key, false) && pageBuffer->isDirty(key)){
                pageBuffer->setClean(key);
            } else if(sectorMap->getPageForKey(key) != NULL){
                sectorMap->Remove(key);
            } else{
                PRINT_ERROR("ERROR IN SUB PAGE WRITE FOR CLUSTERING : " << key)
            }
        }
        NVM_Transaction_Flash_WR *sectorGroupAreaWrite = new NVM_Transaction_Flash_WR(Transaction_Source_Type::SECTORLOG_CLUSTER,
                                                                                        streamID, subPagesPerPage * SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE, NO_LPA, NULL, IO_Flow_Priority_Class::URGENT, 0, TO_FULL_PAGE(subPagesPerPage * SubPageCalculator::subPageUnit), CurrentTimeStamp);
        sectorMap->allocatePage(subPageList, sectorGroupAreaWrite);
        tsu->Prepare_for_transaction_submit();
        tsu->Submit_transaction(sectorGroupAreaWrite);
        tsu->Schedule();
    }




    void SectorLog::userTrBufferHandler(NVM_Transaction_Flash_RD* originTr)
    {
        auto itr = userTrBuffer.find(originTr);
        if(itr == userTrBuffer.end()){
            PRINT_ERROR("USER TRANSACTION BUFFER HANDLER - NO EXISTS TRANSACTION")
        }
        itr->second--;
        
        if(itr->second < 0){
            PRINT_ERROR("USER TRANSACTION BUFFER HANDLER - ERROR IN REMAIN TRANSACTIONS")
        } 
        if(itr->second == 0){
            userTrBuffer.erase(itr);
            dcmServicedTransactionHandler(originTr);
            delete originTr;
        }
    }

    void SectorLog::lockLPA(std::list<LPA_type>& lpaToLock)
    {
        for(auto lpa : lpaToLock){
            lockedTr.insert({lpa, std::list<NVM_Transaction_Flash*>()});
        }
    }

    void SectorLog::unlockLPA(LPA_type lpaToUnlock)
    {
        auto trList = lockedTr.find(lpaToUnlock)->second;
        for(auto curTr : trList){
            dcmServicedTransactionHandler(curTr);
            delete curTr;
        }
        lockedTr.erase(lpaToUnlock);

    }

    bool SectorLog::checkLPAIsLocked(LPA_type lpa)
    {
        auto lockedTrList = lockedTr.find(lpa);
        return lockedTrList != lockedTr.end();
    }

    void SectorLog::sectorGroupAreaReadHandler(NVM_Transaction_Flash_RD* tr)
    {
        std::list<key_type> subPageList = tr->readingSubPages;
        for(auto key : subPageList){
            if(sectorMap->getPageForKey(key) != NULL){
                pageBuffer->insertData(key, 0);
            }
        }

        if(checkFlushIsRequired()){
            ((Data_Cache_Manager_Flash_Advanced*)(dcm))->AddBackPressureBufferDepth(tr->Stream_id, subPagesPerPage * SubPageCalculator::subPageUnit);
        }

        for(auto originTr : readingSectorGroupAreaList.at(tr->PPA)){
            userTrBufferHandler(originTr);
        }

        readingSectorGroupAreaList.erase(tr->PPA);
    }

    void SectorLog::servicedFromDRAMTrHandler(Memory_Transfer_Info *info)
    {
        if(maxBlockSize == 0){
            PRINT_ERROR("ERROR IN SECTOR LOG : SERVICE DRAM")
        }
        switch(info->next_event_type){
        case Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_SECTORLOG_FLUSH_FINISHED:{
            tsu->Prepare_for_transaction_submit();
            tsu->Submit_transaction((NVM_Transaction_Flash*)(info->Related_request));
            tsu->Schedule();
        } break;
        case Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_SECTORLOG_CLUSTERING_FINISHED:{
            bitFilter->handleClusteringReadIsArrived();
        } break;
        }
    }
    
    void SectorLog::handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash *transaction)
    {
        if(transaction->Type == Transaction_Type::READ){
            if (((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite != NULL) {
                ((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->RelatedRead = NULL;
                return;
            }
        }
        if (transaction->Source == Transaction_Source_Type::SECTORLOG_USER)
        {
            switch (transaction->Type) {
            case Transaction_Type::READ: {
                if(((NVM_Transaction_Flash_RD*)transaction)->LPA == NO_LPA){
                    instance->sectorGroupAreaReadHandler(((NVM_Transaction_Flash_RD*)transaction));
                } else{
                    for(auto tr : ((NVM_Transaction_Flash_RD*)transaction)->originTr){
                        instance->userTrBufferHandler(tr);
                    }
                }
            } break;

            default: PRINT_ERROR("ERROR IN SECTOR LOG HANDLE TRANSACTION : 2"); break;
            }
        }
        else if (transaction->Source == Transaction_Source_Type::SECTORLOG_MERGE) {
            switch (transaction->Type) {
                //Read data related to the victim block before the merge process is started.
                case Transaction_Type::READ: instance->sectorMap->handleMergeReadArrived(((NVM_Transaction_Flash_RD*)transaction)->mergeID); break;

                //Write the related blocks.
                case Transaction_Type::WRITE: {
                    instance->unlockLPA(transaction->LPA);
                    ((NVM_Transaction_Flash_WR *)transaction)->RelatedErase->Page_movement_activities.remove((NVM_Transaction_Flash_WR *)transaction);
                    if(((NVM_Transaction_Flash_WR *)transaction)->RelatedErase->Page_movement_activities.size() == 0){
                        instance->tsu->Prepare_for_transaction_submit();
                        instance->tsu->Submit_transaction(((NVM_Transaction_Flash_WR *)transaction)->RelatedErase);
                        instance->tsu->Schedule();
                    }
                }
                break;
                
                //Merge process is completed.
                case Transaction_Type::ERASE: instance->sectorMap->eraseVictimBlock(transaction->mergeID); break;
                default:
                {
                    PRINT_ERROR("ERROR IN SECTOR LOG HANDLE TRANSACTION : 2");
                };
            }
        }
        else if (transaction->Source == Transaction_Source_Type::SECTORLOG_CLUSTER)
        {
            switch (transaction->Type)
            {
                case Transaction_Type::READ: instance->bitFilter->handleClusteringReadIsArrived(); break;

                case Transaction_Type::WRITE: instance->bitFilter->handleClusteringWriteIsArrived(); break;
                
                default: PRINT_ERROR("ERROR IN SECTOR LOG HANDLE")
            }
        }
    }

    uint32_t SectorLog::getNextID()
    {
        static int ID = 1;
        if(ID == 100000000){
            ID = 1;
        }
        return ID++;
    }

    uint32_t SubPageCalculator::subPageUnit = 0;
    key_type SubPageCalculator::makeKey(LPA_type lpa, uint32_t subPageOffset)
    {
        return (lpa * subPageUnit + subPageOffset);
    }
    LPA_type SubPageCalculator::keyToLPA(key_type key)
    {
        return (key / subPageUnit);
    }
    page_status_type SubPageCalculator::keyToSectorsBitmap(key_type key)
    {
        uint32_t subPageOffset = key % subPageUnit;
        return ((page_status_type)1 << ((subPageOffset + 1) * subPageUnit)) - ((page_status_type)1 << ((subPageOffset) * subPageUnit));
    }
    bool SubPageCalculator::isSectorMapIncludeOffset(page_status_type sectorsBitmap, uint32_t subPageOffset)
    {

    return ((sectorsBitmap & ((page_status_type)1 << (subPageOffset * subPageUnit))) > 0) ? true : false;
    }
}