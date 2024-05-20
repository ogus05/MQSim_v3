#ifndef PAGE_BUFFER_H
#define PAGE_BUFFER_H

#include "Sector_Log.h"

namespace SSD_Components{
    class Sector_Log;
    class Sector_Log_WF_Entry;
    
    class Page_Buffer_Entry{
    private:    
    public:
        std::list<Page_Buffer_Entry*>::iterator list_itr;
        LPA_type lpa;
        page_status_type sectorsBitmap;

        uint32_t flushingID;

    Page_Buffer_Entry(const LPA_type& in_lpa, const page_status_type& in_sectorsBitmap)
        :lpa(in_lpa), sectorsBitmap(in_sectorsBitmap), flushingID(0) {};
    };

    class Page_Buffer{
    private:
        std::list<Page_Buffer_Entry*> entryList;
        std::unordered_map<uint32_t, std::list<Page_Buffer_Entry*>> flushingEntryList;

        std::unordered_map<LPA_type, std::list<Page_Buffer_Entry*>> lpaMappingEntry;

        //Indicates free size of the Page Buffer.
        uint32_t curBufferSize;

        Sector_Log* sectorLog;
    public:
        Page_Buffer(const uint32_t page_size_in_sectors, Sector_Log* sector_log)
            :curBufferSize(0), sectorLog(sector_log) {}; 
        ~Page_Buffer();
        page_status_type Exists(const LPA_type& lpa);
        void Insert(const LPA_type& lpa, const page_status_type& sectorsBitmap);

        std::list<std::pair<LPA_type, page_status_type>> getFlushEntries(const uint32_t flushingID);
        void RemoveByFlush(const uint32_t flushingID);
        void RemoveByPageWrite(const LPA_type& lpa, const page_status_type& in_sectorsBitmap);

        uint32_t getCurrentSize();

        void flush(uint32_t sectorsPerPage);
    };
}

#endif  //  SECTOR_MAP__H