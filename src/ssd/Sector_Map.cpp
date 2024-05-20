#include "Sector_Map.h"

namespace SSD_Components{


    std::vector<PPA_type>* Sector_Map::getAllRelatedPPAsInLPA(const LPA_type& lpa)
    {
        std::vector<PPA_type>* retVector = new std::vector<PPA_type>();
        retVector->resize(sectorLog->sectorsPerPage, NO_PPA);
        
        bool isAllocatedInSectorMap = false;

        for(int sectorLocation = 0; sectorLocation < sectorLog->sectorsPerPage; sectorLocation++){
            LHA_type key = lpa * sectorLog->sectorsPerPage + sectorLocation;
            auto curMapTable = mapTable.find(key);
            if(curMapTable != mapTable.end()){
                retVector->at(sectorLocation) = curMapTable->second->ppa;
                isAllocatedInSectorMap = true;
            }
        }

        if(isAllocatedInSectorMap){
            return retVector;
        } else{
            delete retVector;
            return nullptr;
        }
    }

    void Sector_Map::allocateAddr(std::list<std::pair<LPA_type, page_status_type>>& sectorsList, NVM_Transaction_Flash_WR *transaction)
    {
        if(sectorMapBlockList.empty() || sectorMapBlockList.back()->blockRecord->Current_page_write_index == sectorLog->pagesPerBlock){
            createNewBlock();
        }
        SectorMapBlock* curBlock = sectorMapBlockList.back();
        curBlock->setTrAddr(transaction);
        transaction->PPA = sectorLog->amu->Convert_address_to_ppa(transaction->Address);

        Sector_Map_Entry* newMapEntry = new Sector_Map_Entry(transaction->PPA, curBlock);
        newMapEntry->writtenTime = CurrentTimeStamp;
        newMapEntry->block = curBlock;
        for(auto lsa : sectorsList){
            newMapEntry->storedSectors.push_back(lsa);
        }

        curBlock->entryList.push_front(newMapEntry);
        newMapEntry->list_itr = curBlock->entryList.begin();

        setMapTable(sectorsList, newMapEntry);
    }

    void Sector_Map::setMapTable(std::list<std::pair<LPA_type, page_status_type>> &sectorsList, Sector_Map_Entry* mapEntry)
    {
        for(auto lsa : sectorsList){
            std::vector<uint32_t> sectorLocationList = sectorLog->convertBitmapToSectorLocation(lsa.second);
            for(auto sectorLocation : sectorLocationList){
                LHA_type key = lsa.first * sectorLog->sectorsPerPage + sectorLocation;
                mapTable.insert({key, mapEntry});
            }
        }
    }

    void Sector_Map::checkMergeIsRequired()
    {
        uint32_t mergeBlockCount = 0;
        auto victimBlock = sectorMapBlockList.begin();

        while(victimBlock != sectorMapBlockList.end() && (*victimBlock)->ongoingMerge){
            mergeBlockCount++;
            victimBlock++;
        }
        if(sectorMapBlockList.size() - mergeBlockCount >= maxBlockSize){
            (*victimBlock)->ongoingMerge = true;
            (*victimBlock)->mergeID = sectorLog->getNextID();

            std::list<Sector_Map_Entry*>& validEntryList = (*victimBlock)->entryList;

            std::set<LPA_type> lpaToMerge;

            for(auto validEntry : validEntryList){
                for(auto lsa : validEntry->storedSectors){
                    lpaToMerge.insert(lsa.first);
                }
            }

            std::list<LPA_type> lpaToLock(lpaToMerge.begin(), lpaToMerge.end());
            sectorLog->lockLPA(lpaToLock);

            std::set<PPA_type> ppaToRead;
            std::unordered_map<LPA_type, page_status_type> lsaToRead;
            for(auto lpa : lpaToMerge){
                auto curLSAToRead = lsaToRead.insert({lpa, 0}).first;
                for(int sectorLocation = 0; sectorLocation < sectorLog->sectorsPerPage; sectorLocation++){
                    LHA_type key = lpa * sectorLog->sectorsPerPage + sectorLocation;
                    if(mapTable.find(key) != mapTable.end()){
                        ppaToRead.insert(mapTable.at(key)->ppa);
                        curLSAToRead->second |= ((page_status_type)1 << sectorLocation);
                    }
                }
            }

            for(auto lsa : lsaToRead){
                if(lsa.second == 0){
                    PRINT_ERROR("ERROR IN CHECK MERGE IS REQUIRED")
                }
                Remove(lsa.first, lsa.second);
            }

            if(ppaToRead.size() > 0){
                sectorLog->sendReadForMerge(std::list<PPA_type>(ppaToRead.begin(), ppaToRead.end()), (*victimBlock)->mergeID);
                (*victimBlock)->remainReadCountForMerge = ppaToRead.size();
                (*victimBlock)->mergingLSAList = lsaToRead;
            } else{
                Merge((*victimBlock)->mergeID);
            }
        }
    }

