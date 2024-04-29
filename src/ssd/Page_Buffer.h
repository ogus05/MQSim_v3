#include "Sector_Log.h"

namespace SSD_Components{
    class Sector_Log;
    class Sector_Log_WF_Entry;
    
    class Page_Buffer_Entry{
    private:    
    public:
        page_status_type sectorsBitmap;

    Page_Buffer_Entry(const page_status_type& in_sectorsBitmap)
        :sectorsBitmap(in_sectorsBitmap) {};
    };

    class Page_Buffer{
    private:
        std::unordered_map<LPA_type, Page_Buffer_Entry*> entry;

        //Indicates free size of the Page Buffer.
        uint32_t remainBufferSizeInSectors;

        Sector_Log* sectorLog;
    public:
        Page_Buffer(const uint32_t page_size_in_sectors, Sector_Log* sector_log)
            :remainBufferSizeInSectors(page_size_in_sectors), sectorLog(sector_log) {}; 
        ~Page_Buffer();
        uint64_t Exists(const LPA_type& lpa);
        void Insert(const LPA_type& lpa, const page_status_type& sectorsBitmap, page_status_type& remainSectorsBitmap);
        void Remove(const LPA_type& lpa, const page_status_type& sectorsBitmap);
        void RemoveAll();

        const std::unordered_map<LPA_type, Page_Buffer_Entry*> GetAll();
    };
}