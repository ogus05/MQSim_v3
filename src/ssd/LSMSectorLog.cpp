#include "LSMSectorLog.h"
#include "Data_Cache_Flash.h"

namespace SSD_Components{
	LSMSectorLog* LSMSectorLog::instance = NULL;

    LSMSectorLog::LSMSectorLog(stream_id_type streamID, uint32_t sectorsPerPage, uint32_t pagesPerBlock, uint32_t writeBlockBufferSizeInBlocks, uint32_t readBlockBufferSizeInBlocks, 
            std::vector<uint32_t> blocksInLevel, Address_Mapping_Unit_Page_Level* amu, TSU_Base* tsu, Data_Cache_Manager_Base* dcm)
            : streamID(streamID), sectorsPerPage(sectorsPerPage), pagesPerBlock(pagesPerBlock), amu(amu), tsu(tsu), dcm(dcm){
        this->instance = this;

        writeBlockBuffer = new WriteBlockBuffer(writeBlockBufferSizeInBlocks * pagesPerBlock * sectorsPerPage, pagesPerBlock * sectorsPerPage);
        readBlockBuffer = new ReadBlockBuffer(readBlockBufferSizeInBlocks * pagesPerBlock * sectorsPerPage);
        
        LSMSectorMap* curSectorMap = NULL;
        this->sectorMap.resize(blocksInLevel.size());
        for(int32_t levelCount = blocksInLevel.size() - 1; levelCount >= 0; levelCount--){
            curSectorMap = new LSMSectorMap(blocksInLevel.at(levelCount), levelCount, curSectorMap);
            this->sectorMap.at(levelCount) = curSectorMap;
        }
    }

    LSMSectorLog::~LSMSectorLog()
    {
        delete writeBlockBuffer;
        delete readBlockBuffer;

        for(auto e : sectorMap){
            delete e;
        }
    }

    void LSMSectorLog::connectDCMService(void (*DCMHandleTrServicedSignal)(NVM_Transaction_Flash *))
    {
        this->DCMHandleTrServicedSignal = DCMHandleTrServicedSignal;
    }

