#include "Sector_Map.h"

namespace SSD_Components{


    SectorMapPage* SectorMap::getPageForKey(key_type key)
    {
        auto curMapTable = mapTable.find(key);
        if(curMapTable == mapTable.end()){
            return NULL;
        } else{
            return curMapTable->second;
        }
    }

    void SectorMap::allocatePage(std::list<key_type>& subPagesList, NVM_Transaction_Flash_WR *transaction)
    {
        if(sectorMapBlockList.empty() || sectorMapBlockList.back()->blockRecord->Current_page_write_index == sectorLog->pagesPerBlock){
            createNewBlock();
        }
        SectorMapBlock* curBlock = sectorMapBlockList.back();
        curBlock->setTrAddr(transaction);
        transaction->PPA = sectorLog->amu->Convert_address_to_ppa(transaction->Address);

        SectorMapPage* newMapPage = new SectorMapPage(transaction->PPA, curBlock);
        newMapPage->writtenTime = CurrentTimeStamp;
        newMapPage->block = curBlock;
        for(key_type& key : subPagesList){
            newMapPage->storedSubPages.push_back(key);
        }

        curBlock->pageList.push_front(newMapPage);
        newMapPage->list_itr = curBlock->pageList.begin();

        setMapTable(subPagesList, newMapPage);
    }

    void SectorMap::setMapTable(std::list<key_type> &subPagesList, SectorMapPage* mapEntry)
    {
        for(key_type key : subPagesList){
            if(mapTable.find(key) != mapTable.end()){
                PRINT_ERROR("ERROR IN SECTOR MAP - KEY IS ALREADY IN THE MAP TABLE : " << key)
            }
            mapTable.insert({key, mapEntry});
        }
    }

    void SectorMap::checkMergeIsRequired()
    {
        if(sectorLog->bitFilter->isClusteringProcessing()) return;
        uint32_t mergeBlockCount = 0;
        auto victimBlock = sectorMapBlockList.begin();

        while(victimBlock != sectorMapBlockList.end() && (*victimBlock)->ongoingMerge){
            mergeBlockCount++;
            victimBlock++;
        }
        if(sectorMapBlockList.size() - mergeBlockCount >= maxBlockSize){
            (*victimBlock)->ongoingMerge = true;
            (*victimBlock)->mergeID = sectorLog->getNextID();

            std::list<SectorMapPage*>& validPageList = (*victimBlock)->pageList;

            std::set<LPA_type> lpaToMerge;

            for(auto validPage : validPageList){
                for(auto key : validPage->storedSubPages){
                    lpaToMerge.insert(SubPageCalculator::keyToLPA(key));
                }
            }

            std::list<LPA_type> lpaToLock(lpaToMerge.begin(), lpaToMerge.end());
            sectorLog->lockLPA(lpaToLock);

            std::set<PPA_type> ppaToRead;
            std::list<key_type> subPagesToRead;
            for(auto lpa : lpaToMerge){
                for(auto subPageOffset = 0; subPageOffset < sectorLog->subPagesPerPage; subPageOffset++){
                    key_type key = SubPageCalculator::makeKey(lpa, subPageOffset);
                    if(mapTable.find(key) != mapTable.end()){
                        ppaToRead.insert(mapTable.at(key)->ppa);
                        subPagesToRead.push_back(key);
                    }
                }
            }


            for(auto key : subPagesToRead){
                sectorLog->bitFilter->removeBit(key);
                (*victimBlock)->mergingKeyList.push_back(key);
                Remove(key);
            }
            if(ppaToRead.size() > 0){
                sectorLog->sendReadForMerge(std::list<PPA_type>(ppaToRead.begin(), ppaToRead.end()), (*victimBlock)->mergeID);
                (*victimBlock)->remainReadCountForMerge = ppaToRead.size();
            } else{
                Merge((*victimBlock)->mergeID);
            }
        }
    }

