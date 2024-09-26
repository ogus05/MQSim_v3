#ifndef QT_COMP_H
#define QT_COMP_H

namespace MQSimEngine{
    class QTSender;
    class QTComp{
    protected:
    public:
        QTComp();
        virtual void addQTComp(QTSender* sender) = 0;
    };
}

#endif //QT_COMP_H