    void LSMSectorLog::handleInputTr(std::list<NVM_Transaction *>& transaction_list)
    {
        if(transaction_list.front()->Type == Transaction_Type::WRITE){
            std::list<NVM_Transaction*> fullPageWriteTrList;
            std::list<NVM_Transaction_Flash_WR*>* partialPageWriteTrList = new std::list<NVM_Transaction_Flash_WR*>();
            uint32_t partialPageWriteSizeInSectors = 0;

            for(auto& transaction : transaction_list){
                NVM_Transaction_Flash_WR* tr = (NVM_Transaction_Flash_WR*)transaction;
                LSA_type LSAToWrite = LSA_type(tr->LPA, tr->write_sectors_bitmap);
                invalidateInSectorMap(LSAToWrite);

                if(tr->Data_and_metadata_size_in_byte == sectorsPerPage * SECTOR_SIZE_IN_BYTE){
                    readBlockBuffer->Remove(LSAToWrite);
                    writeBlockBuffer->Remove(LSAToWrite);
                    fullPageWriteTrList.push_back(tr);
                } else{
                    readBlockBuffer->Remove(LSAToWrite);
                    writeBlockBuffer->Insert(LSAToWrite);
                    partialPageWriteTrList->push_back(tr);
                    partialPageWriteSizeInSectors += count_sector_no_from_status_bitmap(tr->write_sectors_bitmap);
                    checkFlushIsRequired();
                }
            }

            if(fullPageWriteTrList.size() > 0){
                amu->Translate_lpa_to_ppa_and_dispatch(fullPageWriteTrList);
            }

            if(partialPageWriteTrList->size() > 0){
                Memory_Transfer_Info* partialWriteTransferInfo = new Memory_Transfer_Info();
                partialWriteTransferInfo->Related_request = partialPageWriteTrList;
                partialWriteTransferInfo->Size_in_bytes = partialPageWriteSizeInSectors * SECTOR_SIZE_IN_BYTE;
                partialWriteTransferInfo->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_WRITE_FOR_SECTORLOG_FINISHED;
                partialWriteTransferInfo->Stream_id = streamID;
                dcm->service_dram_access_request(partialWriteTransferInfo);
            }
            
        } else if(transaction_list.front()->Type == Transaction_Type::READ){
            std::list<NVM_Transaction_Flash_RD*>* bufferReadTrList = new std::list<NVM_Transaction_Flash_RD*>();
            uint32_t bufferReadSize = 0;
            std::list<NVM_Transaction_Flash_RD*> sectorGroupAreaReadTrList;
            std::list<NVM_Transaction*> pageGroupAreaReadTrList;

            for(auto& transaction : transaction_list){
                NVM_Transaction_Flash_RD* tr = (NVM_Transaction_Flash_RD*)transaction;
                LSA_type LSAToFind = LSA_type(tr->LPA, tr->read_sectors_bitmap);
                userReadTrBuffer.insert({tr, 0});
                //Handle in Barrier.
                page_status_type sectorsBehindBarrier = checkBehindBarrier(tr);
                LSAToFind.sectorsBitmap &= ~(sectorsBehindBarrier);

                // Handle in Block Buffer.
                if(LSAToFind.sectorsBitmap > 0){
                    page_status_type sectorsInWriteBlockBuffer = (writeBlockBuffer->LookUp(LSAToFind) & LSAToFind.sectorsBitmap);
                    page_status_type sectorsInReadBlockBuffer = (readBlockBuffer->LookUp(LSAToFind) & LSAToFind.sectorsBitmap);

                    if((sectorsInWriteBlockBuffer | sectorsInReadBlockBuffer) > 0){
                        if(sectorsInWriteBlockBuffer > 0){
                            LSAToFind.sectorsBitmap &= ~(sectorsInWriteBlockBuffer);
                            tr->writebackRequired = true;
                        }
                        if(sectorsInReadBlockBuffer > 0){
                            LSAToFind.sectorsBitmap &= ~(sectorsInReadBlockBuffer);
                            invalidateInSectorMap(LSA_type(LSAToFind.lpa, sectorsInReadBlockBuffer));
                        }

                        bufferReadSize += count_sector_no_from_status_bitmap(sectorsInWriteBlockBuffer | sectorsInReadBlockBuffer);
                        bufferReadTrList->push_back(tr);
                        userReadTrBuffer.at(tr)++;
                    }
                }

                // Handle in LSM Sector Map.
                if(LSAToFind.sectorsBitmap > 0){
                    for(auto& curLevelSectorMap : sectorMap){
                        std::list<LSMSectorMapReadingInfo *> searchedInfo = curLevelSectorMap->LookUp(LSAToFind);
                        if(searchedInfo.size() > 0){
                            for(auto curInfo : searchedInfo){
                                PPA_type curPPA = curInfo->ppa;

                                if(ppaOngoingRead.find(curPPA) == ppaOngoingRead.end()){
                                    NVM_Transaction_Flash_RD* sectorGroupAreaReadTr = new NVM_Transaction_Flash_RD(
                                        Transaction_Source_Type::SECTORLOG_USER, streamID, sectorsPerPage
                                        , NO_LPA, curPPA, NULL, 0, ((uint64_t)1 << sectorsPerPage) - (uint64_t)1, Simulator->Time()
                                    );
                                    sectorGroupAreaReadTr->readingInfo = curInfo;
                                    sectorGroupAreaReadTr->Address = amu->Convert_ppa_to_address(curPPA);
                                    ppaOngoingRead.insert({curPPA, std::list<NVM_Transaction_Flash_RD*>()});
                                    sectorGroupAreaReadTrList.push_back(sectorGroupAreaReadTr);
                                }
                                ppaOngoingRead.at(curPPA).push_back(tr);
                                userReadTrBuffer.at(tr)++;
                            }

                            if(LSAToFind.sectorsBitmap == 0){
                                break;
                            }
                        }
                    }
                }
                // Handle in Address Mapping Unit.
                if(LSAToFind.sectorsBitmap > 0){     
                    NVM_Transaction_Flash_RD* pageGroupAreaReadTr = new NVM_Transaction_Flash_RD(Transaction_Source_Type::SECTORLOG_USER, streamID,
                        count_sector_no_from_status_bitmap(LSAToFind.sectorsBitmap) * SECTOR_SIZE_IN_BYTE, LSAToFind.lpa, NO_PPA, NULL, tr->Priority_class, 0, LSAToFind.sectorsBitmap, CurrentTimeStamp);
                    pageGroupAreaReadTrList.push_back(pageGroupAreaReadTr);
                    pageGroupAreaReadTr->originTr = tr;
                    userReadTrBuffer.at(tr)++;
                }

                if(userReadTrBuffer.at(tr) == 0){
                    PRINT_ERROR("READ ERROR")
                }
            }

            if(bufferReadTrList->size() > 0){
                Memory_Transfer_Info* readTransferInfo = new Memory_Transfer_Info;
                readTransferInfo->Size_in_bytes = bufferReadSize * SECTOR_SIZE_IN_BYTE;
                readTransferInfo->Related_request = bufferReadTrList;
                readTransferInfo->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_SECTORLOG_READ_FINISHED;
                readTransferInfo->Stream_id = streamID;
                dcm->service_dram_access_request(readTransferInfo);
            } else{
                delete bufferReadTrList;
            }
            
            if(sectorGroupAreaReadTrList.size() > 0){
                tsu->Prepare_for_transaction_submit();
                for(auto sectorGroupAreadReadTr : sectorGroupAreaReadTrList){
                    tsu->Submit_transaction(sectorGroupAreadReadTr);
                }
                tsu->Schedule();
            }

            if(pageGroupAreaReadTrList.size() > 0){
                amu->Translate_lpa_to_ppa_and_dispatch(pageGroupAreaReadTrList);
            }

        }
    }
    
