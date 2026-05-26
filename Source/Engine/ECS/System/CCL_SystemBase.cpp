/**
 * @file   CCL_SystemBase.cpp
 * @brief  SystemBaseの実装 (タイミング計測などヘッダから分離した処理)
 *
 * @details
 *  本ファイルは以下を実装します:
 *   - コンストラクタ (SystemIDの自動発番)
 *   - UpdateImpl: Update呼び出しと処理時間計測
 *
 *  \<chrono\> 等の重いヘッダはここでだけ include されるため、
 *  SystemBase.h を include する側のビルド負荷を抑制できます。
 *
 * @author CCL ECS Library
 */
#include "CCL_SystemBase.h"

#include <chrono>


namespace CCL::ECS
{
    // =========================================================================
    //  コンストラクタ
    // =========================================================================

    SystemBase::SystemBase(const std::string& name)
        : _systemID(ID_Emitter::EmitSystemID())
        , _name(name)
    {
    }


    // =========================================================================
    //  UpdateImpl: 処理時間計測付きUpdate
    // =========================================================================

    void SystemBase::UpdateImpl(float dt)
    {
        using clock     = std::chrono::high_resolution_clock;
        using ms_double = std::chrono::duration<float, std::milli>;

        const auto start = clock::now();

        Update(dt);

        const auto end     = clock::now();
        const float elapsed = std::chrono::duration_cast<ms_double>(end - start).count();

        _lastUpdateTime = elapsed;

        // 0.1の重みで指数移動平均(EMA)──UIのちらつき抑制
        constexpr float kSmoothFactor = 0.1f;
        _displayUpdateTime = (_displayUpdateTime * (1.0f - kSmoothFactor))
                           + (elapsed         *        kSmoothFactor);
    }

} // namespace CCL::ECS
