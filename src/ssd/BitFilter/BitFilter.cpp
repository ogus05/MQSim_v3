#include "BitFilter.h"
#include "SSD_Defs.h"

void SectorCluster::addSectors(LPA_type lpa, page_status_type sectors)
{
    clusteredSectors.push_back(new SectorGroup(lpa, sectors));
    sizeInSectors += count_sector_no_from_status_bitmap(sectors);
}

uint32_t SectorCluster::getSizeInSectors()
{
    return sizeInSectors;
}

void BitFilter::execute()
{
    std::vector<SectorCluster*> clusterList = makeCluster();
}

std::vector<SectorCluster*>& BitFilter::makeCluster()
{
    std::vector<SectorCluster*> clusterList;
    SectorCluster* sectorCluster = new SectorCluster();
    for(uint32_t filterIdx = 0; filterIdx < filter->size(); filterIdx++){
        uint64_t fineGrainedFilter = filter->at(filterIdx);
        if(fineGrainedFilter != 0){

            uint32_t sizeToAdd = sectorsPerPage - sectorCluster->getSizeInSectors();
            uint32_t flag = 0;
            
            while(flag < 64 && sizeToAdd >= 0){
                if((fineGrainedFilter & (1 << flag)) > 0){
                    sizeToAdd--;
                }
                flag++;
            }

            if(sizeToAdd == 0){
                clusterList.push_back(sectorCluster);
                sectorCluster = new SectorCluster();
            }
        }

    }

    return clusterList;
}

void BitFilter::readFilter()
{
}

void BitFilter::checkClusteringIsRequired(sim_time_type currentTime){
    if(T_lastRead - currentTime > T_executeThreshold){
        //execute();
        
    }
}

BitFilter::BitFilter(sim_time_type T_executeThreshold, uint64_t numberOfLogicalSector, uint32_t sectorsPerPage)
{
    filter = new std::vector<uint64_t>(numberOfLogicalSector / 64, 0);
    this->sectorsPerPage = sectorsPerPage;

    this->T_executeThreshold = T_executeThreshold;
    this->T_lastRead = 0;

}

BitFilter::~BitFilter()
{
    delete filter;
}

void BitFilter::reset()
{
    for(auto& findGrainedFilter : (*filter)){
        findGrainedFilter = 0;
    }
}

void BitFilter::check(LPA_type lpa, page_status_type sector)
{
    if(sector >= sectorsPerPage){
        PRINT_ERROR("ERROR IN BIT FILTER CHECK")
    }
    
    uint64_t sectorLocation = lpa * sectorsPerPage + sector;
    filter->at(sectorLocation / 64) |= (sectorLocation % 64);
}
