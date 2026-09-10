#pragma once
#include <d3d11.h>
#include <string>

// ============================================================================
//  D3D デバッグ名ユーティリティ
// ----------------------------------------------------------------------------
//  依存は <d3d11.h> と <string> のみ。
//  この関数は「D3D オブジェクトに名前を付ける」だけの葉ユーティリティであり、
//  Graphics（描画サブシステムのコーディネータ）とは無関係なので独立させている。
//
//  以前は Graphics.h に置いていたが、その結果
//    ・GraphicsDevice.cpp / RenderTarget.cpp（Graphics の“下位”にあたる RHI 層）が
//      名前を付けたいだけで Graphics.h を include する
//    ・= RHI 層 → コーディネータ という依存の逆流・循環が生まれる
//    ・Graphics.h は RenderPipeline / 全 Renderer / PostProcessManager を巻き込むため
//      無関係なファイルまで巨大な依存を背負う
//  という問題が起きていた。
// ============================================================================

/**
 * @brief D3D デバッグレイヤーでオブジェクト名を設定する。
 * @param child 名前を付ける D3D オブジェクト（nullptr 可。その場合は何もしない）。
 * @param name  デバッグレイヤー／PIX／RenderDoc に表示される名前。
 * @details
 *  既に別サイズの名前が付いている状態で上書きすると
 *  "SETPRIVATEDATA_CHANGINGPARAMS" 警告が出る（例: GpuResourceUtils::LoadTexture が
 *  汎用名を付けた直後に、呼び出し元がファイル名入りの具体名で付け直すケース）。
 *  上書き前に一度クリアしておけば、サイズ違いとして検出されず警告が出ない。
 */
template <typename T>
void SetDebugName(T* child, const std::string& name)
{
    if (!child) return;
    child->SetPrivateData(WKPDID_D3DDebugObjectName, 0, nullptr);
    child->SetPrivateData(WKPDID_D3DDebugObjectName,
        static_cast<UINT>(name.size()), name.c_str());
}
