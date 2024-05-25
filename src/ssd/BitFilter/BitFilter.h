#include "../nvm_chip/flash_memory/FlashTypes.h"
#include <list>
#include <map>
#include "Engine.h"
#include "Sector_Log.h"

namespace SSD_Components{


    class SectorCluster
    {
    private:
        std::list<key_type> clusteredSectors;
    public:
        SectorCluster();
        void addSubPage(key_type key);
    };

    class BitFilter
    {
    private:
        std::map<uint64_t, uint64_t> filter;
        uint64_t filterSize;
        uint32_t subPagesPerPage;

        SectorLog* sectorLog;

        sim_time_type T_lastRead;
        sim_time_type T_executeThreshold;

        std::list<SectorCluster*> makeClusterList();
        
    public:
        BitFilter(sim_time_type T_executeThreshold, uint64_t numberOfLogicalSector, uint32_t subPagesPerPage);
        ~BitFilter();
        
        void addBit(const LPA_type& lpa, const page_status_type& subPageBitmap);
        void removeBit(const LPA_type& lpa, const page_status_type& subPageBitmap);

        void reset();
        void polling();


        void startClustering();
    };
}

