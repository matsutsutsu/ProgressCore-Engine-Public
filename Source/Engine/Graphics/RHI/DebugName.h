#pragma once
// ===========================================================================
// 概要: D3D オブジェクトにデバッグ名を付ける関数。
//
// 責務:
//   - デバッグレイヤー・PIX・RenderDoc に表示される名前を設定する
//
// 備考: 依存は <d3d11.h> と <string> だけにしてある。Graphics.h へ移してはいけない。
//       移すと、名前を付けたいだけの GraphicsDevice.cpp や RenderTarget.cpp（Graphics より
//       下の RHI 層）が Graphics.h を include することになり、下の層が上の層に依存する。
//       さらに Graphics.h は RenderPipeline・全 Renderer・PostProcessManager を含むので、
//       関係の無いファイルまで大きな依存を背負う。
// ===========================================================================
#include <d3d11.h>
#include <string>

/**
 * @brief D3D オブジェクトにデバッグ名を付ける。
 *
 * 違う長さの名前が付いている状態で上書きすると "SETPRIVATEDATA_CHANGINGPARAMS" 警告が出る
 * （例: GpuResourceUtils::LoadTexture が汎用の名前を付けた直後に、呼び出し側がファイル名入りの
 * 名前で付け直す場合）。先に一度消しておけば、長さの違いとして検出されず警告は出ない。
 *
 * @param child 名前を付ける D3D オブジェクト（nullptr なら何もしない）
 * @param name デバッグレイヤー・PIX・RenderDoc に表示される名前
 */
template <typename T>
void SetDebugName(T *child, const std::string &name)
{
    if (!child) return;
    child->SetPrivateData(WKPDID_D3DDebugObjectName, 0, nullptr);
    child->SetPrivateData(WKPDID_D3DDebugObjectName,
                          static_cast<UINT>(name.size()), name.c_str());
}
