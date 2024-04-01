#include "LSMSectorMap.h"
#include "LSMSectorLog.h"
#include "Data_Cache_Flash.h"
#include "TSU_Base.h"
#include "Address_Mapping_Unit_Page_Level.h"

namespace SSD_Components{
    LSMSectorMapEntry::LSMSectorMapEntry(NVM::FlashMemory::Physical_Page_Address* pba)
    {
        this->lastWrittenPage = pba;
        invalidSectorsCount = 0;
        highLPA = 0;
        lowLPA = 0;
        pba->PageID = 0;
        victim = false;
        insertingPage = false;
    }

    LSMSectorMapEntry::~LSMSectorMapEntry()
    {
        for(auto e : info){
            for(auto e2 : e.second){
                delete e2;
            }
        }
        delete lastWrittenPage;
    }

    LSMSectorMap::LSMSectorMap(const uint32_t maxBlockCount, const uint32_t level, LSMSectorMap* upperLevelSectorMap)
        : maxBlockCount(maxBlockCount), level(level), upperLevelSectorMap(upperLevelSectorMap) {}

    LSMSectorMap::~LSMSectorMap()
    {
        for(auto e : mappingLPA){
            for(auto e2 : e.second)
            delete e2;
        }
    }

    void LSMSectorMapEntry::Insert(const std::list<LSA_type> &sectorsInsertToOnePage, const PPA_type& ppa)
    {
        if(!insertingPage){
            PRINT_ERROR("ERROR IN SECTOR MAP ENTRY INSERT")
        }
        insertingPage = false;

        std::shared_ptr<std::unordered_set<LSMSectorMapInfo*>> insertedInfoList = std::make_shared<std::unordered_set<LSMSectorMapInfo*>>();
        for(auto& curLSA : sectorsInsertToOnePage){
            LSMSectorMapInfo* newInfo = new LSMSectorMapInfo(curLSA, ppa);
            insertedInfoList->insert(newInfo);
            newInfo->infoInCurPage = insertedInfoList;

            auto targetInfoList = info.find(curLSA.lpa);
            if(targetInfoList != info.end()){
                targetInfoList->second.push_back(newInfo);
            } else{
                targetInfoList = info.insert({curLSA.lpa, std::list<LSMSectorMapInfo*>()}).first;
                targetInfoList->second.push_back(newInfo);
            }
        }
    }

    std::list<LSMSectorMapReadingInfo *> LSMSectorMap::LookUp(LSA_type &LSAToLookUp)
    {
        std::list<LSMSectorMapReadingInfo*> entriesToRet;
        auto entryListToLookUp = mappingLPA.find(LSAToLookUp.lpa);
        if(entryListToLookUp != mappingLPA.end()){
            for(auto& curEntryToLookUp : entryListToLookUp->second){
                std::list<LSMSectorMapReadingInfo*> curEntriesToRet = curEntryToLookUp->LookUp(LSAToLookUp);
                if(curEntriesToRet.size() > 0){
                    entriesToRet.insert(entriesToRet.begin(), curEntriesToRet.begin(), curEntriesToRet.end());
                    if(LSAToLookUp.sectorsBitmap == 0) break;
                }
            }
        }
        return entriesToRet;
    }

    std::list<LSMSectorMapReadingInfo *> LSMSectorMapEntry::LookUp(LSA_type &LSAToLookUp)
    {
        std::list<LSMSectorMapReadingInfo*> readingInfoToRet;
        if((lowLPA <= LSAToLookUp.lpa && LSAToLookUp.lpa <= highLPA) && !victim){
            auto infoItr = info.find(LSAToLookUp.lpa);
            if(infoItr != info.end()){
                for(auto curInfo : infoItr->second){
                    LSA_type& curLSA = curInfo->lsa;
                    if((curLSA.sectorsBitmap & LSAToLookUp.sectorsBitmap) > 0){
                        std::unordered_map<LPA_type, page_status_type> LSAInPPA;
                        for(auto infoInCurPage : (*curInfo->infoInCurPage)){
                            LSAInPPA.insert({infoInCurPage->lsa.lpa, infoInCurPage->lsa.sectorsBitmap});
                        }
                        readingInfoToRet.push_back(new LSMSectorMapReadingInfo(curInfo->ppa, LSAInPPA));
                        LSAToLookUp.sectorsBitmap &= ~(curLSA.sectorsBitmap);
                        if(LSAToLookUp.sectorsBitmap == 0) break;
                    }
                }
            }
        }
        return readingInfoToRet;
    }
    
