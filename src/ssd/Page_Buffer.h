#ifndef PAGE_BUFFER_H
#define PAGE_BUFFER_H

#include "Sector_Log.h"

namespace SSD_Components{
    class SectorLog;
    
    class PageBufferEntry{
    private:    
    public:
        std::list<PageBufferEntry*>::iterator list_itr;
        key_type key;
        bool dirty;

    PageBufferEntry(const key_type& in_key, const bool in_dirty)
        :key(in_key), dirty(in_dirty) {};
    };

    class PageBuffer{
    private:
        std::list<PageBufferEntry*> entryList;

        std::unordered_map<key_type, PageBufferEntry*> keyMappingEntry;

        //Indicates free size of the Page Buffer.
        const uint32_t maxBufferSize;

        SectorLog* sectorLog;
    public:
        PageBuffer(const uint32_t maxBufferSizeInSubPages, SectorLog* in_sectorLog); 
        ~PageBuffer();
        void setClean(const key_type key);
        bool isDirty(const key_type key);

        bool Exists(const key_type key, bool used);
        void insertData(const key_type& key, bool dirty);

        void RemoveByWrite(const key_type key);
        void RemoveLastEntry();

        bool hasFreeSpace();
        bool isLastEntryDirty();

        void flush(uint32_t sectorsPerPage);
    };
}

#endif  //  SECTOR_MAP__H