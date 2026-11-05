#pragma once
// D3D オブジェクトにデバッグ名（デバッグレイヤー・PIX・RenderDoc に出る名前）を付ける関数
// 依存は <d3d11.h> と <string> だけにしてある。Graphics.h へ移すと、RHI の下の層が上の層に依存する
#include <d3d11.h>
#include <string>

// D3D のオブジェクトにデバッグ名を付ける（nullptr なら何もしない）
// 長さの違う名前で上書きすると SETPRIVATEDATA_CHANGINGPARAMS の警告が出るので、先に一度消してから付ける
template <typename T>
void SetDebugName(T *child, const std::string &name)
{
    if (!child) return;
    child->SetPrivateData(WKPDID_D3DDebugObjectName, 0, nullptr);
    child->SetPrivateData(WKPDID_D3DDebugObjectName,
                          static_cast<UINT>(name.size()), name.c_str());
}
