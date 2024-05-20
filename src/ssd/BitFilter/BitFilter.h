#include "../nvm_chip/flash_memory/FlashTypes.h"
#include <list>
#include <map>
#include "Engine.h"
#include "Sector_Log.h"

namespace SSD_Components{
    class SectorCluster
    {
    private:
        std::list<std::pair<LPA_type, page_status_type>> clusteredSectors;
    public:
        SectorCluster();
        void addSectors(LPA_type lpa, page_status_type sectors);
    };

    class BitFilter
    {
    private:
        std::map<LPA_type, page_status_type> filter;
        uint64_t filterSize;
        uint32_t sectorsPerPage;

        Sector_Log* sectorLog;

        sim_time_type T_lastRead;
        sim_time_type T_executeThreshold;
        
    public:
        BitFilter(sim_time_type T_executeThreshold, uint64_t numberOfLogicalSector, uint32_t sectorsPerPage);
        ~BitFilter();
        
        void addBit(const LPA_type& lpa, const page_status_type& sectors);
        void removeBit(const LPA_type& lpa, const page_status_type& sectors);

        void reset();
        void polling();

        std::list<SectorCluster*> makeCluster();

        void startClustering();
    };
}

