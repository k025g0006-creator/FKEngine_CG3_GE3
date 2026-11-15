#include "ModelBase.h"
#include "DirectXBase.h"
#include <stdexcept>

void ModelBase::Initialize(DirectXBase* directXBase)
{
    if (directXBase_ || !directXBase || !directXBase->GetDevice()) {
        throw std::invalid_argument("ModelBase requires initialized DirectXBase and one Initialize call.");
    }
    directXBase_ = directXBase;
}
