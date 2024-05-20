#include "Sector_Log.h"
#include "Address_Mapping_Unit_Page_Level.h"
#include "Data_Cache_Flash.h"
#include "Page_Buffer.h"
#include "Sector_Map.h"
#include "Stats2.h"
#include "BitFilter/LogBF.cpp"

namespace SSD_Components
{
    Sector_Log* Sector_Log::instance = NULL;
    Sector_Log::Sector_Log(const stream_id_type& in_streamID, const uint32_t& in_sectorsPerPage, const uint32_t& in_pagesPerBlock, const uint32_t& in_maxBlockSize,
    Address_Mapping_Unit_Page_Level *in_amu, TSU_Base* in_tsu, Data_Cache_Manager_Base* in_dcm, sim_time_type BF_Milestone, uint64_t numberOfLogicalSectors){
        
        maxBlockSize = in_maxBlockSize;        //in_maxBlockSize;
        sectorsPerPage = in_sectorsPerPage;
        pagesPerBlock = in_pagesPerBlock;
        streamID = in_streamID;
        amu = in_amu;
        tsu = in_tsu;
        dcm = in_dcm;

        instance = this;
        sectorMap = new Sector_Map(this, maxBlockSize);
        pageBuffer = new Page_Buffer(sectorsPerPage, this);
        readingDRAMForFlushing = false;

        // logBF = new LogBF(BF_Milestone, Simulator->GetLogFilePath(), in_sectorsPerPage);

    }

    Sector_Log::~Sector_Log()
    { 
        delete sectorMap;
        delete pageBuffer;
    }

    void Sector_Log::setCompleteTrHandler(void (*completeTrHandler)(NVM_Transaction_Flash *))
    {
        this->dcmServicedTransactionHandler = completeTrHandler;
    }

    void Sector_Log::handleInputTransaction(std::list<NVM_Transaction *> transaction_list)
    {
        if(maxBlockSize == 0){
            amu->Translate_lpa_to_ppa_and_dispatch(transaction_list);
        }
        else if (transaction_list.front()->Type == SSD_Components::Transaction_Type::WRITE)
        {
            std::list<NVM_Transaction*> fullPageWriteList;

            std::list<NVM_Transaction_Flash_WR*>* subPageWriteList = new std::list<NVM_Transaction_Flash_WR*>();
            uint32_t sectorsInsertToBuffer = 0;

            for (auto& transaction : transaction_list)
            {
                NVM_Transaction_Flash_WR *tr = (NVM_Transaction_Flash_WR *)transaction;
                if(!handleIsLockedLPA(tr)){
                    pageBuffer->RemoveByPageWrite(tr->LPA, tr->write_sectors_bitmap);
                    sectorMap->Remove(tr->LPA, tr->write_sectors_bitmap);
                    //Full page should be transferred to the AMU.
                    if((count_sector_no_from_status_bitmap(tr->write_sectors_bitmap) == sectorsPerPage)){
                        //Before transfer to amu, remove all the related data in the Sector Log.
                        fullPageWriteList.push_back(tr);
                    }
                    //Partial page should be transferred to the Page Buffer.
                    else {
                        pageBuffer->Insert(tr->LPA, tr->write_sectors_bitmap);
                        subPageWriteList->push_back(tr);
                        sectorsInsertToBuffer += count_sector_no_from_status_bitmap(tr->write_sectors_bitmap);
                    }
                }
            }

            checkFlushIsRequired();

            if(fullPageWriteList.size() > 0) {
                amu->Translate_lpa_to_ppa_and_dispatch(fullPageWriteList);
            }
            
            if(subPageWriteList->size() > 0){
                Memory_Transfer_Info* writeTransferInfo = new Memory_Transfer_Info;
                writeTransferInfo->Size_in_bytes = sectorsInsertToBuffer * SECTOR_SIZE_IN_BYTE;
                writeTransferInfo->Related_request = subPageWriteList;
                writeTransferInfo->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_WRITE_FOR_SECTORLOG_WRITE_FINISHED;
                writeTransferInfo->Stream_id = streamID;
                dcm->service_dram_access_request(writeTransferInfo);
            } else{
                delete subPageWriteList;
            }
        }
        //All of the read transactions should look up the Page Buffer, Sector Map.
        else if (transaction_list.front()->Type == SSD_Components::Transaction_Type::READ)
            {
            handleUserReadTr(transaction_list);
        }
    }

