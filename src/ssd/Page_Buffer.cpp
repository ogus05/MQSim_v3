#include "Page_Buffer.h"

namespace SSD_Components{
// return the LPA's sectors that stored in the Page Buffer as a bitmap.
    page_status_type Page_Buffer::Exists(const LPA_type& lpa)
    {
        page_status_type existsSectorsBitmap = 0;

        auto mappingEntryList = lpaMappingEntry.find(lpa);

        if(mappingEntryList != lpaMappingEntry.end()){
            for(auto entry : mappingEntryList->second){
                existsSectorsBitmap |= entry->sectorsBitmap;
            }
        }

        return existsSectorsBitmap;
    }

    void Page_Buffer::Insert(const LPA_type& lpa, const page_status_type& sectorsBitmap)
    {
        Page_Buffer_Entry* newEntry = new Page_Buffer_Entry(lpa, sectorsBitmap);
        entryList.push_front(newEntry);
        newEntry->list_itr = entryList.begin();

        auto mappingEntry = lpaMappingEntry.find(lpa);
        if(mappingEntry == lpaMappingEntry.end()){
            mappingEntry = lpaMappingEntry.insert({lpa, std::list<Page_Buffer_Entry*>()}).first;
        }
        mappingEntry->second.push_back(newEntry);
        curBufferSize += count_sector_no_from_status_bitmap(newEntry->sectorsBitmap);
    }

    std::list<std::pair<LPA_type, page_status_type>> Page_Buffer::getFlushEntries(const uint32_t flushingID)
    {
        auto entryList = flushingEntryList.at(flushingID);
        std::list<std::pair<LPA_type, page_status_type>> listToRet;

        for(auto entry : entryList){
            listToRet.push_back({entry->lpa, entry->sectorsBitmap});
        }

        return listToRet;
    }

    void Page_Buffer::RemoveByFlush(const uint32_t flushingID)
    {
        for(auto flushingEntry : flushingEntryList.at(flushingID)){
            std::list<Page_Buffer_Entry*>& mappingEntryList = lpaMappingEntry.at(flushingEntry->lpa);
            auto mappingEntry = mappingEntryList.begin();

            while((*mappingEntry)->flushingID != flushingID){
                mappingEntry++;
            }
            mappingEntryList.erase(mappingEntry);
            if(mappingEntryList.size() == 0){
                lpaMappingEntry.erase(flushingEntry->lpa);
            }
        }

        for(auto flushingEntry : flushingEntryList.at(flushingID)){
            delete flushingEntry;
        }

        flushingEntryList.erase(flushingID);
    }

    void Page_Buffer::RemoveByPageWrite(const LPA_type &lpa, const page_status_type& in_sectorsBitmap)
    {
        auto mappingEntry = lpaMappingEntry.find(lpa);

        if(mappingEntry != lpaMappingEntry.end()){
            auto entry = mappingEntry->second.begin();
            
            if((*entry)->flushingID == 0){
                curBufferSize -= count_sector_no_from_status_bitmap((*entry)->sectorsBitmap & in_sectorsBitmap);
            }
            (*entry)->sectorsBitmap &= ~(in_sectorsBitmap);

            if((*entry)->sectorsBitmap == 0){
                if((*entry)->flushingID != 0){
                    flushingEntryList.at((*entry)->flushingID).erase((*entry)->list_itr);
                } else{
                    entryList.erase((*entry)->list_itr);
                }
                delete (*entry);
                mappingEntry->second.erase(entry);
            }

            if(mappingEntry->second.size() == 0){
                lpaMappingEntry.erase(mappingEntry);
            }
        }
    }

    Page_Buffer::~Page_Buffer()
    {
        for(auto& entryList : lpaMappingEntry){
            for(auto& entry : entryList.second){
                delete entry;
            }
        }
    }

    uint32_t Page_Buffer::getCurrentSize()
    {
        return curBufferSize;
    }

    void Page_Buffer::flush(uint32_t sectorsPerPage)
    {
        uint32_t remainSectors = sectorsPerPage;
        std::list<std::pair<Page_Buffer_Entry*, page_status_type>> entriesToFlush;
        uint32_t flushingID = sectorLog->getNextID();

        auto curEntry = entryList.rbegin();
        while(curEntry != entryList.rend() && remainSectors != 0){
            page_status_type flag = 1;
            page_status_type curSectorsToInsert = 0;
            
            while(remainSectors != 0 && (curSectorsToInsert != (*curEntry)->sectorsBitmap)){
                while (curSectorsToInsert == (flag & (*curEntry)->sectorsBitmap))
                {
                    flag = (page_status_type)1 | (flag << (page_status_type)1);
                }
                curSectorsToInsert = (flag & (*curEntry)->sectorsBitmap);
                remainSectors--;
            }

            entriesToFlush.push_back({(*curEntry), curSectorsToInsert});
            curEntry++;
        }

        if(remainSectors == 0){
            curBufferSize -= sectorsPerPage;
        } else{
            PRINT_ERROR("ERROR IN FLUSH")
        }

        std::list<std::pair<LPA_type, page_status_type>> sectorsToInsert;

        std::list<Page_Buffer_Entry*>& newFlushingEntryList = flushingEntryList.insert({flushingID, std::list<Page_Buffer_Entry*>()}).first->second;
        for(auto entry : entriesToFlush){
            if(entry.first->sectorsBitmap == entry.second){
                this->entryList.erase(entry.first->list_itr);
                newFlushingEntryList.push_front(entry.first);
                entry.first->list_itr = newFlushingEntryList.begin();
                entry.first->flushingID = flushingID;
                sectorsToInsert.push_back({entry.first->lpa, entry.second});
            } else{
                Page_Buffer_Entry* flushEntry = new Page_Buffer_Entry(entry.first->lpa, entry.second);
                newFlushingEntryList.push_front(flushEntry);
                flushEntry->list_itr = newFlushingEntryList.begin();
                flushEntry->flushingID = flushingID;
                
                lpaMappingEntry.at(entry.first->lpa).push_back(flushEntry);
                entry.first->sectorsBitmap &= ~(entry.second);
                sectorsToInsert.push_back({entry.first->lpa, entry.second});
            }
        }

        sectorLog->sendSubPageWriteForFlush(sectorsToInsert, flushingID);
    }
}
    
