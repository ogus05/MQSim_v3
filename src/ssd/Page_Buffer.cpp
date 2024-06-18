#include "Page_Buffer.h"
#include "Sector_Map.h"

namespace SSD_Components{
// return the LPA's sectors that stored in the Page Buffer as a bitmap.
    bool PageBuffer::Exists(const key_type key, bool used)
    {
        auto entry = keyMappingEntry.find(key);
        
        if(entry != keyMappingEntry.end()){
            if(used){
                if((*entryList.begin())->key != key){
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
            if (entryList.begin() != entry->second->list_itr) {
                entryList.splice(entryList.begin(), entryList, entry->second->list_itr);
            }
            entry->second->dirty |= dirty;
        }
    }

    void PageBuffer::RemoveByWrite(const key_type key)
    {
        auto mappingEntryItr = keyMappingEntry.find(key);
        if(mappingEntryItr != keyMappingEntry.end()){
            auto entry = mappingEntryItr->second;
            entryList.erase(entry->list_itr);
            delete entry;
            keyMappingEntry.erase(mappingEntryItr);
        } else{
            PRINT_ERROR("PAGE BUFFER REMOVE BY WRITE - THERE ARE NO KEY : " << key)
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

    void PageBuffer::setClean(const key_type key)
    {
        auto mappingEntry = keyMappingEntry.find(key);
        if(mappingEntry != keyMappingEntry.end()){
            mappingEntry->second->dirty = 0;
        } else{
            PRINT_ERROR("ERROR IN PAGE BUFFER SET CLEAN")
        }
        
    }

    bool PageBuffer::isDirty(const key_type key)
    {
        auto mappingEntry = keyMappingEntry.find(key);
        if(mappingEntry != keyMappingEntry.end()){
            return mappingEntry->second->dirty;
        } else{
            PRINT_ERROR("ERROR IN PAGE BUFFER IS DIRTY")
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

    std::list<key_type> PageBuffer::getLastEntries(uint32_t subPagesPerPage)
    {
        uint32_t remainSubPages = subPagesPerPage;
        std::list<key_type> subPagesToFlush;
        auto curEntry = entryList.rbegin();
        while(!(curEntry == entryList.rend() || remainSubPages == 0)){
            if((*curEntry)->dirty){
                subPagesToFlush.push_back((*curEntry)->key);
                remainSubPages--;
            }
            curEntry++;
        }

        for(key_type key : subPagesToFlush){
            auto curEntry = keyMappingEntry.find(key);
            if(curEntry == keyMappingEntry.end()) PRINT_ERROR("ERROR IN FLUSH")

            entryList.erase(curEntry->second->list_itr);
            keyMappingEntry.erase(curEntry);
            delete curEntry->second;
        }
        return subPagesToFlush;
    }
}
