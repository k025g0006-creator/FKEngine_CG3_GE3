#include "MaltiCamera.h"
#include <stdexcept>
#include <utility>

ViewCamera& MaltiCamera::AddCamera(std::string name)
{
    cameras_.push_back({ std::move(name), ViewCamera{} });
    return cameras_.back().camera;
}

void MaltiCamera::SetActiveCamera(std::size_t index)
{
    if (index >= cameras_.size()) { throw std::out_of_range("Camera index is not registered."); }
    activeIndex_ = index;
}
