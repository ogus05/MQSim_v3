#include "Sector_Log.h"
#include "Address_Mapping_Unit_Page_Level.h"
#include "Data_Cache_Flash.h"
#include "Page_Buffer.h"
#include "Sector_Map.h"

namespace SSD_Components
{
    // Return whether the 2pages are in the same block or not.
    bool checkEqualBlockAddr(NVM::FlashMemory::Physical_Page_Address* addr1, NVM::FlashMemory::Physical_Page_Address* addr2){
        return (addr1->ChannelID == addr2->ChannelID && addr1->ChipID == addr2->ChipID && addr1->DieID == addr2->DieID &&
                    addr1->PlaneID == addr2->PlaneID && addr1->BlockID == addr2->BlockID);
    }
    
    Sector_Log* Sector_Log::instance = NULL;
    Sector_Log::Sector_Log(const stream_id_type& in_streamID, const uint32_t& in_sectorsPerPage, const uint32_t& in_pagesPerBlock, const uint32_t& in_maxBlockSize,
    Address_Mapping_Unit_Page_Level *in_amu, TSU_Base* in_tsu, Data_Cache_Manager_Base* in_dcm){
        
        maxBlockSize = in_maxBlockSize;        //in_maxBlockSize;
        sectorsPerPage = in_sectorsPerPage;
        pagesPerBlock = in_pagesPerBlock;
        streamID = in_streamID;

        amu = in_amu;
        tsu = in_tsu;
        dcm = in_dcm;

        instance = this;
        sectorMap = new Sector_Map(this);
        pageBuffer = new Page_Buffer(sectorsPerPage, this);
        sectorLogWF = std::list<Sector_Log_WF_Entry*>();
        ongoingFlush = false;
    }

    Sector_Log::~Sector_Log()
    {  
        delete sectorMap;
        delete pageBuffer;
        for(auto& e : sectorLogWF){
            delete e;
        }
    }

    void Sector_Log::setCompleteTrHandler(void (*completeTrHandler)(NVM_Transaction_Flash *))
    {
        this->dcmServicedTransactionHandler = completeTrHandler;
    }

    void Sector_Log::allocate_page_in_sector_area(NVM_Transaction_Flash_WR *transaction)
    {
        if(sectorLogWF.empty() || sectorLogWF.back()->blockRecord->Current_page_write_index >= pagesPerBlock){
            amu->allocate_block_for_sectorLog(streamID, sectorLogWF);
            checkMergeIsRequired();
        }
        Sector_Log_WF_Entry* blockToInsertPage = sectorLogWF.back();
        blockToInsertPage->planeRecord->Valid_pages_count++;
        blockToInsertPage->planeRecord->Free_pages_count--;
        transaction->Address = (*blockToInsertPage->blockAddr);
        transaction->Address.PageID = sectorLogWF.back()->blockRecord->Current_page_write_index++;
        blockToInsertPage->planeRecord->Check_bookkeeping_correctness(transaction->Address);

    }