    page_status_type LSMSectorMap::Invalidate(const LSA_type &lsa)
    {
        page_status_type sectorsToInvalidate = lsa.sectorsBitmap;

        if(mappingLPA.find(lsa.lpa) != mappingLPA.end()){
            for(auto curEntry : mappingLPA.at(lsa.lpa)){
                sectorsToInvalidate &= ~(curEntry->Invalidate(lsa));
                if(sectorsToInvalidate == 0) break;
            }
        }

        return (lsa.sectorsBitmap & ~(sectorsToInvalidate));
    }

    page_status_type LSMSectorMapEntry::Invalidate(const LSA_type &lsa)
    {
        page_status_type invalidatedSectorsBitmap = 0;

        if((lowLPA <= lsa.lpa && lsa.lpa <= highLPA) && !victim){
            auto infoItr = info.find(lsa.lpa);
            if(infoItr != info.end()){
                std::list<LSMSectorMapInfo*>& infoList = infoItr->second;
                for(auto curInfo = infoList.begin(); curInfo != infoList.end();){
                    LSA_type& targetLSA = (*curInfo)->lsa;
                    invalidatedSectorsBitmap |= (targetLSA.sectorsBitmap & lsa.sectorsBitmap);
                    targetLSA.sectorsBitmap &= ~(lsa.sectorsBitmap);
                    if(targetLSA.sectorsBitmap == 0){
                        (*curInfo)->infoInCurPage->erase(*curInfo);
                        delete (*curInfo);
                        curInfo = infoList.erase(curInfo);
                    } else{
                        curInfo++;
                    }

                }
                if(infoItr->second.size() != 0 && (lsa.lpa == lowLPA || lsa.lpa == highLPA)){
                    reArrange();
                }
            }
        }

        invalidSectorsCount += count_sector_no_from_status_bitmap(invalidatedSectorsBitmap);
        return invalidatedSectorsBitmap;
    }

    void LSMSectorMap::InsertEntry(std::list<LSMSectorMapEntry *> entryList)
    {
        for(auto curEntry : entryList){
            curEntry->reArrange();
            for(auto curInfo : curEntry->info){
                mappingLPA[curInfo.first].insert(curEntry);
            }
        }
        entriesCount += entryList.size();
    }

    bool LSMSectorMap::EraseEntry(LSMSectorMapEntry *entry)
    {
        bool entryWasErased = false;
        for(auto curInfo : entry->info){
            auto targetEntryList = mappingLPA.find(curInfo.first);
            if(targetEntryList != mappingLPA.end()){
                auto targetEntry = targetEntryList->second.find(entry);
                if(targetEntry != targetEntryList->second.end()){
                    targetEntryList->second.erase(entry);

                    if(targetEntryList->second.size() == 0){
                        mappingLPA.erase(targetEntryList);
                    }
                    entryWasErased = true;
                }
            }
        }

        if(entryWasErased){
            entriesCount--;
            delete entry;
        }

        return entryWasErased;
    }

    void LSMSectorMap::MoveEntryToUpperLevel(LSMSectorMapEntry *entry)
    {
        for(auto curInfo : entry->info){
            auto targetEntryList = mappingLPA.find(curInfo.first);
            if(targetEntryList != mappingLPA.end()){
                auto targetEntry = targetEntryList->second.find(entry);
                if(targetEntry != targetEntryList->second.end()){
                    targetEntryList->second.erase(entry);

                    if(targetEntryList->second.size() == 0){
                        mappingLPA.erase(targetEntryList);
                    }
                }
            }
        }
        entriesCount--;
        upperLevelSectorMap->InsertEntry(std::list<LSMSectorMapEntry*>{entry});
    }

    bool LSMSectorMap::isNoFreeCapacity()
    {
        return maxBlockCount <= entriesCount;
    }

    bool LSMSectorMap::isCompactioning()
    {
        return compaction;
    }

    void LSMSectorMap::startCompaction()
    {
        if(compaction){
            PRINT_ERROR("COMPACTION IS ALREADY PROCESSING : " << level);
        }

        compaction = true;
    }

    void LSMSectorMap::endCompaction()
    {
        if(!compaction){
            PRINT_ERROR("COMPACTION IS NOT PROCESSING : " << level)
        }

        compaction = false;
    }