    void Sector_Log::handleUserReadTr(std::list<NVM_Transaction*> transaction_list)
    {
        //If Sector Area doesn't store all of the sectors the read transaction needed,
        //A new read transaction is created and the origin transaction are temporarily buffered(Sector_Log::userReadBuffer).
        //This variable stores the new read transactions created from the transaction_list which should be transferred to AMU.
        std::list<NVM_Transaction *> transactionListForTransferAMU;
        
        //Read transactions desitnated to the Sector Area are stored in this variable.
        std::unordered_map<PPA_type, NVM_Transaction_Flash_RD*> transactionListForTransferTSU;

        std::list<NVM_Transaction_Flash_RD*>* partialPageReadFromDRAM = new std::list<NVM_Transaction_Flash_RD*>();
        uint32_t sectorSizeToReadDRAM = 0;

        for (auto it = transaction_list.begin(); it != transaction_list.end(); it++)
        {
            NVM_Transaction_Flash_RD *tr = (NVM_Transaction_Flash_RD *)(*it);
            page_status_type sectorsToRead = tr->read_sectors_bitmap;
            uint32_t dataSizeToRead = tr->Data_and_metadata_size_in_byte;
            //The sectors ongoing merge process are supposed to exist in the controller. So handled immediatly too. 
            if(handleIsLockedLPA(tr)) continue;

            userTrBuffer.insert({tr, 0});

            page_status_type sectorsExistInPageBuffer = pageBuffer->Exists(tr->LPA);
            if((sectorsToRead & sectorsExistInPageBuffer) > 0){
                dataSizeToRead -= count_sector_no_from_status_bitmap(sectorsToRead & sectorsExistInPageBuffer) * SECTOR_SIZE_IN_BYTE;
                sectorSizeToReadDRAM += count_sector_no_from_status_bitmap(sectorsToRead & sectorsExistInPageBuffer);
                sectorsToRead = (sectorsToRead & ~(sectorsExistInPageBuffer));
                partialPageReadFromDRAM->push_back(tr);
                userTrBuffer.at(tr)++;
            }
            if(sectorsToRead == 0) continue;

            //logBF->addReadCount();
            //logBF->checkRead(tr->LPA, (sectorLocation));
            //Stats2::storeSectorData(tr->LPA, PPAForCurSector, relatedPPA->at(sectorLocation).writtenTime);

            std::vector<PPA_type>* relatedPPA = sectorMap->getAllRelatedPPAsInLPA(tr->LPA);
            if(relatedPPA != nullptr){
                for(int sectorLocation = 0; sectorLocation < sectorsPerPage; sectorLocation++){
                    if(((((page_status_type)1 << sectorLocation) & sectorsToRead) != 0) && relatedPPA->at(sectorLocation) != NO_PPA){
                        sectorsToRead = (sectorsToRead & ~((page_status_type)1 << sectorLocation));
                        dataSizeToRead -= 1 * SECTOR_SIZE_IN_BYTE;

                        auto readSectorGroupArea = transactionListForTransferTSU.find(relatedPPA->at(sectorLocation));

                        if(readSectorGroupArea != transactionListForTransferTSU.end()){
                            readSectorGroupArea->second->read_sectors_bitmap = ((readSectorGroupArea->second->read_sectors_bitmap << 1) | 1);
                            readSectorGroupArea->second->Data_and_metadata_size_in_byte += 1 * SECTOR_SIZE_IN_BYTE;
                        } else{
                            readSectorGroupArea = transactionListForTransferTSU.insert({relatedPPA->at(sectorLocation), new NVM_Transaction_Flash_RD(Transaction_Source_Type::SECTORLOG_USER,
                                streamID, SECTOR_SIZE_IN_BYTE, NO_LPA, relatedPPA->at(sectorLocation), NULL, tr->Priority_class, 0, 1, CurrentTimeStamp)}).first;
                            readSectorGroupArea->second->Address = amu->Convert_ppa_to_address(relatedPPA->at(sectorLocation));
                        }
                        readSectorGroupArea->second->originTr.push_back(tr); 
                        userTrBuffer.at(tr)++;
                    }
                }
            }
            
            Stats2::handleSector();
            
            //If the Sector Area doesn't store all of the sectors, a read transaction read the data area is tranferred to AMU.
            if(sectorsToRead > 0){     
                NVM_Transaction_Flash_RD* readPageGroupArea = new NVM_Transaction_Flash_RD(Transaction_Source_Type::SECTORLOG_USER, streamID,
                    dataSizeToRead, tr->LPA, NO_PPA, NULL, tr->Priority_class, 0, sectorsToRead, CurrentTimeStamp);
                readPageGroupArea->originTr.push_back(tr);
                transactionListForTransferAMU.push_back(readPageGroupArea);
                userTrBuffer.at(tr)++;
            }
        }

        if(transactionListForTransferTSU.size() > 0){
            tsu->Prepare_for_transaction_submit();
            for(auto& tr: transactionListForTransferTSU){
                tsu->Submit_transaction(tr.second);
            }
            tsu->Schedule();
        }
        
        if(transactionListForTransferAMU.size() > 0){
            amu->Translate_lpa_to_ppa_and_dispatch(transactionListForTransferAMU);
        }

        if(sectorSizeToReadDRAM > 0){
            Memory_Transfer_Info* readTransferInfo = new Memory_Transfer_Info;
            readTransferInfo->Size_in_bytes = sectorSizeToReadDRAM * SECTOR_SIZE_IN_BYTE;
            readTransferInfo->Related_request = partialPageReadFromDRAM;
            readTransferInfo->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_SECTORLOG_READ_FINISHED;
            readTransferInfo->Stream_id = streamID;
            dcm->service_dram_access_request(readTransferInfo);
        } else{
            delete partialPageReadFromDRAM;
        }
    }