    void Sector_Log::checkMergeIsRequired()
    {
        if(sectorLogWF.size() >= maxBlockSize){
            std::set<PPA_type> PPAsToRead;
            auto victimBlock = sectorLogWF.begin();
            if((*victimBlock)->ongoingMerge || (*victimBlock)->prepareMerge)
            {
                PRINT_ERROR("ERROR IN PREPARE MERGE : THERE IS ALREADY A BLOCK MERGING")
            }
            (*victimBlock)->prepareMerge = true;

            std::set<LPA_type> victimLPAList;
            for(auto& victimLPA : (*victimBlock)->storeSectors){
                victimLPAList.insert(victimLPA.first);
                if(victimLPA.second == 0){
                    PRINT_ERROR("ERROR IN CHECK MERGE IS REQUIRED")
                }
            }

            for(auto& lpa : victimLPAList){
                std::vector<Sector_Map_Entry>* sectorMapEntryList = sectorMap->getAllRelatedPPAsInLPA(lpa);
                if(sectorMapEntryList != NULL){
                    for(page_status_type sectorLocation = 0; sectorLocation < sectorMapEntryList->size(); sectorLocation++){
                        if(sectorMapEntryList->at(sectorLocation).ppa != NO_PPA){
                            if((*victimBlock)->LSAOnMerge.find(lpa) == (*victimBlock)->LSAOnMerge.end()){
                                (*victimBlock)->LSAOnMerge.insert({lpa, (1 << sectorLocation)});
                            } else{
                                (*victimBlock)->LSAOnMerge.at(lpa) = (((*victimBlock)->LSAOnMerge.at(lpa) | ((uint64_t)1 << sectorLocation)));
                            }
                            PPAsToRead.insert(sectorMapEntryList->at(sectorLocation).ppa);
                        }
                    }
                }
            }

            if(PPAsToRead.size() > 0){
                tsu->Prepare_for_transaction_submit();
                for (auto &ppa : PPAsToRead)
                {
                    NVM_Transaction_Flash_RD *readSectorAreaTr = new NVM_Transaction_Flash_RD(Transaction_Source_Type::SECTORLOG_MERGE, streamID,
                                                                                            sectorsPerPage * SECTOR_SIZE_IN_BYTE, NO_LPA, ppa, NULL, 0, TO_FULL_PAGE(sectorsPerPage), CurrentTimeStamp);
                    readSectorAreaTr->Address = amu->Convert_ppa_to_address(ppa);
                    tsu->Submit_transaction(readSectorAreaTr);
                    waitingTrPrepareMerge.insert({ppa, new std::list<NVM_Transaction_Flash_RD *>()});
                    (*victimBlock)->remainReadCountForMerge++;
                }
                tsu->Schedule();
            } else{
                Merge(*victimBlock);
            }
        }
    }

    void Sector_Log::Merge(Sector_Log_WF_Entry *in_entry)
    {
        if (!in_entry->prepareMerge && (in_entry->remainReadCountForMerge != 0))
        {
            PRINT_ERROR("ERROR IN SECTOR LOG MERGE")
        }
        in_entry->prepareMerge = false;
        in_entry->ongoingMerge = true;
        NVM_Transaction_Flash_ER *eraseTr = new NVM_Transaction_Flash_ER(Transaction_Source_Type::SECTORLOG_MERGE, streamID, (*in_entry->blockAddr));
        std::list<NVM_Transaction*> transferToAMUList;

        for (auto &LSA : in_entry->LSAOnMerge)
        {
            const LPA_type LPA = LSA.first;
            const page_status_type sectorsBitmap = LSA.second;
            // If the sectors in the sector area can be merged to the full page, transferred to the Address Mapping Unit.
            NVM_Transaction_Flash_WR *pageWriteTr = new NVM_Transaction_Flash_WR(
                    Transaction_Source_Type::SECTORLOG_MERGE, streamID, count_sector_no_from_status_bitmap(sectorsBitmap) * SECTOR_SIZE_IN_BYTE, LPA, NULL, 0, sectorsBitmap, CurrentTimeStamp);
            eraseTr->Page_movement_activities.push_back(pageWriteTr);
            pageWriteTr->RelatedErase = eraseTr;
            transferToAMUList.push_back(pageWriteTr);
            sectorMap->Remove(LPA);
        }
        if(transferToAMUList.size() > 0){
            amu->Translate_lpa_to_ppa_and_dispatch(transferToAMUList);
        } else{
            tsu->Prepare_for_transaction_submit();
            tsu->Submit_transaction(eraseTr);
            tsu->Schedule();
        }
    }

    void Sector_Log::flushPageBuffer()
    {
        const std::unordered_map<LPA_type, Page_Buffer_Entry*> pageBufferEntries = pageBuffer->GetAll();
        if((count_sector_no_from_status_bitmap((*pageBufferEntries.begin()).second->sectorsBitmap) == sectorsPerPage)){
            NVM_Transaction_Flash_WR *fullPageWrite = new NVM_Transaction_Flash_WR(Transaction_Source_Type::SECTORLOG_USER,
                                                                                        streamID, sectorsPerPage * SECTOR_SIZE_IN_BYTE, (*pageBufferEntries.begin()).first, NULL, IO_Flow_Priority_Class::URGENT, 0, TO_FULL_PAGE(sectorsPerPage), CurrentTimeStamp);
            amu->Translate_lpa_to_ppa_and_dispatch(std::list<NVM_Transaction*>({fullPageWrite}));
        } else{
            NVM_Transaction_Flash_WR *partialPageWrite = new NVM_Transaction_Flash_WR(Transaction_Source_Type::SECTORLOG_USER,
                                                                                        streamID, sectorsPerPage * SECTOR_SIZE_IN_BYTE, NO_LPA, NULL, IO_Flow_Priority_Class::URGENT, 0, TO_FULL_PAGE(sectorsPerPage), CurrentTimeStamp);
            allocate_page_in_sector_area(partialPageWrite);
            partialPageWrite->PPA = amu->Convert_address_to_ppa(partialPageWrite->Address);
            tsu->Prepare_for_transaction_submit();
            tsu->Submit_transaction(partialPageWrite);
            tsu->Schedule();
        }
    }