    std::list<LSMSectorMapEntry*>* LSMSectorMap::getVictimGroup()
    {
        std::list<LSMSectorMapEntry*>* selectedGroup = NULL;
        std::list<LSMSectorMapEntry*>* curGroup = new std::list<LSMSectorMapEntry*>();
        
        int32_t selectedInvalidSectorsCount = -1;
        int32_t curInvalidSectorsCount = 0;
        for(auto entryList = mappingLPA.begin(); entryList != mappingLPA.end(); entryList++){
            for(auto curLevelEntry = entryList->second.begin(); curLevelEntry != entryList->second.end(); curLevelEntry++){
                if((*curLevelEntry)->victim){
                    continue;
                }
                curGroup->push_back(*curLevelEntry);
                curInvalidSectorsCount += (*curLevelEntry)->getInvalidSectorsCount();

                if(upperLevelSectorMap != NULL){
                    for(auto upperEntryList = upperLevelSectorMap->mappingLPA.begin(); upperEntryList != upperLevelSectorMap->mappingLPA.end(); upperEntryList++){
                        for(auto upperLevelEntry = upperEntryList->second.begin(); upperLevelEntry != upperEntryList->second.end(); upperLevelEntry++){
                            if((*upperLevelEntry)->isOverlapped(*curLevelEntry) && !(*upperLevelEntry)->victim){
                                curGroup->push_back(*upperLevelEntry);
                                curInvalidSectorsCount += (*upperLevelEntry)->getInvalidSectorsCount();
                            }
                        }
                    }
                }

                if(selectedInvalidSectorsCount < curInvalidSectorsCount){
                    if(selectedGroup != NULL){
                        delete selectedGroup;
                    }
                    selectedGroup = curGroup;
                    curGroup = new std::list<LSMSectorMapEntry*>();
                    selectedInvalidSectorsCount = curInvalidSectorsCount;
                } else{
                    curGroup->clear();
                }
                curInvalidSectorsCount = 0;
            }
        }

        if(selectedGroup != curGroup){
            delete curGroup;
        }
        return selectedGroup;
    }


    NVM::FlashMemory::Physical_Page_Address LSMSectorMapEntry::getNextPage()
    {
        if(insertingPage){
            PRINT_ERROR("ERROR IN GET NEXT PAGE")
        }
        insertingPage = true;

        NVM::FlashMemory::Physical_Page_Address ppaToRet = (*lastWrittenPage);
        lastWrittenPage->PageID++;
        return ppaToRet;
    }

    std::unordered_map<PPA_type, uint32_t> LSMSectorMapEntry::getValidSectorsCountInPage()
    {
        std::unordered_map<PPA_type, uint32_t> retValue;
        for(auto& infoListItr : info){
            for(auto & infoItr : infoListItr.second){
                retValue[infoItr->ppa] += count_sector_no_from_status_bitmap(infoItr->lsa.sectorsBitmap);
            }
        }
        return retValue;
    }

    std::list<LSA_type> LSMSectorMapEntry::getValidLSAList()
    {
        std::list<LSA_type> LSAToReturn;
        for(auto& infoListItr : info){
            for(auto & infoItr : infoListItr.second){
                LSAToReturn.push_back(infoItr->lsa);
            }
        }
        return LSAToReturn;
    }

    uint32_t LSMSectorMapEntry::getInvalidSectorsCount()
    {
        return invalidSectorsCount;
    }

    NVM::FlashMemory::Physical_Page_Address& LSMSectorMapEntry::getPBA()
    {
        return (*lastWrittenPage);
    }

    bool LSMSectorMapEntry::isOverlapped(const LSMSectorMapEntry* in_entry)
    {
        return (in_entry->highLPA < this->lowLPA || this->highLPA < in_entry->lowLPA);
    }

    void LSMSectorMapEntry::setAsVictim()
    {
        victim = true;
    }

    void LSMSectorMapEntry::reArrange()
    {
        lowLPA = UINT64_MAX;
        highLPA = 0;
        for(auto& curInfo : info){
            if(curInfo.first < lowLPA){
                lowLPA = curInfo.first;
            }
            if(curInfo.first > highLPA){
                highLPA = curInfo.first;
            }
        }
    }
    LSMSectorMapReadingInfo::LSMSectorMapReadingInfo(PPA_type ppa, std::unordered_map<LPA_type, page_status_type>& dataInPage)
    {
        this->ppa = ppa;
        this->dataInPage = dataInPage;
    }
    
    LSMSectorMapInfo::LSMSectorMapInfo(const LSA_type &lsa, const PPA_type &ppa)
        : lsa(lsa), ppa(ppa){}
    LSMSectorMapInfo::~LSMSectorMapInfo()
    {
    }
}