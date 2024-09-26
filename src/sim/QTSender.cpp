#include "QTSender.h"
#include "Sim_Defs.h"

#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <cerrno>
#include <fcntl.h>

namespace MQSimEngine{


    QTSender::QTSender()
    {
        QTCompTree* root = new QTCompTree(TREE_ROOT_NAME);
        groupList.insert({TREE_ROOT_NUM, root});
    }

    QTSender::~QTSender()
    {
        for(auto [name, ptr] : groupList){
            delete ptr;
        }
        close(sockfd);
    }

    int QTSender::Start()
    {
        std::string ipAddr;
        uint64_t portNumber;

        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            PRINT_MESSAGE("Socket creation failed");
            return SEND_BLOCKED;
        } else{
            timeout.tv_sec = 5;
            timeout.tv_usec = 0;
            setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

            servaddr.sin_family = AF_INET;
        }

        do{
            char isQTDisplay;
            PRINT_MESSAGE("Need the QT Display?? Y / N")
            std::cin >> isQTDisplay;
            if(std::isupper(isQTDisplay)){
                isQTDisplay = tolower(isQTDisplay);
            } else if(!std::islower(isQTDisplay)){
                PRINT_MESSAGE("Input Error")
                continue;
            }

            if(isQTDisplay == 'y'){
                PRINT_MESSAGE("Type the IP addr of QT Display")
                std::cin >> ipAddr;

                PRINT_MESSAGE("Type the port number of QT Display");
                std::cin >> portNumber;

                PRINT_MESSAGE("IP Addr : " + ipAddr + "\tPort Number : " + std::to_string(portNumber))

                servaddr.sin_port = htons(portNumber);
                servaddr.sin_addr.s_addr = inet_addr(ipAddr.c_str());

                if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
                    PRINT_MESSAGE(sizeof(servaddr))
                    PRINT_MESSAGE("Connection failed");
                    continue;
                }

                PRINT_MESSAGE("Connected to server")
                return SEND_ALLOWED;
            } else{
                PRINT_MESSAGE("Display will not be serviced")
                return DONT_SEND;
            }

        }while(true);

        maxID = 0;

    }

    void QTSender::AddFunc(const std::function<size_t()>& func, const std::string& name, const int& upperValue)
    {
        auto it = groupList.find(upperValue);
        if(it == groupList.end()){
            PRINT_ERROR("Add Group : There are no group name - " << upperValue);
        }

        QTCompTree* upperGroup = it->second;
        if(upperGroup->childList.size() > 0){
            PRINT_ERROR("Add Func : A inner node should not have the qt func")
        }

        QTCompInfo* info = new QTCompInfo(func, name);
        upperGroup->QTCompList.insert({name, info});
    }

    void QTSender::AddQTComp(QTComp* comp)
    {
        componentList.push_back(comp);
    }

    int QTSender::AddGroup(const std::string &groupName, const int &upperValue)
    {
        static int unique_value = TREE_ROOT_NUM + 1;

        auto it = groupList.find(upperValue);
        if(it == groupList.end()){
            PRINT_ERROR("Add Group : There are no group name - " << upperValue);
        }
        
        QTCompTree* upperGroup = it->second;
        if(upperGroup->QTCompList.size() > 0){
            PRINT_ERROR("Add Group : A leaf node should not have the child")
        }

        QTCompTree* group = new QTCompTree(groupName);
        groupList.insert({unique_value, group});
        upperGroup->childList.insert({groupName, group});

        return unique_value++;
    }

    void QTSender::SetMilestone(const uint64_t &totalReqs, const uint64_t QTSendCount)
    {
        this->totalReqs = totalReqs;
        this->QTSendCount = QTSendCount;
    }

    void QTSender::CreateGroupList()
    {
        for(auto& comp : componentList){
            comp->addQTComp(this);
        }
    }

    void QTSender::AppendValueList(const char *startPtr)
    {
        auto rootItr = groupList.find(TREE_ROOT_NUM);
        if(rootItr == groupList.end()){
            PRINT_ERROR("Append Value List : There are no root")
        }
        QTCompTree* root = rootItr->second;
        uint32_t offset = 0;

        std::function<void(QTCompInfo*)> appendCompInfo = [&](QTCompInfo* info) -> void {
            value_type value = info->func();
            std::memcpy((void*)(startPtr + (info->id * sizeof(value))), (const char*)&value, sizeof(value_type));
            offset += sizeof(value);
        };
        
        std::function<void(QTCompTree*)> traversalGroup = [&](QTCompTree* tree) -> void {
            if(tree->childList.size() > 0){
                for(auto& [childName, child] : tree->childList){
                    traversalGroup(child);
                }
            } else if(tree->QTCompList.size() > 0){
                for(auto& [compName, comp] : tree->QTCompList){
                    appendCompInfo(comp);
                }
            } else{
                PRINT_ERROR("Traversal group : There are no element - " << tree->groupName)
            }
        };

        traversalGroup(root);
    }

    std::string QTSender::SerializeGroupList()
    {
        std::string retValue = "";
        uint32_t currentID = 0;

        std::function<void(QTCompInfo*)> appendCompInfo = [&](QTCompInfo* info) -> void {
            info->id = currentID++;

            retValue.append(info->name);
            retValue.append("=");
            retValue.append(std::to_string(info->id));
        };

        std::function<void(QTCompTree*)> traversalGroup = [&](QTCompTree* tree) -> void {
            retValue.append(tree->groupName);
            if(tree->childList.size() > 0){
                retValue.append("{");
                for(auto& [childName, child] : tree->childList){
                    traversalGroup(child);
                }
                retValue.append("}");
            } else if(tree->QTCompList.size() > 0){
                retValue.append("{");
                for(auto& [compName, comp] : tree->QTCompList){
                    appendCompInfo(comp);
                    retValue.append(",");
                }
                retValue.pop_back();
                retValue.append("}");
            } else{
                PRINT_ERROR("Traversal group : There are no element in - " << tree->groupName)
            }
        };

        auto rootItr = groupList.find(TREE_ROOT_NUM);
        if(rootItr == groupList.end()){
            PRINT_ERROR("Serialize key list : There are no root")
        }
        QTCompTree* root = rootItr->second;

        for(auto& child : root->childList){
            traversalGroup(child.second);
        }

        maxID = currentID;

        return retValue;
    }

    void QTSender::SendConnPacket()
    {
        PRINT_MESSAGE("QT Sender sends a connection packet to the QT Display...")
        

        QTPacketMTDT mtdt;
        mtdt.type = CON;

        uint64_t sendCount = this->QTSendCount;
        uint64_t totalReqs = this->totalReqs;

        CreateGroupList();
        std::string keyList = SerializeGroupList();

        mtdt.length = sizeof(mtdt) + sizeof(sendCount) + sizeof(totalReqs) + keyList.size();


        const char* message = new char[mtdt.length];
        std::memset((void*)message, 0, mtdt.length);

        uint32_t offset = 0;
        std::memcpy((void*)message, (const char*)&mtdt, sizeof(mtdt));
        offset += sizeof(mtdt);

        std::memcpy((void*)(message + offset), (const char*)&sendCount, sizeof(sendCount));
        offset += sizeof(sendCount);

        std::memcpy((void*)(message + offset), (const char*)&totalReqs, sizeof(totalReqs));
        offset += sizeof(totalReqs);

        std::memcpy((void*)(message + offset), keyList.c_str(), keyList.size());
        send(sockfd, message, mtdt.length, 0);

        delete[] message;
    }

    void QTSender::SendDisconnPacket()
    {
        QTPacketMTDT mtdt;
        mtdt.type = DISCON;
        mtdt.length = sizeof(mtdt);
        
        const char* message = new char[mtdt.length];
        
        std::memcpy((void*)message, (const char*)&mtdt , sizeof(mtdt));

        send(sockfd, message, mtdt.length, 0);

        delete message;
    }

    
    void QTSender::SendCompInfoPacket(uint64_t curReqCount)
    {
        if(curReqCount == UINT64_MAX){
            curReqCount = totalReqs;
        }

        if((curReqCount % (totalReqs / QTSendCount)) == 0){

            QTPacketMTDT mtdt;
            mtdt.type = COMPINFO;
            mtdt.length = sizeof(mtdt) + sizeof(curReqCount) + (maxID * sizeof(value_type));

            const char* message = new char[mtdt.length];

            uint32_t offset = 0;

            std::memcpy((void*)(message + offset), (const char*)&mtdt, sizeof(mtdt));
            offset += sizeof(mtdt);

            std::memcpy((void*)(message + offset), (const char*)&curReqCount, sizeof(curReqCount));
            offset += sizeof(curReqCount);

            AppendValueList(message + offset);

            PRINT_MESSAGE("QT Sender sends a comp info packet to the QT Display...")
            PRINT_MESSAGE("Current Requests Count : " + std::to_string(curReqCount));
            send(sockfd, message, mtdt.length, 0);

            delete[] message;
        }
    }
}