    void Sector_Log::flushTrServicedHandler(const PPA_type& ppa)
    {
        const std::unordered_map<LPA_type, Page_Buffer_Entry*> pageBufferEntries = pageBuffer->GetAll();
        auto targetEntry = sectorLogWF.rbegin();
        NVM::FlashMemory::Physical_Page_Address addr = amu->Convert_ppa_to_address(ppa);
        while(targetEntry != sectorLogWF.rend()){
            if(checkEqualBlockAddr((*targetEntry)->blockAddr, &addr)){
                break;
            } else{
                targetEntry++;
            }
        }
        
        for (auto &e : pageBufferEntries)
        {
            for (uint32_t flag = 0; flag < sectorsPerPage; flag++)
            {
                if ((e.second->sectorsBitmap & ((uint64_t)1 << flag)) > 0)
                {
                    sectorMap->Insert(e.first, flag, ppa, (*targetEntry));
                }
            }
        }
        pageBuffer->RemoveAll();

        waitingPageBufferFreeSpaceTrHandler();
    }

    //After the erase transaction in merge process is serviced.
    void Sector_Log::eraseSectorLogWFEntry(Sector_Log_WF_Entry *entryToRemove)
    {
        sectorLogWF.remove(entryToRemove);
        entryToRemove->blockRecord->Invalid_page_count += entryToRemove->blockRecord->Current_page_write_index;
        amu->erase_block_from_sectorLog(*entryToRemove->blockAddr);
        delete entryToRemove;
    }

    void Sector_Log::waitingPrepareMergeTrHandler(const PPA_type &ppa)
    {
        std::list<NVM_Transaction_Flash_RD*>* waitingTrList = waitingTrPrepareMerge.at(ppa);
        for(auto& waitingTr: *waitingTrList){
            userTrBufferHandler(waitingTr);
        }
        waitingTrPrepareMerge.erase(ppa);
        delete waitingTrList;
    }

    void Sector_Log::waitingPageBufferFreeSpaceTrHandler()
    {
        page_status_type remainSectorBitmap = 0;
        std::list<NVM_Transaction_Flash_WR*>* writeToPageBufferTrList = new std::list<NVM_Transaction_Flash_WR*>();
        uint32_t pageBufferWriteSizeInSectors = 0;
        auto waitingEntry = waitingTrPageBufferFreeSpace.begin();
        while(waitingEntry != waitingTrPageBufferFreeSpace.end()){
            NVM_Transaction_Flash_WR* waitingTr = waitingEntry->first;
            page_status_type& sectorsToInsert = waitingEntry->second;

            pageBuffer->Insert(waitingTr->LPA, sectorsToInsert, remainSectorBitmap);
            if(remainSectorBitmap > 0){
                pageBufferWriteSizeInSectors += count_sector_no_from_status_bitmap(waitingEntry->second & ~(remainSectorBitmap));
                waitingEntry->second = remainSectorBitmap;
                if(sectorLogWF.empty() || ((!(sectorLogWF.front()->ongoingMerge) && !(sectorLogWF.front()->prepareMerge)) && !(ongoingFlush))){
                    ongoingFlush = true;
                    Memory_Transfer_Info* flushTransferInfo = new Memory_Transfer_Info;
                    flushTransferInfo->Size_in_bytes = sectorsPerPage * SECTOR_SIZE_IN_BYTE;
                    flushTransferInfo->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_SECTORLOG_FLUSH_FINISHED;
                    flushTransferInfo->Stream_id = streamID;
                    dcm->service_dram_access_request(flushTransferInfo);
                }
                break;
            } else{
                waitingEntry = waitingTrPageBufferFreeSpace.erase(waitingEntry);
                pageBufferWriteSizeInSectors += count_sector_no_from_status_bitmap(sectorsToInsert);
                writeToPageBufferTrList->push_back(waitingTr);
            }
        }
        if(pageBufferWriteSizeInSectors > 0){
            Memory_Transfer_Info* writeTransferInfo = new Memory_Transfer_Info;
            writeTransferInfo->Size_in_bytes = pageBufferWriteSizeInSectors * SECTOR_SIZE_IN_BYTE;
            writeTransferInfo->Related_request = writeToPageBufferTrList;
            writeTransferInfo->next_event_type = Data_Cache_Simulation_Event_Type::MEMORY_WRITE_FOR_SECTORLOG_WRITE_FINISHED;
            writeTransferInfo->Stream_id = streamID;
            dcm->service_dram_access_request(writeTransferInfo);
        } else{
            delete writeToPageBufferTrList;
        }
    }

