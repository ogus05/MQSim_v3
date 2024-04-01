#include "LSMBlockBuffer.h"
#include "Data_Cache_Flash.h"
#include "LSMSectorLog.h"
namespace SSD_Components{
    BlockBuffer::BlockBuffer(const uint32_t maxCapacityInSectors)
        : maxCapacityInSectors(maxCapacityInSectors){
        curCapacityInSectors = 0;
        flush = false;
    }

    BlockBuffer::~BlockBuffer()
    {
        for(auto e : entry){
            delete e.second;
        }
    }

    page_status_type BlockBuffer::LookUp(const LSA_type& lsa)
    {
        page_status_type findSectors = 0;
        for(auto entryItr = entry.lower_bound(lsa.lpa); entryItr != entry.upper_bound(lsa.lpa); entryItr++){
            findSectors |= entryItr->second->lsa.sectorsBitmap;
        }
        return findSectors;
    }

    void BlockBuffer::Remove(const LSA_type& lsa)
    {
        for(auto entryItr = entry.lower_bound(lsa.lpa); entryItr != entry.upper_bound(lsa.lpa); ){
            page_status_type sectorsToRemove = entryItr->second->lsa.sectorsBitmap & lsa.sectorsBitmap;
            if(sectorsToRemove > 0){
                curCapacityInSectors -= count_sector_no_from_status_bitmap(sectorsToRemove);
                entryItr->second->lsa.sectorsBitmap &= ~(sectorsToRemove);
                if(entryItr->second->lsa.sectorsBitmap == 0){
                    lru_list.erase(entryItr->second->lru_list_ptr);
                    delete entryItr->second;
                    entry.erase(entryItr++);
                    continue;
                }
            }
            entryItr++;
        }
    }

    void BlockBuffer::Insert(const LSA_type& lsa)
    {
        Remove(lsa);
        curCapacityInSectors += count_sector_no_from_status_bitmap(lsa.sectorsBitmap);
        BlockBufferEntry* newEntry = new BlockBufferEntry(lsa);
        lru_list.push_front({lsa.lpa, newEntry});
        newEntry->lru_list_ptr = lru_list.begin();
        newEntry->selfItr = entry.insert({lsa.lpa, newEntry});
    }

    WriteBlockBuffer::WriteBlockBuffer(const uint32_t &maxCapacityInSectors, const uint32_t &blockSizeInSectors)
        : BlockBuffer(maxCapacityInSectors), blockSizeInSectors(blockSizeInSectors) {}

    WriteBlockBuffer::~WriteBlockBuffer()
    {
    }

    std::list<BlockBufferEntry *> WriteBlockBuffer::evictEntriesInBlockSize()
    {
        std::list<BlockBufferEntry*> entriesToEvict;
        uint32_t blockBufferReadSizeInSectors = 0;
        while(entry.size() > 0){
            BlockBufferEntry* targetEntry = lru_list.back().second;
            uint32_t targetEntrySectorCount = count_sector_no_from_status_bitmap(targetEntry->lsa.sectorsBitmap);
            if(blockBufferReadSizeInSectors + targetEntrySectorCount > blockSizeInSectors){
                break;
            }
            blockBufferReadSizeInSectors += targetEntrySectorCount;
            lru_list.pop_back();
            entry.erase(targetEntry->selfItr);
            entriesToEvict.push_back(targetEntry);
        }
        curCapacityInSectors -= blockBufferReadSizeInSectors;
        return entriesToEvict;
    }

    bool BlockBuffer::isNoFreeCapacity()
    {
        return curCapacityInSectors >= maxCapacityInSectors;
    }

    void BlockBuffer::startFlushing()
    {
        flush = true;
    }

    void BlockBuffer::endFlushing()
    {
        flush = false;
    }

    bool BlockBuffer::isFlushing()
    {
        return flush;
    }

    BlockBufferEntry::BlockBufferEntry(const LSA_type& lsa)
    {
        this->lsa = LSA_type(lsa);
    }
    SharedVar::SharedVar(const stream_id_type streamID, const uint32_t sectorsPerPage, const uint32_t pagesPerBlock, Address_Mapping_Unit_Page_Level *amu, TSU_Base *tsu, Data_Cache_Manager_Base* dcm)
        :streamID(streamID), sectorsPerPage(sectorsPerPage), pagesPerBlock(pagesPerBlock), amu(amu), tsu(tsu), dcm(dcm) {}
    LSA_type::LSA_type(LPA_type lpa, page_status_type sectorsBitmap)
        : lpa(lpa), sectorsBitmap(sectorsBitmap) {}
    LSA_type::LSA_type(const LSA_type& lsa){
        this->lpa = lsa.lpa;
        this->sectorsBitmap = lsa.sectorsBitmap;
    }
    LSA_type::LSA_type()
        : lpa(NO_LPA), sectorsBitmap(0) {}

    ReadBlockBuffer::ReadBlockBuffer(const uint32_t &maxCapacityInSectors)
        : BlockBuffer(maxCapacityInSectors) {}

    ReadBlockBuffer::~ReadBlockBuffer()
    {
    }

    void ReadBlockBuffer::Insert(const LSA_type &lsa)
    {
        BlockBuffer::Insert(lsa);

        while(curCapacityInSectors > maxCapacityInSectors){
            Remove(lru_list.back().second->lsa);
        }
    }
}