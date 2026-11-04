#pragma once

class DirectXBase;

// 全モデルが利用するDirectX基盤への参照。Modelより長く保持する。
class ModelBase
{
public:
    void Initialize(DirectXBase* directXBase);
    void Finalize() { directXBase_ = nullptr; }
    DirectXBase* GetDirectXBase() const { return directXBase_; }
private:
    DirectXBase* directXBase_ = nullptr; // 非所有
};
