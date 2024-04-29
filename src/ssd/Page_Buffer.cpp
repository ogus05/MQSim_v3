#include "Page_Buffer.h"

namespace SSD_Components{
// return the LPA's sectors that stored in the Page Buffer as a bitmap.
    uint64_t Page_Buffer::Exists(const LPA_type& lpa)
    {
        auto itr = entry.find(lpa);
        if (itr == entry.end())
            return 0;
        else
        {
            return itr->second->sectorsBitmap;
        }
    }

    void Page_Buffer::Insert(const LPA_type& lpa, const page_status_type& sectorsBitmap, page_status_type& remainSectorsBitmap)
    {
        page_status_type sectorsToInsert = 0;
        page_status_type sectorsToRemain = sectorsBitmap;

        if(remainBufferSizeInSectors == 0){
            remainSectorsBitmap = sectorsToRemain;
            return;
        } else if(remainBufferSizeInSectors < 0){
            PRINT_ERROR("PAGE BUFFER INSERT - REMAIN BUFFER SIZE IS LOWER THAN 0")
        }
        
        if(entry.find(lpa) != entry.end()){
            sectorsToRemain = (sectorsToRemain & ~(entry.at(lpa)->sectorsBitmap));
        }

        if(remainBufferSizeInSectors >= count_sector_no_from_status_bitmap(sectorsToRemain)){
            sectorsToInsert = sectorsToRemain;
            remainBufferSizeInSectors -= count_sector_no_from_status_bitmap(sectorsToInsert);
            sectorsToRemain = 0;
        } else{
            page_status_type flag = 1;

            while (count_sector_no_from_status_bitmap(sectorsToInsert) != remainBufferSizeInSectors)
            {
                while (sectorsToInsert == (flag & sectorsToRemain))
                {
                    flag = (uint64_t)1 | (flag << (uint64_t)1);
                }
                sectorsToInsert = (sectorsToRemain & flag);
            }

            remainBufferSizeInSectors -= count_sector_no_from_status_bitmap(sectorsToInsert);
            if(remainBufferSizeInSectors != 0){
                PRINT_ERROR("PAGE BUFFER INSERT - REMAIN BUFFER SIZE IS NOT 0")
            }
            sectorsToRemain = (sectorsToRemain & ~(sectorsToInsert));
        }

        if(sectorsToInsert != 0){
            if(entry.find(lpa) != entry.end()){
                entry.at(lpa)->sectorsBitmap |= sectorsToInsert;
            } else{
                entry.insert({lpa, new Page_Buffer_Entry(sectorsToInsert)});
            }
        }
        remainSectorsBitmap = sectorsToRemain;
    }

    void Page_Buffer::Remove(const LPA_type &lpa, const page_status_type &sectorsToRemoveBitmap)
    {
        auto entryToRemove = entry.find(lpa);
        if(entryToRemove == entry.end()){
            return;
        } else{
            remainBufferSizeInSectors += count_sector_no_from_status_bitmap(entryToRemove->second->sectorsBitmap & sectorsToRemoveBitmap);
            entryToRemove->second->sectorsBitmap = (entryToRemove->second->sectorsBitmap & ~(sectorsToRemoveBitmap));
            if(entryToRemove->second->sectorsBitmap == 0){
                delete entryToRemove->second;
                entry.erase(entryToRemove);
            }
        }
    }

    Page_Buffer::~Page_Buffer()
    {
        for(auto& e : entry){
            delete e.second;
        }
    }

    void Page_Buffer::RemoveAll()
    {
        for(auto& target : entry){
            remainBufferSizeInSectors += count_sector_no_from_status_bitmap(target.second->sectorsBitmap);
            delete target.second;
        }
        if(remainBufferSizeInSectors != sectorLog->sectorsPerPage){
            PRINT_ERROR("PAGE BUFFER REMOVE ALL - REMAIN BUFFER SIZE IS NOT 0")
        }
        entry.clear();
    }

    const std::unordered_map<LPA_type, Page_Buffer_Entry*> Page_Buffer::GetAll()
    {
        return entry;
    }
}
    