    void Sector_Map::Merge(uint32_t mergeID)
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

        std::list<std::pair<LPA_type, page_status_type>> lsaToWrite;
        for(auto lsa : (*victimBlock)->mergingLSAList){
            lsaToWrite.push_back(lsa);
        }
        sectorLog->sendAMUWriteForMerge(lsaToWrite, eraseTr);
    }

    void Sector_Map::handleMergeReadArrived(uint32_t mergeID)
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

    void Sector_Map::eraseVictimBlock(uint32_t mergeID)
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

    Sector_Map::~Sector_Map()
    {
        for(auto block : sectorMapBlockList){
            delete block;
        }
    }

    void Sector_Map::Remove(const LPA_type &lpa, const page_status_type& sectors)
    {
        for(uint32_t sectorLocation = 0; sectorLocation < sectorLog->sectorsPerPage; sectorLocation++){
            if((sectors & ((page_status_type)1 << sectorLocation)) == 0) continue;
            LHA_type key = lpa * sectorLog->sectorsPerPage + sectorLocation;
            auto curMapTable = mapTable.find(key);
            if(curMapTable == mapTable.end()) continue;
            Sector_Map_Entry* curMapEntry = curMapTable->second;
            mapTable.erase(curMapTable);

            auto curSectors = curMapEntry->storedSectors.begin();
            while(curSectors != curMapEntry->storedSectors.end()){
                if(curSectors->first == lpa && ((((page_status_type)1 << sectorLocation) & curSectors->second) != 0)){
                    curSectors->second &= ~((page_status_type)1 << sectorLocation);
                    if(curSectors->second == 0){
                        curMapEntry->storedSectors.erase(curSectors);
                        if(curMapEntry->storedSectors.size() == 0){
                            curMapEntry->block->entryList.erase(curMapEntry->list_itr);
                            delete curMapEntry;
                        }
                    }
                    break;
                } else{
                    curSectors++;
                }
            }
            
        }

    }

    void Sector_Map::createNewBlock()
    {
        NVM::FlashMemory::Physical_Page_Address* blockAddr = new NVM::FlashMemory::Physical_Page_Address();
		PlaneBookKeepingType* planeRecordToReturn = sectorLog->amu->getColdPlane(sectorLog->streamID, blockAddr);
		Block_Pool_Slot_Type* freeBlock = planeRecordToReturn->Get_a_free_block(sectorLog->streamID, false);
		freeBlock->Holds_sector_data = true;
		blockAddr->BlockID = freeBlock->BlockID;
		sectorMapBlockList.push_back(new SectorMapBlock(blockAddr, planeRecordToReturn, freeBlock));

        checkMergeIsRequired();
    }

    Sector_Map_Entry::Sector_Map_Entry(const PPA_type& in_ppa, SectorMapBlock* in_block)
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
        for(auto entry : entryList){
            delete entry;
        }
    }
}