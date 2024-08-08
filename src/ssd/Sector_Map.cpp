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
        sectorLog->amu->allocateAddrForSectorLogWrite(transaction);
        
        SectorMapPage* newMapPage = new SectorMapPage(transaction->PPA);
        for(key_type& key : subPagesList){
            newMapPage->storedSubPages.push_back(key);
        }
        setMapTable(subPagesList, newMapPage);
        addPhysicalBlockTable(newMapPage);
    }

    void SectorMap::setMapTable(std::list<key_type> &subPagesList, SectorMapPage* mapEntry)
    {
        for(key_type key : subPagesList){
            if(mapTable.find(key) != mapTable.end()){
                Remove(key);
            }
            mapTable.insert({key, mapEntry});
        }
    }

    void SectorMap::addPhysicalBlockTable(SectorMapPage *pageEntry)
    {
        PPA_type pageOffset = pageEntry->ppa % sectorLog->pagesPerBlock;
        PPA_type blockAddr = pageEntry->ppa - pageOffset;
        if(physicalBlockTable.find(blockAddr) == physicalBlockTable.end()){
            physicalBlockTable.insert({blockAddr, new std::list<SectorMapPage*>(sectorLog->pagesPerBlock)});
        }
        physicalBlockTable.at(blockAddr)->push_front(pageEntry);
        pageEntry->list_itr = physicalBlockTable.at(blockAddr)->begin();
    }

    PPA_type SectorMap::getMergeBlock(std::vector<PPA_type>& sectorLogBlockList)
    {
        for(auto sectorLogBlock : sectorLogBlockList){
            if(mergingEntryList.find(sectorLogBlock) == mergingEntryList.end()){
                return sectorLogBlock;
            }
        }
        PRINT_ERROR("getMergeBlock")
    }

    bool SectorMap::checkMergeIsRequired()
    {
        if(sectorLog->bitFilter->isClusteringProcessing()){
            return false;
        }
        std::vector<PPA_type>& sectorLogBlockList = sectorLog->amu->getSectorLogBlockList(sectorLog->streamID);

        if((sectorLogBlockList.size() - mergingEntryList.size()) >= maxBlockSize){
            PPA_type victimBlockAddr = getMergeBlock(sectorLogBlockList);
            std::list<SectorMapPage*>* pagesInVictimBlock = physicalBlockTable.at(victimBlockAddr);

            std::set<LPA_type> lpaToMerge;
            for(auto page : *pagesInVictimBlock){
                for(auto key : page->storedSubPages){
                    lpaToMerge.insert(SubPageCalculator::keyToLPA(key));
                }
            }

            std::set<PPA_type> ppaToRead;
            std::list<key_type>* subPagesToRead = new std::list<key_type>();
            for(auto lpa : lpaToMerge){
                for(auto subPageOffset = 0; subPageOffset < sectorLog->subPagesPerPage; subPageOffset++){
                    key_type key = SubPageCalculator::makeKey(lpa, subPageOffset);
                    if(mapTable.find(key) != mapTable.end()){
                        ppaToRead.insert(mapTable.at(key)->ppa);
                        Remove(key);
                        sectorLog->bitFilter->removeKey(key);
                        subPagesToRead->push_back(key);
                    }
                }
                sectorLog->lockLPA(lpa);
            }

            MergingEntry* mergingEntry = new MergingEntry(victimBlockAddr, subPagesToRead);
            mergingEntryList.insert({victimBlockAddr, mergingEntry});
            
            if(ppaToRead.size() > 0){
                sectorLog->sendTSUReadForMerge(std::list<PPA_type>(ppaToRead.begin(), ppaToRead.end()));
                mergingEntry->remainReadCount = ppaToRead.size();
            } else{
                Merge(victimBlockAddr);
            }
            return true;
        } else{
            return false;
        }
    }

    void SectorMap::Merge(PPA_type blockAddr)
    {
        PPA_type c_blockAddr = blockAddr - (blockAddr % sectorLog->pagesPerBlock);
        MergingEntry* mergingEntry = mergingEntryList.at(c_blockAddr);
        NVM_Transaction_Flash_ER *eraseTr = new NVM_Transaction_Flash_ER(Transaction_Source_Type::SECTORLOG_MERGE, sectorLog->streamID, sectorLog->amu->Convert_ppa_to_address(c_blockAddr));

        std::list<key_type> keyToWrite;
        sectorLog->sendAMUWriteForMerge(*mergingEntry->mergingKeyList, eraseTr);
    }

    void SectorMap::erasePhysicalBlockTableEntry(SectorMapPage *pageEntry)
    {
        PPA_type pageOffset = pageEntry->ppa % sectorLog->pagesPerBlock;
        PPA_type blockAddr = pageEntry->ppa - pageOffset;
        auto targetBlock = physicalBlockTable.find(blockAddr);
        if(targetBlock == physicalBlockTable.end()){
            PRINT_ERROR("Erase Physical Block Table Entry")
        }
        targetBlock->second->erase(pageEntry->list_itr);
    }

    void SectorMap::handleMergeReadArrived(PPA_type blockAddr)
    {
        PPA_type c_blockAddr = blockAddr - (blockAddr % sectorLog->pagesPerBlock);
        MergingEntry* mergingEntry = mergingEntryList.at(c_blockAddr);
        mergingEntry->remainReadCount--;
        if(mergingEntry->remainReadCount == 0){
            Merge(c_blockAddr);
        }
    }

    void SectorMap::eraseVictimBlock(PPA_type blockAddr)
    {
        PPA_type c_blockAddr = blockAddr - (blockAddr % sectorLog->pagesPerBlock);
        auto mergingEntry = mergingEntryList.find(c_blockAddr);
        sectorLog->amu->erase_block_from_sectorLog(c_blockAddr);
        mergingEntryList.erase(mergingEntry);
        delete mergingEntry->second;
    }

    SectorMap::~SectorMap()
    {
        for(auto block : physicalBlockTable){
            for(auto page : *block.second){
                delete page;
            }
            delete block.second;
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
                        erasePhysicalBlockTableEntry(curMapPage);
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

    SectorMapPage::SectorMapPage(const PPA_type& in_ppa)
    {
        ppa = in_ppa;
        writtenTime = CurrentTimeStamp;
    }

    MergingEntry::MergingEntry(PPA_type in_blockAddr, std::list<key_type> *in_mergingKeyList)
    {
        this->blockAddr = in_blockAddr;
        this->mergingKeyList = in_mergingKeyList;
    }
}