    void LSMSectorLog::handleDRAMServiced(Memory_Transfer_Info *info)
    {
        switch(info->next_event_type){
            case Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_FLUSH_FINISHED:{
                ((CompactionInformation*)info->Related_request)->remainReadCount--;
                writeToSectorGroupArea((CompactionInformation*)info->Related_request);
            } break;
            case Data_Cache_Simulation_Event_Type::MEMORY_WRITE_FOR_SECTORLOG_FINISHED:{
                for(auto& curTr : (*(std::list<NVM_Transaction_Flash_WR*>*)info->Related_request)){
                    DCMHandleTrServicedSignal(curTr);
                    delete curTr;
                }
                delete (std::list<NVM_Transaction_Flash_WR*>*)info->Related_request;
            } break;
            case Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_SECTORLOG_READ_FINISHED:{
                for(auto& curTr : (*(std::list<NVM_Transaction_Flash_RD*>*)info->Related_request)){
                    writeBlockBuffer->Remove(LSA_type(curTr->LPA, curTr->read_sectors_bitmap));
                    readBlockBuffer->Remove(LSA_type(curTr->LPA, curTr->read_sectors_bitmap));
                    handleUserReadBuffer(curTr);
                }
                delete (std::list<NVM_Transaction_Flash_RD*>*)info->Related_request;
            } break;

        }
    }

    void LSMSectorLog::handleUserReadDataIsArrived(NVM_Transaction_Flash_RD * transaction)
    {
        if(transaction->originTr == NULL){
            auto itr = ppaOngoingRead.find(transaction->PPA);
            if(itr == ppaOngoingRead.end()){
                PRINT_ERROR("HANDLE USE READ DATA ARRIVED - 1")
            }
            std::list<NVM_Transaction_Flash_RD*> waitingOriginReadTrList = itr->second;

            LSMSectorMapReadingInfo* readingInfo = transaction->readingInfo;
            
            for(auto waitingReadTr : waitingOriginReadTrList){
                readingInfo->dataInPage.find(waitingReadTr->LPA)->second &= ~(waitingReadTr->read_sectors_bitmap);
                handleUserReadBuffer(waitingReadTr);
            }

            for(auto dataInPage : readingInfo->dataInPage){
                if(dataInPage.second != 0){
                    readBlockBuffer->Insert(LSA_type(dataInPage.first, dataInPage.second));
                }
            }

            ppaOngoingRead.erase(itr);
            delete readingInfo;
        } else{
            handleUserReadBuffer(transaction->originTr);
        }
    }

    void LSMSectorLog::handleUserReadBuffer(NVM_Transaction_Flash_RD * transaction)
    {
        auto bufferItr = userReadTrBuffer.find(transaction);
        if(bufferItr == userReadTrBuffer.end()){
            PRINT_ERROR("USER TRANSACTION BUFFER HANDLER - NO EXISTS TRANSACTION")
        }
        bufferItr->second--;
        
        if(bufferItr->second < 0){
            PRINT_ERROR("USER TRANSACTION BUFFER HANDLER - ERROR IN REMAIN TRANSACTIONS")
        }

        if(bufferItr->second == 0){
            userReadTrBuffer.erase(bufferItr);
            DCMHandleTrServicedSignal(transaction);
            delete transaction;
        }
    }

