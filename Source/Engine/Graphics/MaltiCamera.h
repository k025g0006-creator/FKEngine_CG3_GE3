#pragma once

#include "ViewCamera.h"
#include <cstddef>
#include <deque>
#include <string>

// 登録したカメラを所有し、表示に使う1台を選択する。
class MaltiCamera
{
public:
    MaltiCamera() = default;
    MaltiCamera(const MaltiCamera&) = delete;
    MaltiCamera& operator=(const MaltiCamera&) = delete;

    ViewCamera& AddCamera(std::string name);
    void SetActiveCamera(std::size_t index);
    ViewCamera& GetActiveCamera() { return cameras_.at(activeIndex_).camera; }
    const ViewCamera& GetActiveCamera() const { return cameras_.at(activeIndex_).camera; }
    std::size_t GetActiveIndex() const { return activeIndex_; }
    std::size_t GetCameraCount() const { return cameras_.size(); }
    const std::string& GetName(std::size_t index) const { return cameras_.at(index).name; }

private:
    struct Entry
    {
        std::string name;
        ViewCamera camera;
    };
    // 追加登録しても既存カメラへの参照は有効。
    std::deque<Entry> cameras_;
    std::size_t activeIndex_ = 0;
};
