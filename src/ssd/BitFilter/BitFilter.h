#ifndef BIT_FILTER_H
#define BIT_FILTER_H

#include "../nvm_chip/flash_memory/FlashTypes.h"
#include <list>
#include <map>
#include "Engine.h"
#include "Sector_Log.h"

namespace SSD_Components{


    class SubPageCluster
    {
    public:
        std::list<key_type> clusteredSectors;
        SubPageCluster();
    };

    class BitFilter
    {
    private:
        static BitFilter* instance;

        std::set<key_type> filter;
        SectorLog* sectorLog;

        sim_time_type T_lastRead;
        sim_time_type T_executeThreshold;

        bool isClustering;

        uint32_t remainReadForClustering;
        uint32_t remainWriteForClustering;
        std::list<std::list<NVM_Transaction *>*> pendingTrList;

        std::list<SubPageCluster*> makeClusterList();
        
        void startClustering();

        static void reset();
        static void polling();
    public:
        BitFilter(sim_time_type T_executeThreshold, SectorLog* sectorLog);
        ~BitFilter();

        void addBit(const key_type key);
        void removeBit(const key_type key);
        void endClustering();

        void setRemainRead(uint32_t remainReadForClustering);
        void handleClusteringReadIsArrived();
        void handleClusteringWriteIsArrived();

        bool isClusteringProcessing();
        void addPendingTrListUntilClustering(std::list<NVM_Transaction *>& transaction_list);
    };
}

#endif