    void LSMSectorLog::handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash *transaction)
    {
        switch(transaction->Source){
            case Transaction_Source_Type::SECTORLOG_USER:{
                if(transaction->Type == Transaction_Type::READ){
                    instance->handleUserReadDataIsArrived((NVM_Transaction_Flash_RD*)transaction);
                } else if (transaction->Type == Transaction_Type::WRITE){

                }
            } break;
            case Transaction_Source_Type::SECTORLOG_MERGE:{
                if(transaction->Type == Transaction_Type::READ){
                    if (((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite != NULL) {
                        ((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->RelatedRead = NULL;
                        return;
                    }
                }
                instance->handleCompactionTrIsArrived(transaction);
            } break;
        }
    }

    void LSMSectorLog::setBarrier(LSA_type &lsa)
    {
        auto barrierItr = barrier.find(lsa.lpa);
        if(barrierItr == barrier.end()){
            barrierItr = barrier.insert({lsa.lpa, std::list<BarrierInformation*>()}).first;
        }
        barrierItr->second.push_back(new BarrierInformation(lsa.sectorsBitmap));
    }

    void LSMSectorLog::removeBarrier(LSA_type &lsa)
    {
        auto barrierItr = barrier.find(lsa.lpa);
        if(barrierItr == barrier.end()){
            return;
        }
        std::list<BarrierInformation*>& barrierInfoList = barrierItr->second;
        page_status_type sectorsToRemove = lsa.sectorsBitmap;

        for(auto barrierInfo = barrierInfoList.begin(); barrierInfo != barrierInfoList.end();){
            page_status_type overlappedSectors = ((*barrierInfo)->compactionSectors & sectorsToRemove);
            if(overlappedSectors > 0){
                (*barrierInfo)->compactionSectors &= ~(overlappedSectors);
                sectorsToRemove &= ~(overlappedSectors);
                for(auto curPendingRead = (*barrierInfo)->pendingTr.begin(); curPendingRead != (*barrierInfo)->pendingTr.end();){
                    if(((*barrierInfo)->compactionSectors & curPendingRead->first) == 0){
                        handleUserReadBuffer(curPendingRead->second);
                        curPendingRead = (*barrierInfo)->pendingTr.erase(curPendingRead);
                    } else{
                        curPendingRead++;
                    }
                }

                if((*barrierInfo)->compactionSectors == 0){
                    delete (*barrierInfo);
                    barrierInfo = barrierInfoList.erase(barrierInfo);

                    if(barrierInfoList.size() == 0){
                        barrier.erase(barrierItr);
                        break;
                    }
                } else{
                    barrierInfo++;
                }
            } else{
                barrierInfo++;
            }
        }
    }

    page_status_type LSMSectorLog::checkBehindBarrier(NVM_Transaction_Flash_RD *tr)
    {
        page_status_type sectorsToCheck = tr->read_sectors_bitmap;
        auto barrierItr = barrier.find(tr->LPA);
        if(barrierItr != barrier.end()){
            page_status_type overlappedSectors = 0;
            for(auto barrierInfo : barrierItr->second){
                overlappedSectors = (barrierInfo->compactionSectors & sectorsToCheck);
                if(overlappedSectors > 0){
                    sectorsToCheck &= ~(overlappedSectors);
                    barrierInfo->pendingTr.push_back({overlappedSectors, tr});
                    userReadTrBuffer.at(tr)++;
                }
            }
        }
        return (tr->read_sectors_bitmap & ~(sectorsToCheck));
    }

    void LSMSectorLog::invalidateBarrierSectors(LSA_type &lsa)
    {
        std::list<BarrierInformation*> targetBarrierList = barrier.find(lsa.lpa)->second;
        for(auto barrierItr = targetBarrierList.rbegin(); barrierItr != targetBarrierList.rend(); barrierItr++){
            LSA_type overlappedSectors = LSA_type(lsa.lpa, (lsa.sectorsBitmap & (*barrierItr)->sectorsToInvalidate));
            if(overlappedSectors.sectorsBitmap > 0){
                lsa.sectorsBitmap &= ~(overlappedSectors.sectorsBitmap);
                removeBarrier(overlappedSectors);
                if(lsa.sectorsBitmap == 0){
                    return;
                }
            }
        }
    }

    void LSMSectorLog::handleCompactionTrIsArrived(NVM_Transaction_Flash *transaction)
    {
        CompactionInformation* compactionInfo = (*compactionInfoList.find(transaction->compactionInfo));
        if(transaction->Type == Transaction_Type::READ){
            compactionInfo->remainReadCount--;
            if(compactionInfo->remainReadCount == 0){
                // In case of the highest level of the sector map is needed for free area.
                if(compactionInfo->levelToInsert == sectorMap.size()){
                    writeToPageGroupArea(compactionInfo);
                }
                else{
                    writeToSectorGroupArea(compactionInfo);
                }

                //There are no valid sectors.
                if(compactionInfo->remainWriteCount == 0){
                    PRINT_ERROR("SECTOR LOG - HANDLE COMPACTION TR IS ARRIVED : THERE ARE NO VALID SECTORS")
                }
            }
        } else if(transaction->Type == Transaction_Type::WRITE){
            for(auto& curLSA : ((NVM_Transaction_Flash_WR*)transaction)->lsa){
                removeBarrier(curLSA);
            }

            compactionInfo->remainWriteCount--;
            if(compactionInfo->remainWriteCount == 0){
                //If the compaction target is the Buffer, erase transactions are not needed.
                if(compactionInfo->levelToInsert == 0){
                    handleCompactionIsFinished(compactionInfo);
                } else{
                    transferEraseTr(compactionInfo);
                }
            }

        } else if(transaction->Type == Transaction_Type::ERASE){
            PPA_type trAddrToPPA = amu->Convert_address_to_ppa(transaction->Address);
            for(auto entryItr = compactionInfo->victimGroup->begin(); entryItr != compactionInfo->victimGroup->end(); entryItr++){
                if(amu->Convert_address_to_ppa((*entryItr)->getPBA()) == trAddrToPPA){
                    if(!sectorMap.at(compactionInfo->levelToInsert - 1)->EraseEntry(*entryItr)){
                        if(!sectorMap.at(compactionInfo->levelToInsert)->EraseEntry(*entryItr)){
                            PRINT_ERROR("ERROR IN ERASE SECTOR MAP ENTRY")
                        }
                    }
                    amu->erase_block_from_sectorLog(transaction->Address);
                    compactionInfo->victimGroup->erase(entryItr);
                    break;
                }
            }
            
            compactionInfo->remainEraseCount--;
            if(compactionInfo->remainEraseCount == 0){
                handleCompactionIsFinished(compactionInfo);
            }
        }
    }

    void LSMSectorLog::writeToPageGroupArea(CompactionInformation *compactionInfo)
    {
        std::list<LSA_type>* lsaToWritePageGroupArea = compactionInfo->dataToCompaction;

        sortLSAList(lsaToWritePageGroupArea);

        std::list<NVM_Transaction*> writeTrList;
        for(auto& lsa : (*lsaToWritePageGroupArea)){
            NVM_Transaction_Flash_WR* newTr = new NVM_Transaction_Flash_WR(Transaction_Source_Type::SECTORLOG_MERGE, streamID, 
                count_sector_no_from_status_bitmap(lsa.sectorsBitmap) * SECTOR_SIZE_IN_BYTE, lsa.lpa, NULL, 0, lsa.sectorsBitmap, CurrentTimeStamp);
            newTr->compactionInfo = compactionInfo;
            newTr->lsa.push_back(lsa);
            writeTrList.push_back(newTr);
        }
        compactionInfo->remainWriteCount += writeTrList.size();
        amu->Translate_lpa_to_ppa_and_dispatch(writeTrList);
    }

    void LSMSectorLog::checkFlushIsRequired()
    {
        if(writeBlockBuffer->isNoFreeCapacity() && !writeBlockBuffer->isFlushing()){
            std::list<LSA_type>* lsaToStoreSectorGroupArea = new std::list<LSA_type>();
            std::list<BlockBufferEntry*> entriesToFlush = writeBlockBuffer->evictEntriesInBlockSize();
            for(auto curEntry : entriesToFlush){
                lsaToStoreSectorGroupArea->push_back(curEntry->lsa);
                setBarrier(curEntry->lsa);
                delete curEntry;
            }

            CompactionInformation* compactionInfo = addCompactioningGroup(NULL, lsaToStoreSectorGroupArea, 0);
            if(lsaToStoreSectorGroupArea->size() > 0){
                compactionInfo->remainReadCount++;

                Memory_Transfer_Info* DRAMReadInfo = new Memory_Transfer_Info();
                DRAMReadInfo->Related_request = compactionInfo;
                //There could be a case that the read DRAM size is not fitted the block size, but we assume that.
                DRAMReadInfo->Size_in_bytes = pagesPerBlock * sectorsPerPage * SECTOR_SIZE_IN_BYTE;
                DRAMReadInfo->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_FLUSH_FINISHED;
                DRAMReadInfo->Stream_id = streamID;
                dcm->service_dram_access_request(DRAMReadInfo);
            } else{
                PRINT_ERROR("BLOCKBUFFER : CHECK FLUSH IS REQUIRED")
            }
        }
    }

    void LSMSectorLog::checkCompactionIsRequired(const uint32_t level)
    {
        LSMSectorMap* targetLevel = sectorMap.at(level);
        if(targetLevel->isNoFreeCapacity() && !targetLevel->isCompactioning()){

            std::list<LSMSectorMapEntry*>* victimGroup = targetLevel->getVictimGroup();
            if(victimGroup == NULL){
                return;
            } else if((victimGroup->size() == 1 && victimGroup->front()->getInvalidSectorsCount() == 0) && level != sectorMap.size() - 1){
                sectorMap.at(level)->MoveEntryToUpperLevel(victimGroup->front());
                delete victimGroup;
                checkCompactionIsRequired(level + 1);
                return;
            }

            targetLevel->startCompaction();

            std::unordered_map<PPA_type, uint32_t> ppaToReadList;
            std::list<LSA_type>* readLSAList = new std::list<LSA_type>();
            for(auto & victimEntry : (*victimGroup)){
                std::unordered_map<PPA_type, uint32_t> currentSectorsList = victimEntry->getValidSectorsCountInPage();
                std::list<LSA_type> currentreadLSAList = victimEntry->getValidLSAList();
                victimEntry->setAsVictim();
                for(auto curSectors : currentSectorsList){
                    ppaToReadList[curSectors.first] += curSectors.second;
                }
                readLSAList->insert(readLSAList->end(), currentreadLSAList.begin(), currentreadLSAList.end());
            }

            for(auto& readLSA : (*readLSAList)){
                setBarrier(readLSA);
            }

            CompactionInformation* compactionInfo = addCompactioningGroup(victimGroup, readLSAList, level + 1);
            
            if(ppaToReadList.size() > 0){
                tsu->Prepare_for_transaction_submit();
                compactionInfo->remainReadCount += ppaToReadList.size();
                for(auto& curPPA : ppaToReadList){
                    NVM_Transaction_Flash_RD* newTr = new NVM_Transaction_Flash_RD(Transaction_Source_Type::SECTORLOG_MERGE, streamID, 
                        curPPA.second * SECTOR_SIZE_IN_BYTE, NO_LPA, curPPA.first, NULL, 0, ((uint64_t)1 << curPPA.second) - (uint64_t)1, CurrentTimeStamp);
                    newTr->Address = amu->Convert_ppa_to_address(curPPA.first);
                    newTr->compactionInfo = compactionInfo;
                    tsu->Submit_transaction(newTr);
                }
                tsu->Schedule();
            } else{
                if(readLSAList->size() > 0){
                    PRINT_ERROR("ERROR IN CHECK COMPACTION IS REQUIRED")
                } else{
                    transferEraseTr(compactionInfo);
                }
            }
        }
    }

    //SectorMap mapping information is allocated in this function.
    void LSMSectorLog::writeToSectorGroupArea(CompactionInformation* compactionInfo)
    {
        std::list<NVM_Transaction_Flash_WR*> sectorGroupTrList;
        std::list<LSA_type>* remainingEntries = compactionInfo->dataToCompaction;
        std::list<NVM_Transaction*> pageGroupTrList;

        sortLSAList(remainingEntries);

        //Flush To Page Group Area.
        for(auto lsaItr = remainingEntries->begin(); lsaItr != remainingEntries->end(); lsaItr++){
            if(lsaItr->sectorsBitmap == ((uint64_t)1 << sectorsPerPage) - (uint64_t)1){
                NVM_Transaction_Flash_WR* pageGroupWriteTr = new NVM_Transaction_Flash_WR(Transaction_Source_Type::SECTORLOG_MERGE, streamID, sectorsPerPage * SECTOR_SIZE_IN_BYTE, 
                    lsaItr->lpa, NULL, 0, lsaItr->sectorsBitmap, CurrentTimeStamp);
                pageGroupWriteTr->compactionInfo = compactionInfo;
                pageGroupWriteTr->lsa.push_back((*lsaItr));
                
                pageGroupTrList.push_back(pageGroupWriteTr);
                lsaItr = remainingEntries->erase(lsaItr);
            }
        }

        if(remainingEntries->size() == 0){
            return;
        }

        std::list<LSMSectorMapEntry*> createdEntryList;
        createdEntryList.push_back(new LSMSectorMapEntry(amu->allocate_block_for_sectorLog(streamID)));

        while(!remainingEntries->empty()){
            std::list<LSA_type> sectorGroupPage = makeSectorGroupPage(remainingEntries);
            LSMSectorMapEntry* targetSectorMapEntry = createdEntryList.back();

            NVM::FlashMemory::Physical_Page_Address pageAddr = targetSectorMapEntry->getNextPage();
            if(pageAddr.PageID >= pagesPerBlock){
                targetSectorMapEntry = new LSMSectorMapEntry(amu->allocate_block_for_sectorLog(streamID));
                createdEntryList.push_back(targetSectorMapEntry);
                pageAddr = targetSectorMapEntry->getNextPage();
            }

            NVM_Transaction_Flash_WR* newTr = new NVM_Transaction_Flash_WR(Transaction_Source_Type::SECTORLOG_MERGE, 
                streamID, sectorsPerPage, NO_LPA, NULL, 0, ((uint64_t)1 << sectorsPerPage) - (uint64_t)1, CurrentTimeStamp);
            newTr->Address = pageAddr;
            newTr->PPA = amu->Convert_address_to_ppa(pageAddr);
            newTr->compactionInfo = compactionInfo;
            newTr->lsa.insert(newTr->lsa.end(), sectorGroupPage.begin(), sectorGroupPage.end());
            targetSectorMapEntry->Insert(sectorGroupPage, newTr->PPA);
            sectorGroupTrList.push_back(newTr);
        }

        if(sectorGroupTrList.size() > 0){
            compactionInfo->remainWriteCount += sectorGroupTrList.size();

            sectorMap.at(compactionInfo->levelToInsert)->InsertEntry(createdEntryList);

            tsu->Prepare_for_transaction_submit();
            for(auto sectorGroupTr : sectorGroupTrList){
                tsu->Submit_transaction(sectorGroupTr);
            }
            tsu->Schedule();
        }

        if(pageGroupTrList.size() > 0){
            compactionInfo->remainWriteCount += pageGroupTrList.size();
            amu->Translate_lpa_to_ppa_and_dispatch(pageGroupTrList);
        }
    }

    std::list<LSA_type> LSMSectorLog::makeSectorGroupPage(std::list<LSA_type> *remainingEntries)
    {
        uint32_t sectorsCountInPage = 0;                         // Sum of # of the sectors count that would be inserted to current page.
        std::list<LSA_type> sectorsInsertToOnePage;             // Data of the current page.
        while(!remainingEntries->empty()){
            auto curEntryToInsert = remainingEntries->begin();

            invalidateBarrierSectors((*curEntryToInsert));

            if(curEntryToInsert->sectorsBitmap == 0){
                remainingEntries->erase(curEntryToInsert);
                continue;
            }

            if(sectorsCountInPage + count_sector_no_from_status_bitmap(curEntryToInsert->sectorsBitmap) <= sectorsPerPage){
                sectorsInsertToOnePage.push_back(*curEntryToInsert);
                sectorsCountInPage += count_sector_no_from_status_bitmap(curEntryToInsert->sectorsBitmap);
                remainingEntries->erase(curEntryToInsert);
            } else if(sectorsCountInPage != sectorsPerPage){
                page_status_type remainingSectors = curEntryToInsert->sectorsBitmap;
                page_status_type writingSectors = 0;
                page_status_type flag = 1;
                while (sectorsCountInPage + count_sector_no_from_status_bitmap(writingSectors) < sectorsPerPage)
                {
                    while (writingSectors == (flag & remainingSectors))
                    {
                        flag = (uint64_t)1 | (flag << (uint64_t)1);
                    }
                    writingSectors = (flag & remainingSectors);
                }
                remainingSectors &= ~(writingSectors);
                sectorsInsertToOnePage.push_back(LSA_type(curEntryToInsert->lpa, writingSectors));
                sectorsCountInPage += count_sector_no_from_status_bitmap(writingSectors);
                if(remainingSectors == 0){
                    remainingEntries->erase(curEntryToInsert);
                } else{
                    curEntryToInsert->sectorsBitmap = remainingSectors;
                }
            } else{
                break;
            }
        }

        return sectorsInsertToOnePage;
    }

    void LSMSectorLog::sortLSAList(std::list<LSA_type>* entries)
    {
        entries->sort();
        
        auto prevEntry = entries->begin();
        auto curEntry = entries->begin();
        curEntry++;
        
        // Combine the sectors which have the corresponding LPA.
        // Transmit the sectors which could be made to a full-page to the AMU.
        while(curEntry != entries->end()){
            if(curEntry->lpa == prevEntry->lpa){
                prevEntry->sectorsBitmap |= curEntry->sectorsBitmap;
                curEntry = entries->erase(curEntry);
            } else{
                prevEntry++;
                curEntry++;
            }
        }
    }

    void LSMSectorLog::transferEraseTr(CompactionInformation *compactionInfo)
    {
        if(compactionInfo->remainReadCount != 0 || compactionInfo->remainWriteCount != 0){
            PRINT_ERROR("ERROR IN TRANSFER ERASE TRANSACTION")
        }
        tsu->Prepare_for_transaction_submit();
        auto victimGroup = (*compactionInfo->victimGroup);
        compactionInfo->remainEraseCount += victimGroup.size();
        for(auto curVictimEntry = victimGroup.begin(); curVictimEntry != victimGroup.end(); curVictimEntry++){
            NVM_Transaction_Flash_ER* newTr = new NVM_Transaction_Flash_ER(Transaction_Source_Type::SECTORLOG_MERGE, streamID, (*curVictimEntry)->getPBA());
            newTr->compactionInfo = compactionInfo;
            tsu->Submit_transaction(newTr);
        }
        tsu->Schedule();
    }

    void LSMSectorLog::handleCompactionIsFinished(CompactionInformation *compactionInfo)
    {
        if(compactionInfo->levelToInsert != 0){
            sectorMap.at(compactionInfo->levelToInsert - 1)->endCompaction();
            checkCompactionIsRequired(compactionInfo->levelToInsert - 1);
            
        }
        if(compactionInfo->levelToInsert != sectorMap.size()){
            checkCompactionIsRequired(compactionInfo->levelToInsert);
        }

        compactionInfoList.erase(compactionInfo);
        delete compactionInfo;
    }
    
    void LSMSectorLog::invalidateInSectorMap(const LSA_type& lsa)
    {
        page_status_type sectorsToInvalidate = lsa.sectorsBitmap;
        auto barrierItr = barrier.find(lsa.lpa);
        if(barrierItr != barrier.end()){
            for(auto barrierInfo : barrierItr->second){
                page_status_type overlappedSectors = (barrierInfo->compactionSectors & sectorsToInvalidate);
                if(overlappedSectors > 0){
                    barrierInfo->sectorsToInvalidate |= sectorsToInvalidate;
                }
            }
        }

        for(auto& curSectorMap : sectorMap){
            sectorsToInvalidate &= ~(curSectorMap->Invalidate(lsa));
            if(sectorsToInvalidate == 0) break;
        }
    }

    CompactionInformation::CompactionInformation(std::list<LSMSectorMapEntry*>* victimGroup, std::list<LSA_type>* dataToCompaction, uint32_t levelToInsert)
    {
        this->victimGroup = victimGroup;
        this->dataToCompaction = dataToCompaction;
        this->levelToInsert = levelToInsert;
        this->remainReadCount = 0;
        this->remainWriteCount = 0;
        this->remainEraseCount = 0;
    }

    CompactionInformation::~CompactionInformation()
    {
        if(victimGroup != NULL){
            if(victimGroup->size() == 0){
                delete victimGroup;
            } else{
                PRINT_ERROR("DELETE COMPACTION INFO")
            }
        }
        delete dataToCompaction;
    }

    CompactionInformation* LSMSectorLog::addCompactioningGroup(std::list<LSMSectorMapEntry *> *compactioningGroup, std::list<LSA_type>* lsaToRead, uint32_t levelToInsert)
    {
        auto insertResult = this->compactionInfoList.insert(new CompactionInformation(compactioningGroup, lsaToRead, levelToInsert));
        if(insertResult.second){
            return (*insertResult.first);
        } else{
            PRINT_ERROR("ERROR IN ADD COMPACTIONING GROUP")
        }
    }

    BarrierInformation::BarrierInformation(page_status_type compactionSectors)
        :compactionSectors(compactionSectors){
        sectorsToInvalidate = 0;
        pendingTr.clear();
    }

    BarrierInformation::~BarrierInformation()
    {
    }
}