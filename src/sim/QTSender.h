#ifndef QT_SENDER_H
#define QT_SENDER_H

#include <netinet/in.h>
#include <functional>
#include <list>
#include <map>
#include <vector>
#include <utility>
#include <string>
#include <cstdint>
#include "QTComp.h"

#define FULL_COMP_COUNT 256


#define DONT_SEND 0
#define SEND_ALLOWED 1
#define SEND_BLOCKED 2

#define TREE_ROOT_NAME "root"
#define TREE_ROOT_NUM -1

enum QTPacketType{
    CON = 0,
    COMPINFO = 1,
    DISCON = 2,
};

namespace MQSimEngine
{
    typedef uint32_t value_type;

    struct QTPacketMTDT{
        uint8_t type;
        uint8_t rsvd[3];
        uint32_t length;
    };

    struct QTCompInfo{
        uint32_t id  = 0;
        std::function<value_type()> func;
        std::string name;

        QTCompInfo(std::function<value_type()> in_func, std::string in_name)
            : func(in_func), name(in_name){};
    };

    struct QTCompTree{
        std::string groupName;
        std::map<std::string, QTCompTree*> childList;

        std::map<std::string, QTCompInfo*> QTCompList;

        QTCompTree(std::string in_groupName)
            : groupName(in_groupName) {};
    };


    class QTSender
    {
        friend class Engine;
    public:
        void AddQTComp(QTComp* comp);
        int AddGroup(const std::string& groupName, const int& upperValue = TREE_ROOT_NUM);
        void AddFunc(const std::function<size_t()>& func, const std::string& name, const int& upperValue);
    private:
        int sockfd;
        sockaddr_in servaddr;
        struct timeval timeout;
        std::map<int, QTCompTree*> groupList;

        uint64_t totalReqs;
        uint64_t QTSendCount;

        uint32_t maxID;

        std::list<QTComp*> componentList;


        QTSender();
        ~QTSender();

        int Start();
        void SendCompInfoPacket(uint64_t curReqCount = UINT64_MAX);

        void CreateGroupList();
        void AppendValueList(const char* startPtr);
        std::string SerializeGroupList();

        void SetMilestone(const uint64_t& totalReqs, const uint64_t QTSendCount);

        void SendConnPacket();
        void SendDisconnPacket();


    };

}

#endif //QT_SENDER_H