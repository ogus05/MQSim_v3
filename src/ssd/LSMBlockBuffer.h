#ifndef NVM_BLOCKBUFFER_H
#define NVM_BLOCKBUFFER_H

#include <list>
#include <unordered_map>
#include <map>
#include <vector>
#include "FlashTypes.h"


namespace SSD_Components{

    class Address_Mapping_Unit_Page_Level;
    class TSU_Base;
    class Data_Cache_Manager_Base;
    class LSMSectorLog;

    class SharedVar{
    public:
        const uint32_t sectorsPerPage;
        const uint32_t pagesPerBlock;
        const stream_id_type streamID;
        Address_Mapping_Unit_Page_Level* amu;
        TSU_Base* tsu;
        Data_Cache_Manager_Base* dcm;

        page_status_type sectorsBitmap(uint32_t sectorSize){
            return ((uint64_t)1 << sectorSize) - (uint64_t)1;
        }

        SharedVar(const stream_id_type streamID, const uint32_t sectorsPerPage, const uint32_t pagesPerBlock, 
            Address_Mapping_Unit_Page_Level* amu, TSU_Base* tsu, Data_Cache_Manager_Base* dcm);
    };
    
    struct LSA_type{
        LPA_type lpa;
        page_status_type sectorsBitmap;
        LSA_type(LPA_type lpa, page_status_type sectorsBitmap);
        LSA_type(const LSA_type& lsa);
        LSA_type();

        bool operator < (const LSA_type& in_lsa) const{
            return lpa < in_lsa.lpa;
        }
    };

    class BlockBufferEntry
    {
    private:
    public:
        LSA_type lsa;
        std::list<std::pair<LPA_type, BlockBufferEntry*>>::iterator lru_list_ptr;
        std::multimap<LPA_type, BlockBufferEntry*>::iterator selfItr;
        BlockBufferEntry(const LSA_type& lsa);
    };
    

    class BlockBuffer
    {
    protected:
        uint32_t curCapacityInSectors;
        const uint32_t maxCapacityInSectors;
        std::multimap<LPA_type, BlockBufferEntry*> entry;
        std::list<std::pair<LPA_type, BlockBufferEntry*>> lru_list;
        bool flush;
    public:
        BlockBuffer(const uint32_t maxCapacityInSectors);
        ~BlockBuffer();
        
        void Insert(const LSA_type& lsa);
        page_status_type LookUp(const LSA_type& lsa);
        void Remove(const LSA_type& lsa);

        bool isNoFreeCapacity();

        void startFlushing();
        void endFlushing();
        bool isFlushing();

    };

    class ReadBlockBuffer : public BlockBuffer{
    private:

    public:
        ReadBlockBuffer(const uint32_t& maxCapacityInSectors);
        ~ReadBlockBuffer();
        void Insert(const LSA_type& lsa);
    };

    class WriteBlockBuffer : public BlockBuffer{
    private:
        const uint32_t blockSizeInSectors;
    public:
        WriteBlockBuffer(const uint32_t& maxCapacityInSectors, const uint32_t& blockSizeInSectors);
        ~WriteBlockBuffer();
        std::list<BlockBufferEntry*> evictEntriesInBlockSize();
    };
}

#endif