    void SectorMap::Merge(uint32_t mergeID)
    {
        auto victimBlock = sectorMapBlockList.begin();
        while((*victimBlock)->mergeID != mergeID){
            victimBlock++;
        }
        if (!(*victimBlock)->ongoingMerge && (*victimBlock)->remainReadCountForMerge != 0)
        {
            PRINT_ERROR("ERROR IN SECTOR LOG MERGE : 2")
        }
        NVM_Transaction_Flash_ER *eraseTr = new NVM_Transaction_Flash_ER(Transaction_Source_Type::SECTORLOG_MERGE, sectorLog->streamID, (*(*victimBlock)->blockAddr));
        eraseTr->mergeID = (*victimBlock)->mergeID;

        std::list<key_type> keyToWrite;
        for(auto key : (*victimBlock)->mergingKeyList){
            keyToWrite.push_back(key);
        }
        sectorLog->sendAMUWriteForMerge(keyToWrite, eraseTr);
    }

    void SectorMap::handleMergeReadArrived(uint32_t mergeID)
    {
        auto victimBlock = sectorMapBlockList.begin();
        while((*victimBlock)->mergeID != mergeID){
            victimBlock++;
        }
        (*victimBlock)->remainReadCountForMerge--;
        if((*victimBlock)->remainReadCountForMerge == 0){
            Merge(mergeID);
        }
    }

    void SectorMap::eraseVictimBlock(uint32_t mergeID)
    {
        auto victimBlock = sectorMapBlockList.begin();
        while((*victimBlock)->mergeID != mergeID){
            victimBlock++;
        }
        sectorMapBlockList.remove((*victimBlock));
        (*victimBlock)->blockRecord->Invalid_page_count += (*victimBlock)->blockRecord->Current_page_write_index;
        sectorLog->amu->erase_block_from_sectorLog(*(*victimBlock)->blockAddr);
        delete (*victimBlock);
    }

    SectorMap::~SectorMap()
    {
        for(auto block : sectorMapBlockList){
            delete block;
        }
    }

    void SectorMap::Remove(key_type key)
    {
        auto curMapTable = mapTable.find(key);
        if(curMapTable != mapTable.end()){
            SectorMapPage* curMapPage = curMapTable->second;
            auto subPageItr = curMapPage->storedSubPages.begin();
            while(true){
                if((*subPageItr) == key){
                    curMapPage->storedSubPages.erase(subPageItr);
                    if(curMapPage->storedSubPages.size() == 0){
                        curMapPage->block->pageList.erase(curMapPage->list_itr);
                        delete curMapPage;
                    }
                    break;
                } else{
                    subPageItr++;
                }
            }
            mapTable.erase(curMapTable);
        } else{
            PRINT_ERROR("SECTOR MAP REMOVE - THERE ARE NO KEY : " << key)
        }
    }

    void SectorMap::createNewBlock()
    {
        NVM::FlashMemory::Physical_Page_Address* blockAddr = new NVM::FlashMemory::Physical_Page_Address();
		PlaneBookKeepingType* planeRecordToReturn = sectorLog->amu->getColdPlane(sectorLog->streamID, blockAddr);
		Block_Pool_Slot_Type* freeBlock = planeRecordToReturn->Get_a_free_block(sectorLog->streamID, false);
		freeBlock->Holds_sector_data = true;
		blockAddr->BlockID = freeBlock->BlockID;
		sectorMapBlockList.push_back(new SectorMapBlock(blockAddr, planeRecordToReturn, freeBlock));

        checkMergeIsRequired();
    }

    SectorMapPage::SectorMapPage(const PPA_type& in_ppa, SectorMapBlock* in_block)
    {
        ppa = in_ppa;
        block = in_block;
        writtenTime = 0;
    }

    void SectorMapBlock::setTrAddr(NVM_Transaction_Flash_WR *tr)
    {
        planeRecord->Valid_pages_count++;
        planeRecord->Free_pages_count--;
        tr->Address = (*blockAddr);
        tr->Address.PageID = blockRecord->Current_page_write_index++;
        planeRecord->Check_bookkeeping_correctness(tr->Address);
    }

    SectorMapBlock::SectorMapBlock(NVM::FlashMemory::Physical_Page_Address *in_blockAddr, PlaneBookKeepingType *in_planeRecord, Block_Pool_Slot_Type *in_blockRecord)
    {
        blockAddr = in_blockAddr;
        planeRecord = in_planeRecord;
        blockRecord = in_blockRecord;
        remainReadCountForMerge = 0;
        ongoingMerge = false;
        mergeID = 0;
    }

    SectorMapBlock::~SectorMapBlock()
    {
        for(auto entry : pageList){
            delete entry;
        }
    }
}