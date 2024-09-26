#include "QTComp.h"
#include "Engine.h"

namespace MQSimEngine{
    QTComp::QTComp()
    {
        Simulator->AddQTComp(this);
    }
}
