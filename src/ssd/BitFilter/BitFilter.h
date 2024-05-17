#include "../nvm_chip/flash_memory/FlashTypes.h"
#include <list>
#include <vector>

class SectorGroup
{
    friend class SectorCluster;
private:
    LPA_type lpa;
    page_status_type sectors;
    SectorGroup(LPA_type lpa, page_status_type sectors);
public:

};

class SectorCluster
{
private:
    std::list<SectorGroup*> clusteredSectors;
    uint32_t sizeInSectors;
public:
    void addSectors(LPA_type lpa, page_status_type sectors);
    uint32_t getSizeInSectors();
};

class BitFilter
{
private:
    std::vector<uint64_t>* filter;
    uint32_t sectorsPerPage;

    sim_time_type T_lastRead;
    sim_time_type T_executeThreshold;
    
    void execute();
    std::vector<SectorCluster*>& makeCluster();
    void readFilter();
public:
    BitFilter(sim_time_type T_executeThreshold, uint64_t numberOfLogicalSector, uint32_t sectorsPerPage);
    ~BitFilter();

    void checkClusteringIsRequired(sim_time_type currentTime);
    void reset();
    void check(LPA_type lpa, page_status_type sector);
};

