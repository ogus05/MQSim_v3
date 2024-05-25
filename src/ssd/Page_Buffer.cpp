#include "Page_Buffer.h"

namespace SSD_Components{
// return the LPA's sectors that stored in the Page Buffer as a bitmap.
    bool PageBuffer::Exists(const key_type key, bool used)
    {
        auto entry = keyMappingEntry.find(key);
        
        if(entry != keyMappingEntry.end()){
            if(used){
                if(entry->second->flushingID == 0 && (*entryList.begin())->key != key){
                    entryList.splice(entryList.begin(), entryList, entry->second->list_itr);
                }
            }
            return true;
        }
        return false;
    }

    void PageBuffer::insertData(const key_type& key, bool dirty)
    {
        auto entry = keyMappingEntry.find(key);
        if(entry == keyMappingEntry.end()){
            PageBufferEntry* newEntry = new PageBufferEntry(key, dirty);
            entryList.push_front(newEntry);
            newEntry->list_itr = entryList.begin();

            keyMappingEntry.insert({key, newEntry});
        } else{
            entry->second->dirty |= dirty;
            if(entry->second->flushingID == 0 && (*entryList.begin())->key != key){
                entryList.splice(entryList.begin(), entryList, entry->second->list_itr);
            }
        }
    }

    void PageBuffer::RemoveByFlush(const uint32_t flushingID)
    {
        for(auto flushingEntry : flushingEntryList.at(flushingID)){
            keyMappingEntry.erase(flushingEntry->key);
            delete flushingEntry;
        }

        flushingEntryList.erase(flushingID);
    }

    void PageBuffer::RemoveByWrite(const key_type key)
    {
        auto mappingEntryItr = keyMappingEntry.find(key);
        if(mappingEntryItr != keyMappingEntry.end()){
            auto entry = mappingEntryItr->second;

            if(entry->flushingID == 0){
                entryList.erase(entry->list_itr);
            } else{
                flushingEntryList.at(entry->flushingID).erase(entry->list_itr);
            }
            delete entry;
            keyMappingEntry.erase(mappingEntryItr);
        }
    }

    void PageBuffer::RemoveLastEntry()
    {
        PageBufferEntry* lastEntry = entryList.back();
        if(lastEntry->dirty) PRINT_ERROR("LAST ENTRY HAS DIRTY DATA");

        keyMappingEntry.erase(lastEntry->key);
        entryList.erase(lastEntry->list_itr);
        delete lastEntry;
    }

    PageBuffer::PageBuffer(const uint32_t maxBufferSizeInSubPages, SectorLog *in_sectorLog) :
        maxBufferSize(maxBufferSizeInSubPages), sectorLog(in_sectorLog) {}

    PageBuffer::~PageBuffer()
    {
        for(auto& entry : entryList){
            delete entry;
        }
    }

    bool PageBuffer::hasFreeSpace()
    {
        return entryList.size() < maxBufferSize;
    }

    bool PageBuffer::isLastEntryDirty()
    {
        return entryList.back()->dirty;
    }

    void PageBuffer::flush(uint32_t subPagesPerPage)
    {
        uint32_t remainSubPages = subPagesPerPage;
        uint32_t flushingID = sectorLog->getNextID();
        std::list<PageBufferEntry*>& entriesToFlush = flushingEntryList.insert({flushingID, std::list<PageBufferEntry*>()}).first->second;
        std::list<key_type> subPagesToFlush;
        auto curEntry = entryList.rbegin();
        int i = 0;
        while(!(curEntry == entryList.rend() || remainSubPages == 0)){
            if((*curEntry)->dirty){
                (*curEntry)->flushingID = flushingID;
                entriesToFlush.push_front(*curEntry);
                (*curEntry)->list_itr = entriesToFlush.begin();

                subPagesToFlush.push_back((*curEntry)->key);
                remainSubPages--;

                curEntry = std::list<PageBufferEntry*>::reverse_iterator(entryList.erase(--curEntry.base()));
            } else{
                curEntry++;
            }
        }

        sectorLog->sendSubPageWriteForFlush(subPagesToFlush, flushingID);
    }
}
    