    void Sector_Log::checkFlushIsRequired()
    {
        if(pageBuffer->getCurrentSize() > sectorsPerPage && !readingDRAMForFlushing){
            readingDRAMForFlushing = true;
            Memory_Transfer_Info* flushTransferInfo = new Memory_Transfer_Info;
            flushTransferInfo->Size_in_bytes = sectorsPerPage * SECTOR_SIZE_IN_BYTE;
            flushTransferInfo->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_SECTORLOG_FLUSH_FINISHED;
            flushTransferInfo->Stream_id = streamID;
            dcm->service_dram_access_request(flushTransferInfo);
        }
    }

    void Sector_Log::sendSubPageWriteForFlush(std::list<std::pair<LPA_type, page_status_type>>& sectorsList, uint32_t flushingID)
    {
        NVM_Transaction_Flash_WR *partialPageWrite = new NVM_Transaction_Flash_WR(Transaction_Source_Type::SECTORLOG_USER,
                                                                                        streamID, sectorsPerPage * SECTOR_SIZE_IN_BYTE, NO_LPA, NULL, IO_Flow_Priority_Class::URGENT, 0, TO_FULL_PAGE(sectorsPerPage), CurrentTimeStamp);
        sectorMap->allocateAddr(sectorsList, partialPageWrite);
        partialPageWrite->PPA = amu->Convert_address_to_ppa(partialPageWrite->Address);
        partialPageWrite->flushingID = flushingID;
        tsu->Prepare_for_transaction_submit();
        tsu->Submit_transaction(partialPageWrite);
        tsu->Schedule();
        
    }