    void Sector_Log::userTrBufferHandler(NVM_Transaction_Flash_RD * transaction)
    {
        auto itr = userTrBuffer.find(transaction);
        if(itr == userTrBuffer.end()){
            PRINT_ERROR("USER TRANSACTION BUFFER HANDLER - NO EXISTS TRANSACTION")
        }
        itr->second--;
        
        if(itr->second < 0){
            PRINT_ERROR("USER TRANSACTION BUFFER HANDLER - ERROR IN REMAIN TRANSACTIONS")
        } 
        if(itr->second == 0){
            userTrBuffer.erase(itr);
            dcmServicedTransactionHandler(transaction);
            delete transaction;
        }

    }

    void Sector_Log::handleInputTransaction(std::list<NVM_Transaction *> transaction_list)
    {
        if(maxBlockSize == 0){
            amu->Translate_lpa_to_ppa_and_dispatch(transaction_list);
        }
        else if (transaction_list.front()->Type == SSD_Components::Transaction_Type::WRITE)
        {
            std::list<NVM_Transaction*> fullPageWriteList;

            for (auto& transaction : transaction_list)
            {
                NVM_Transaction_Flash_WR *tr = (NVM_Transaction_Flash_WR *)transaction;
                //Full page should be transferred to the AMU.
                if((count_sector_no_from_status_bitmap(tr->write_sectors_bitmap) == sectorsPerPage)){
                    //Before transfer to amu, remove all the related data in the Sector Log.
                    pageBuffer->Remove(tr->LPA, tr->write_sectors_bitmap);
                    sectorMap->Remove(tr->LPA);
                    fullPageWriteList.push_back(tr);
                }
                //Partial page should be transferred to the Page Buffer.
                else
                {
                    waitingTrPageBufferFreeSpace.push_back({tr, tr->write_sectors_bitmap});
                }
            }
            //remainSectors can indicate whether the Page Buffer is full or not.
            if(waitingTrPageBufferFreeSpace.size() > 0){
                waitingPageBufferFreeSpaceTrHandler();
            }

            if(fullPageWriteList.size() > 0) {
                amu->Translate_lpa_to_ppa_and_dispatch(fullPageWriteList);
            }
        }
        //All of the read transactions should look up the Page Buffer, Sector Map.
        else if (transaction_list.front()->Type == SSD_Components::Transaction_Type::READ)
        {
            //If Sector Area doesn't store all of the sectors the read transaction needed,
            //A new read transaction is created and the origin transaction are temporarily buffered(Sector_Log::userReadBuffer).
            //This variable stores the new read transactions created from the transaction_list which should be transferred to AMU.
            std::list<NVM_Transaction *> transactionListForTransferAMU;
            
            //Read transactions desitnated to the Sector Area are stored in this variable.
            std::list<NVM_Transaction_Flash_RD *> transactionListForTransferTSU;

            std::list<NVM_Transaction_Flash_RD*> completeTrList;

            std::list<NVM_Transaction_Flash_RD*>* partialPageReadFromDRAM = new std::list<NVM_Transaction_Flash_RD*>();
            uint32_t sectorSizeToReadDRAM = 0;

            for (auto it = transaction_list.begin(); it != transaction_list.end(); it++)
            {
                NVM_Transaction_Flash_RD *tr = (NVM_Transaction_Flash_RD *)(*it);
                page_status_type sectorsToRead = tr->read_sectors_bitmap;
                uint32_t dataSizeToRead = tr->Data_and_metadata_size_in_byte;
                //The sectors ongoing merge process are supposed to exist in the controller. So handled immediatly too. 
                for(auto& wfEntry : sectorLogWF){
                    if(wfEntry->ongoingMerge){
                        auto itr = wfEntry->LSAOnMerge.find(tr->LPA);
                        if(itr != wfEntry->LSAOnMerge.end()){
                            page_status_type sectorsOnMerge = itr->second;
                            dataSizeToRead -= count_sector_no_from_status_bitmap(sectorsToRead & sectorsOnMerge) * SECTOR_SIZE_IN_BYTE;
                            sectorsToRead = (sectorsToRead & ~(sectorsOnMerge));
                        }
                    }
                    //Because merge process is started from the head of the sectorLogWF.
                    else{
                        break;
                    }
                }
                
                page_status_type sectorsExistInPageBuffer = pageBuffer->Exists(tr->LPA);

                userTrBuffer.insert({tr, 0});
                if((sectorsToRead & sectorsExistInPageBuffer) > 0){
                    dataSizeToRead -= count_sector_no_from_status_bitmap(sectorsToRead & sectorsExistInPageBuffer) * SECTOR_SIZE_IN_BYTE;
                    sectorSizeToReadDRAM += count_sector_no_from_status_bitmap(sectorsToRead & sectorsExistInPageBuffer);
                    
                    sectorsToRead = (sectorsToRead & ~(sectorsExistInPageBuffer));
                    partialPageReadFromDRAM->push_back(tr);
                    userTrBuffer.at(tr)++;
                }
                if (sectorsToRead == 0)
                {
                    if(userTrBuffer.at(tr) == 0){
                        completeTrList.push_back(tr);
                        userTrBuffer.erase(tr);
                    }
                    continue;
                }



                //PPAs for handle the sectors in the second.
                std::unordered_map<PPA_type, page_status_type> PPAForReadTransaction;

                std::vector<Sector_Map_Entry>* relatedPPA = sectorMap->getAllRelatedPPAsInLPA(tr->LPA);
                if(relatedPPA != NULL){
                    for(uint32_t sectorLocation = 0; sectorLocation < relatedPPA->size(); sectorLocation++){
                        //Look up all of the sectors should be handled by the Sector Log. 
                        if((((uint64_t)1 << sectorLocation) & sectorsToRead) > 0){
                            PPA_type PPAForCurSector = relatedPPA->at(sectorLocation).ppa;
                            if(PPAForCurSector != NO_PPA){
                                if(PPAForReadTransaction.find(PPAForCurSector) == PPAForReadTransaction.end()){
                                    PPAForReadTransaction.insert({PPAForCurSector, ((uint64_t)1 << sectorLocation)});
                                } else{
                                    PPAForReadTransaction.at(PPAForCurSector) |= ((uint64_t)1 << sectorLocation);
                                }
                                sectorsToRead = (sectorsToRead & ~((uint64_t)1 << sectorLocation));
                                dataSizeToRead -= 1 * SECTOR_SIZE_IN_BYTE;
                            }
                        }
                    }
                }

                userTrBuffer.at(tr) += PPAForReadTransaction.size();
                for(auto& PPA : PPAForReadTransaction){
                    //If the page is reading for the merge process, it would be handled after the read process.
                    if(waitingTrPrepareMerge.find(PPA.first) != waitingTrPrepareMerge.end()){
                        waitingTrPrepareMerge.at(PPA.first)->push_back(tr);
                    }
                    //Create the transactions for read Sector Area.
                    //These are already translated to PPA through the Sector Map.
                    else{
                        NVM_Transaction_Flash_RD* readSectorArea = new NVM_Transaction_Flash_RD(Transaction_Source_Type::SECTORLOG_USER, streamID,
                        count_sector_no_from_status_bitmap(PPA.second) * SECTOR_SIZE_IN_BYTE, NO_LPA, PPA.first, NULL, tr->Priority_class, 0, PPA.second, CurrentTimeStamp);
                        readSectorArea->originTr = tr;
                        readSectorArea->Address = amu->Convert_ppa_to_address(readSectorArea->PPA);
                        transactionListForTransferTSU.push_back(readSectorArea);
                    }
                    
                }
                //If the Sector Area doesn't store all of the sectors, a read transaction read the data area is tranferred to AMU.
                if(sectorsToRead > 0){     
                    NVM_Transaction_Flash_RD* readDataArea = new NVM_Transaction_Flash_RD(Transaction_Source_Type::SECTORLOG_USER, streamID,
                        dataSizeToRead, tr->LPA, NO_PPA, NULL, tr->Priority_class, 0, sectorsToRead, CurrentTimeStamp);
                    readDataArea->originTr = tr;
                    transactionListForTransferAMU.push_back(readDataArea);
                    userTrBuffer.at(tr)++;
                }
            }

            for(auto& completeTr : completeTrList){
                dcmServicedTransactionHandler(completeTr);
            }

            if(transactionListForTransferTSU.size() > 0){
                tsu->Prepare_for_transaction_submit();
                for(auto& tr: transactionListForTransferTSU){
                    tsu->Submit_transaction(tr);
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
    }
    
    void Sector_Log::servicedFromDRAMTrHandler(Memory_Transfer_Info *info)
    {
        if(maxBlockSize == 0){
            PRINT_ERROR("ERROR IN SECTOR LOG : SERVICE DRAM")
        }
        switch(info->next_event_type){
        case Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_SECTORLOG_FLUSH_FINISHED:{
            flushPageBuffer();
        } break;
        case Data_Cache_Simulation_Event_Type::MEMORY_READ_FOR_SECTORLOG_READ_FINISHED:{
            for(auto& tr : *((std::list<NVM_Transaction_Flash_RD*>*)info->Related_request)){
                userTrBufferHandler(tr);
            }
            delete ((std::list<NVM_Transaction_Flash_RD*>*)info->Related_request);
        } break;
        case Data_Cache_Simulation_Event_Type::MEMORY_WRITE_FOR_SECTORLOG_WRITE_FINISHED:{
            for(auto& tr : *((std::list<NVM_Transaction_Flash_WR*>*)info->Related_request)){
                dcmServicedTransactionHandler(tr);
            }
            for(auto& tr : *((std::list<NVM_Transaction_Flash_RD*>*)info->Related_request)){
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
                instance->ongoingFlush = false;
                if(transaction->LPA == NO_LPA){
                    instance->flushTrServicedHandler(transaction->PPA);
                } else{
                    instance->pageBuffer->RemoveAll();
                    instance->waitingPageBufferFreeSpaceTrHandler();
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
                    auto targetEntry = instance->sectorLogWF.begin();
                    //If all of the read data is fetched, start merge process.
                    (*targetEntry)->remainReadCountForMerge--;
                    if ((*targetEntry)->remainReadCountForMerge == 0)
                    {
                        instance->Merge(*targetEntry);
                    }
                    instance->waitingPrepareMergeTrHandler(transaction->PPA);
                }
                break;

                //Write the related blocks.
                case Transaction_Type::WRITE:
                {
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
                    auto targetEntry = instance->sectorLogWF.begin();
                    if (!(*targetEntry)->ongoingMerge)
                    {
                        PRINT_ERROR("ERROR IN SECTOR LOG HANDLE MERGE ERASE")
                    }
                    instance->eraseSectorLogWFEntry(*targetEntry);
                    instance->waitingPageBufferFreeSpaceTrHandler();
                }
                break;
                default:
                {
                    PRINT_ERROR("ERROR IN SECTOR LOG HANDLE TRANSACTION : 2");
                };
            }
        }
    }
    
    Sector_Log_WF_Entry::Sector_Log_WF_Entry(NVM::FlashMemory::Physical_Page_Address *in_blockAddr, PlaneBookKeepingType *in_planeRecord, Block_Pool_Slot_Type *in_blockRecord)
    :blockAddr(in_blockAddr), planeRecord(in_planeRecord), blockRecord(in_blockRecord) {
        prepareMerge = false;
        ongoingMerge = false;
        remainReadCountForMerge = 0;
    };
}