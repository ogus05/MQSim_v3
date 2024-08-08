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
    SectorLog::SectorLog(const stream_id_type in_streamID, const uint32_t in_subPagesPerPage, const uint32_t in_pagesPerBlock, const uint32_t in_maxBlockSize, const uint32_t in_sectorCacheCapacity, const uint32_t in_subPageUnit,
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
        pageBuffer = new PageBuffer(in_sectorCacheCapacity / (in_subPageUnit * SECTOR_SIZE_IN_BYTE));
        bitFilter = new BitFilter(BF_Milestone, this);
    }

    SectorLog::~SectorLog()
    {
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
            }
        }
        return availableSectorsBitmap;
    }

    void SectorLog::removeSectorGroupArea(LPA_type lpa, page_status_type sectorsBitmap)
    {
        for(uint32_t subPageOffset = 0; subPageOffset < subPagesPerPage; subPageOffset++){
            if(SubPageCalculator::isSectorMapIncludeOffset(sectorsBitmap, subPageOffset)){
                key_type key = SubPageCalculator::makeKey(lpa, subPageOffset);
                if(pageBuffer->Exists(key, false)){
                    pageBuffer->RemoveByWrite(key);
                    bitFilter->removeKey(key);
                }

                if(sectorMap->getPageForKey(key)){
                    sectorMap->Remove(key);
                    bitFilter->removeKey(key);
                }

            }
        }
    }

    bool SectorLog::insertSectorCache(NVM_Transaction_Flash_WR *tr)
    {
        if(bitFilter->isClusteringProcessing()){
            return false;
        }
        
        for(uint32_t subPageOffset = 0; subPageOffset < subPagesPerPage; subPageOffset++){
            if(SubPageCalculator::isSectorMapIncludeOffset(tr->write_sectors_bitmap, subPageOffset)){
                key_type key = SubPageCalculator::makeKey(tr->LPA, subPageOffset);
                if(pageBuffer->Exists(key, 1)){
                    pageBuffer->updateData(key, 1);
                } else{
                    pageBuffer->insertData(key, 1);
                }
            }
        }
        return true;
    }

    void SectorLog::queryReadTrList(std::list<NVM_Transaction *> &transactionList)
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
                        bitFilter->addKey(key);
                    } else{
                        auto curTr = transactionListForTransferAMU.find(tr->LPA);
                        if(curTr == transactionListForTransferAMU.end()){
                            curTr = transactionListForTransferAMU.insert({tr->LPA, new NVM_Transaction_Flash_RD(Transaction_Source_Type::SECTORLOG_USER,
                                streamID, 0, tr->LPA, NO_PPA, NULL, tr->Priority_class, tr->Content, 0, tr->DataTimeStamp)}).first;
                            curTr->second->originTr.clear();
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
                pendingReadTrListWhileClustering.push_back(new std::list<NVM_Transaction_Flash*>(transactionListForTransferTSU));
            } else{
                tsu->Prepare_for_transaction_submit();
                for(auto tr: transactionListForTransferTSU){
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

    void SectorLog::addPendingWriteReqListWhileClustering(User_Request *req)
    {
        pendingWriteReqListWhileClustering.insert(req);
    }

    bool SectorLog::isPendingWriteReq(User_Request *req)
    {
        return (pendingWriteReqListWhileClustering.find(req) != pendingWriteReqListWhileClustering.end());
    }

    void SectorLog::handleWaitingReqsWhileClustering()
    {
        for(auto pendingReadTrList : pendingReadTrListWhileClustering){
            tsu->Prepare_for_transaction_submit();
            for(auto tr : (*pendingReadTrList)){
                tsu->Submit_transaction(tr);
            }
            tsu->Schedule();
            delete pendingReadTrList;
        }
        pendingReadTrListWhileClustering.clear();
        

        for(auto pendingWriteReqItr = pendingWriteReqListWhileClustering.begin(); pendingWriteReqItr != pendingWriteReqListWhileClustering.end();){
            User_Request* req = (*pendingWriteReqItr);
            pendingWriteReqListWhileClustering.erase(pendingWriteReqItr++);
            ((Data_Cache_Manager_Flash_Advanced*)dcm)->process_new_user_request(req);
        }

        while(sectorMap->checkMergeIsRequired()){}
    }

    NVM_Transaction_Flash_WR *SectorLog::getFlushTransaction()
    {
        std::list<key_type> subPageList = pageBuffer->evictLastEntries(subPagesPerPage);
        NVM_Transaction_Flash_WR *sectorGroupAreaWrite = new NVM_Transaction_Flash_WR(Transaction_Source_Type::SECTORLOG_USER,
                                                                            streamID, subPagesPerPage * SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE, NO_LPA, NULL, IO_Flow_Priority_Class::URGENT, 0, TO_FULL_PAGE(subPagesPerPage * SubPageCalculator::subPageUnit), CurrentTimeStamp);
        sectorMap->allocatePage(subPageList, sectorGroupAreaWrite);
        return sectorGroupAreaWrite;
    }

    bool SectorLog::checkFlushIsRequired(uint32_t sizeToWriteInSectors)
    {
        while(pageBuffer->getFreeSpace() < (sizeToWriteInSectors / SubPageCalculator::subPageUnit)){
            if(pageBuffer->isLastEntryDirty()){
                Stats2::addFlushCount();
                return true;
            } else{
                key_type keyOfLastEntry = pageBuffer->RemoveLastEntry();
                if(sectorMap->getPageForKey(keyOfLastEntry) == NULL){
                    bitFilter->removeKey(keyOfLastEntry);
                }
            }
        }
        return false;
    }

    void SectorLog::sendTSUReadForMerge(std::list<PPA_type> ppaToRead)
    {
        tsu->Prepare_for_transaction_submit();
        for (auto &ppa : ppaToRead)
        {
            NVM_Transaction_Flash_RD *readSectorAreaTr = new NVM_Transaction_Flash_RD(Transaction_Source_Type::SECTORLOG_MERGE, streamID,
                                                                                    subPagesPerPage * SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE, NO_LPA, ppa, NULL, 0, TO_FULL_PAGE(subPagesPerPage * SubPageCalculator::subPageUnit), CurrentTimeStamp);
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
                DRAMReadSize += SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE;
            } else{
                SectorMapPage* pageInSectorGroupArea = sectorMap->getPageForKey(key);
                if(pageInSectorGroupArea != NULL){
                    NVM_Transaction_Flash_RD* curTr = NULL;
                    if(trListForTransferTSU.find(pageInSectorGroupArea->ppa) == trListForTransferTSU.end()){
                        curTr = new NVM_Transaction_Flash_RD(Transaction_Source_Type::SECTORLOG_CLUSTER,
                                        streamID, 0, NO_LPA, pageInSectorGroupArea->ppa, NULL, IO_Flow_Priority_Class::URGENT, 0, 0, CurrentTimeStamp);
                        curTr->Address = amu->Convert_ppa_to_address(pageInSectorGroupArea->ppa);
                        trListForTransferTSU.insert({pageInSectorGroupArea->ppa, curTr});
                    } else{
                        curTr = trListForTransferTSU.at(pageInSectorGroupArea->ppa);
                    }
                    curTr->Data_and_metadata_size_in_byte += SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE;
                    curTr->read_sectors_bitmap = (curTr->read_sectors_bitmap << (page_status_type)SubPageCalculator::subPageUnit) | 
                            ((page_status_type)1 << SubPageCalculator::subPageUnit) - (page_status_type)1;
                } else{
                    PRINT_ERROR("SEND READ FOR CLUSTERING : THERE ARE NO KEY - " << key)
                }
            }
        }

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
    }

    void SectorLog::sendSubPageWriteForClustering(std::list<SubPageCluster*>& subPageList)
    {
        tsu->Prepare_for_transaction_submit();
        for(auto subPage : subPageList){
            for(key_type key : subPage->clusteredSectors){
                if(pageBuffer->Exists(key, false)){
                    if(pageBuffer->isDirty(key)){
                        pageBuffer->setClean(key);
                    }
                } else if(sectorMap->getPageForKey(key) != NULL){
                    sectorMap->Remove(key);
                } else{
                    PRINT_ERROR("ERROR IN SUB PAGE WRITE FOR CLUSTERING : " << key)
                }
            }
            NVM_Transaction_Flash_WR *sectorGroupAreaWrite = new NVM_Transaction_Flash_WR(Transaction_Source_Type::SECTORLOG_CLUSTER,
                                                                                            streamID, subPagesPerPage * SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE, NO_LPA, NULL, IO_Flow_Priority_Class::URGENT, 0, TO_FULL_PAGE(subPagesPerPage * SubPageCalculator::subPageUnit), CurrentTimeStamp);
            sectorMap->allocatePage(subPage->clusteredSectors, sectorGroupAreaWrite);
            tsu->Submit_transaction(sectorGroupAreaWrite);

            delete subPage;
        }
        tsu->Schedule();
    }

    void SectorLog::userTrBufferHandler(NVM_Transaction_Flash_RD* originTr)
    {
        auto itr = userTrBuffer.find(originTr);
        if(itr == userTrBuffer.end()){
            PRINT_ERROR("USER TRANSACTION BUFFER HANDLER - NO EXISTS TRANSACTION")
        } else if(itr->second == 0){
            PRINT_ERROR("USER TRANSACTION BUFFER HANDLER - ZERO COUNT")
        }
        itr->second--;
        
        if(itr->second == 0){
            userTrBuffer.erase(itr);
            dcmServicedTransactionHandler(originTr);
            delete originTr;
        }
    }

    void SectorLog::lockLPA(const LPA_type lpaToLock)
    {
        lockedTr.insert({lpaToLock, std::list<NVM_Transaction_Flash*>()});
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
        for(auto it = subPageList.begin(); it != subPageList.end(); ){
            if(sectorMap->getPageForKey((*it)) != NULL){
                if(pageBuffer->Exists((*it), 1)){
                    pageBuffer->updateData((*it), 0);
                } else{
                    pageBuffer->insertData((*it), 0);
                }
                it++;
            } else{
                subPageList.erase(it++);
            }
        }

        if(checkFlushIsRequired(subPageList.size() * SubPageCalculator::subPageUnit)){
            std::list<NVM_Transaction_Flash*>* evictedTr = new std::list<NVM_Transaction_Flash*>();
            evictedTr->push_back(getFlushTransaction());
            Memory_Transfer_Info* read_transfer_info = new Memory_Transfer_Info;
			read_transfer_info->Size_in_bytes = subPagesPerPage * SubPageCalculator::subPageUnit * SECTOR_SIZE_IN_BYTE;
			read_transfer_info->Related_request = evictedTr;
			read_transfer_info->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_SECTORLOG_FLUSH_FINISHED;
			read_transfer_info->Stream_id = streamID;
			dcm->service_dram_access_request(read_transfer_info);
            ((Data_Cache_Manager_Flash_Advanced*)(dcm))->AddBackPressureBufferDepth(tr->Stream_id, subPagesPerPage * SubPageCalculator::subPageUnit);
        }

        std::set<NVM_Transaction_Flash_RD*> temp;
        for(auto tr : readingSectorGroupAreaList.at(tr->PPA)){
            temp.insert(tr);
        }
        Stats2::corRead(temp.size());

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
            for(auto tr : *(std::list<NVM_Transaction_Flash*>*)(info->Related_request)){
                tsu->Submit_transaction(tr);
            }
            tsu->Schedule();
            delete (std::list<NVM_Transaction_Flash*>*)info->Related_request;
        } break;
        case Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_SECTORLOG_CLUSTERING_FINISHED:{
            bitFilter->handleClusteringReadIsArrived();
        } break;
        }
    }
    
    void SectorLog::handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash *transaction)
    {
        if(transaction->Type == Transaction_Type::READ && ((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite != NULL){
            ((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->RelatedRead = NULL;
            return;
        }
        if (transaction->Source == Transaction_Source_Type::SECTORLOG_USER)
        {
            switch (transaction->Type) {
            case Transaction_Type::READ: {
                if(((NVM_Transaction_Flash_RD*)transaction)->LPA == NO_LPA){
                    instance->sectorGroupAreaReadHandler(((NVM_Transaction_Flash_RD*)transaction));
                } else{
                    ((Data_Cache_Manager_Flash_Advanced*)instance->dcm)->InsertReadPageMappedCache((NVM_Transaction_Flash_RD*)transaction);
                    for(auto tr : ((NVM_Transaction_Flash_RD*)transaction)->originTr){
                        instance->userTrBufferHandler(tr);
                    }
                }
            } break;
            case Transaction_Type::WRITE:{
                ((Data_Cache_Manager_Flash_Advanced*)(instance->dcm))->SubBackPressureBufferDepth(transaction->Stream_id, instance->subPagesPerPage * SubPageCalculator::subPageUnit);
            } break;

            default: PRINT_ERROR("ERROR IN SECTOR LOG HANDLE TRANSACTION : 1"); break;
            }
        }
        else if (transaction->Source == Transaction_Source_Type::SECTORLOG_MERGE) {
            switch (transaction->Type) {
                //Read data related to the victim block before the merge process is started.
                case Transaction_Type::READ: instance->sectorMap->handleMergeReadArrived(transaction->PPA); break;

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
                case Transaction_Type::ERASE: instance->sectorMap->eraseVictimBlock(transaction->PPA); break;
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