    void Sector_Log::sendAMUWriteForMerge(std::list<std::pair<LPA_type, page_status_type>>& sectorsList, NVM_Transaction_Flash_ER* eraseTr)
    {
        std::list<NVM_Transaction*> transferToAMUList;
        // If the sectors in the sector area can be merged to the full page, transferred to the Address Mapping Unit.
        for(auto & lsa : sectorsList){
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

    void Sector_Log::sendReadForMerge(std::list<PPA_type> ppaToRead, uint32_t mergeID)
    {
        tsu->Prepare_for_transaction_submit();
        for (auto &ppa : ppaToRead)
        {
            NVM_Transaction_Flash_RD *readSectorAreaTr = new NVM_Transaction_Flash_RD(Transaction_Source_Type::SECTORLOG_MERGE, streamID,
                                                                                    sectorsPerPage * SECTOR_SIZE_IN_BYTE, NO_LPA, ppa, NULL, 0, TO_FULL_PAGE(sectorsPerPage), CurrentTimeStamp);
            readSectorAreaTr->mergeID = mergeID;
            readSectorAreaTr->Address = amu->Convert_ppa_to_address(ppa);
            tsu->Submit_transaction(readSectorAreaTr);
        }
        tsu->Schedule();
    }

    void Sector_Log::userTrBufferHandler(std::list<NVM_Transaction_Flash_RD*> originTr)
    {
        for(auto tr : originTr){
            auto itr = userTrBuffer.find(tr);
            if(itr == userTrBuffer.end()){
                PRINT_ERROR("USER TRANSACTION BUFFER HANDLER - NO EXISTS TRANSACTION")
            }
            itr->second--;
            
            if(itr->second < 0){
                PRINT_ERROR("USER TRANSACTION BUFFER HANDLER - ERROR IN REMAIN TRANSACTIONS")
            } 
            if(itr->second == 0){
                userTrBuffer.erase(itr);
                dcmServicedTransactionHandler(tr);
                delete tr;
            }
        }
    }

    void Sector_Log::lockLPA(std::list<LPA_type>& lpaToLock)
    {
        for(auto lpa : lpaToLock){
            lockedTr.insert({lpa, std::list<NVM_Transaction_Flash*>()});
        }
    }

    void Sector_Log::unlockLPA(LPA_type lpaToUnlock)
    {
        auto trList = lockedTr.find(lpaToUnlock)->second;
        for(auto curTr : trList){
            dcmServicedTransactionHandler(curTr);
            delete curTr;
        }
        lockedTr.erase(lpaToUnlock);

    }

    bool Sector_Log::handleIsLockedLPA(NVM_Transaction_Flash* tr)
    {
        auto lockedTrList = lockedTr.find(tr->LPA);
        if(lockedTrList != lockedTr.end()){
            lockedTrList->second.push_back(tr);
            return true;
        } else{
            return false;
        }
    }

    void Sector_Log::servicedFromDRAMTrHandler(Memory_Transfer_Info *info)
    {
        if(maxBlockSize == 0){
            PRINT_ERROR("ERROR IN SECTOR LOG : SERVICE DRAM")
        }
        switch(info->next_event_type){
        case Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_SECTORLOG_FLUSH_FINISHED:{
            pageBuffer->flush(sectorsPerPage);
            readingDRAMForFlushing = false;
            checkFlushIsRequired();
        } break;
        case Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_SECTORLOG_READ_FINISHED:{
            userTrBufferHandler((*(std::list<NVM_Transaction_Flash_RD*>*)(info->Related_request)));
            delete ((std::list<NVM_Transaction_Flash_RD*>*)info->Related_request);
        } break;
        case Data_Cache_Simulation_Event_Type::MEMORY_WRITE_FOR_SECTORLOG_WRITE_FINISHED:{
            for(auto& tr : *((std::list<NVM_Transaction_Flash_WR*>*)info->Related_request)){
                dcmServicedTransactionHandler(tr);
            }
            for(auto& tr : *((std::list<NVM_Transaction_Flash_WR*>*)info->Related_request)){
                delete tr;
            }
            delete ((std::list<NVM_Transaction_Flash_WR*>*)info->Related_request);
        } break;
        }
    }
    
    void Sector_Log::handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash *transaction)
    {
        
        if (transaction->Source == Transaction_Source_Type::SECTORLOG_USER)
        {
            switch (transaction->Type)
            {
            // Page Buffer flush.
            case Transaction_Type::WRITE:
            {
                if(transaction->LPA == NO_LPA){
                    instance->pageBuffer->RemoveByFlush(((NVM_Transaction_Flash_WR*)transaction)->flushingID);
                }
            } break;
            case Transaction_Type::READ:
            {
                if (((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite != NULL) {
                    ((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->RelatedRead = NULL;
                    return;
                }
                instance->userTrBufferHandler(((NVM_Transaction_Flash_RD*)transaction)->originTr);
            } break;
            default:
            {
                PRINT_ERROR("ERROR IN SECTOR LOG HANDLE TRANSACTION : 2");
            } break;
            }
        }
        else if (transaction->Source == Transaction_Source_Type::SECTORLOG_MERGE)
        {
            switch (transaction->Type)
            {
                //Read data related to the victim block before the merge process is started.
                case Transaction_Type::READ:
                {
                    if (((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite != NULL) {
                        ((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->RelatedRead = NULL;
                        return;
                    }
                    instance->sectorMap->handleMergeReadArrived(((NVM_Transaction_Flash_RD*)transaction)->mergeID);
                } break;

                //Write the related blocks.
                case Transaction_Type::WRITE:
                {
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
                case Transaction_Type::ERASE:
                {
                    instance->sectorMap->eraseVictimBlock(transaction->mergeID);
                }
                break;
                default:
                {
                    PRINT_ERROR("ERROR IN SECTOR LOG HANDLE TRANSACTION : 2");
                };
            }
        }
    }

    uint32_t Sector_Log::getNextID()
    {
        static int ID = 1;
        if(ID == 10000000){
            ID = 1;
        }
        return ID++;
    }

    std::vector<uint32_t> Sector_Log::convertBitmapToSectorLocation(const page_status_type &sectorsBitmap)
    {
        std::vector<uint32_t> retValue;
        for(uint32_t sectorLocation = 0; sectorLocation < sectorsPerPage; sectorLocation++){
            if((((uint64_t)1 << sectorLocation) & sectorsBitmap) != 0){
                retValue.push_back(sectorLocation);
            }
        }
        return retValue;
    }
}