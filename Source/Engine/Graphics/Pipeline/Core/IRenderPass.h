#pragma once
// ===========================================================================
// 概要: 描画の1段階（影・不透明・半透明・ポストプロセスなど）を表すパスの基底クラス
//
// 責務:
//   - Initialize で受け取った RenderServices を保持し、派生クラスに m_services で渡す
//   - 毎フレームの Execute、終了時の Shutdown、サイズ変更時の OnResize の口を用意する
//   - RenderPipeline が設定する表示名と Tracy の計測用の情報を持つ
//
// 備考: エンジンが終わるまで変わらないもの（RenderServices）と、そのフレームだけ有効な
//       もの（RenderFrameContext）を分けて受け取る。Execute が引数に取るのは後者だけにして、
//       フレームごとに変わるデータがどれかを型で分かるようにしている
// ===========================================================================
#include <cstdint>
#include <string>
#include "Engine/Graphics/RHI/RenderContext.h"
#include "tracy/Tracy.hpp"

class IRenderPass
{
public:
    virtual ~IRenderPass() = default;

    // RenderPipeline::AddPass が型名から設定する
    std::string m_passName      = "Unknown Pass"; // GPU マーカー（PIX / RenderDoc）に出す名前
    std::string m_tracyZoneName = "Pass: Unknown"; // Tracy の CPU ゾーン名（毎フレームの文字列生成を避けるため持っておく）

#if defined(TRACY_ENABLE)
    // Tracy の GPU ゾーンの情報（RenderPipeline::AddPass の関数内 static を指す。所有しない）
    const tracy::SourceLocationData *m_gpuSourceLocation = nullptr;
#endif

    // services を持つ。ポインタはエンジンの終了まで有効
    // GPU のリソースが要る派生クラスは、上書きして先に IRenderPass::Initialize を呼ぶ
    virtual void Initialize(const RenderServices &services)
    {
        m_services = &services;
    }

    // エンジンの終了時に1回だけ呼ばれる
    virtual void Shutdown() {}

    // 毎フレームの描画。ctx はこのフレームの間だけ有効
    virtual void Execute(const RenderFrameContext &ctx) = 0;

    // 画面サイズが変わったとき、中で持っているレンダーターゲットを作り直す
    virtual void OnResize(uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;
    }

    // false のパスは RenderPipeline::Execute で飛ばす
    bool enabled = true;

protected:
    const RenderServices *m_services = nullptr; // Initialize で受け取ったもの（所有しない）
};
