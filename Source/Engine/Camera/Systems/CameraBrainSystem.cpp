#include "CameraBrainSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/SceneView/Camera.h"
#include <algorithm>
#include "Engine/Core/Math/Interpolation.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX;

const VirtualCamera *CameraBrainSystem::SelectHighestPriorityCamera(EntityID &outID)
{
    EntityID             highestCamID = InvalidEntityID;
    int                  maxPriority  = 0;
    const VirtualCamera *highestVCam  = nullptr;

    ForEachWithID([&](EntityID id, const VirtualCamera &vCam) {
        // まだ一度も位置/注視点を計算していない vCam は採用しない
        // これを見ないと、生成直後（resultPos が {0,0,0} のまま）の vCam が
        // 最高優先度になった瞬間、メインカメラが原点にスナップする
        if (!vCam.isValid) return;

        if (highestVCam == nullptr)
        {
            maxPriority  = vCam.priority;
            highestCamID = id;
            highestVCam  = &vCam;
            return;
        }

        // 優先度が同値のときのタイブレーク
        //   チャンクの走査順はアーキタイプの生成順に依存するため、
        //   「先に見つかった方が勝ち」だとシーンや生成順で結果が変わり、
        //   再現しない不具合になる。EntityID で決定的に決める（後勝ち）
        const bool wins = (vCam.priority > maxPriority) ||
                          (vCam.priority == maxPriority && id > highestCamID);
        if (wins)
        {
            maxPriority  = vCam.priority;
            highestCamID = id;
            highestVCam  = &vCam;
        }
    });

    outID = highestCamID;
    return highestVCam;
}



void CameraBrainSystem::Update(float dt)
{
    if (!m_world->HasResource<Camera *>()) return;
    Camera *mainCamera = m_world->GetResource<Camera *>();
    if (!mainCamera) return;

    // 1. 最も優先度が高い vCam を探す
    EntityID             highestCamID = InvalidEntityID;
    const VirtualCamera *highestVCam  = SelectHighestPriorityCamera(highestCamID);

    if (highestCamID == InvalidEntityID || highestVCam == nullptr) return;

    // 2. カメラ切り替えの検知とブレンド開始
    if (highestCamID != m_currentCamID)
    {
        // 最初の1台はブレンドせず即座に切り替える
        // 経過時間をブレンド時間と同じにしておけば、下の isBlending が false になる
        if (m_currentCamID == InvalidEntityID)
        {
            m_currentCamID  = highestCamID;
            m_blendDuration = highestVCam->blendTime;
            m_blendTimer    = m_blendDuration;
        }
        else
        {
            m_prevCamID    = m_currentCamID;
            m_currentCamID = highestCamID;
            m_blendTimer   = 0.0f;

            // ブレンド時間は切り替え先の vCam の設定を使う
            m_blendDuration = highestVCam->blendTime;
        }
    }

    // 3. ブレンド計算
    XMVECTOR finalPos;
    XMVECTOR finalLook;
    XMVECTOR finalUp;
    float    finalFov;
    float    finalNear;
    float    finalFar;

    // 次のカメラ（ターゲット）の情報
    XMVECTOR targetPos  = XMLoadFloat3(&highestVCam->resultPos);
    XMVECTOR targetLook = XMLoadFloat3(&highestVCam->resultLookAt);
    XMVECTOR targetUp   = XMLoadFloat3(&highestVCam->resultUp);
    float    targetFov  = highestVCam->fov;
    float    targetNear = highestVCam->nearClip;
    float    targetFar  = highestVCam->farClip;

    // blendDuration が 0 以下でも t = timer/duration が NaN にならないよう、
    // ここで「ブレンド中か」を判定してから割り算する
    bool isBlending = (m_blendDuration > 0.0f) && (m_blendTimer < m_blendDuration);

    if (isBlending)
    {
        m_blendTimer += dt;
        // SmoothStep が 0..1 に丸めるので、ここでの上限クランプは不要
        const float t = CCL::Math::SmoothStep(m_blendTimer / m_blendDuration);

        VirtualCamera *prevVCam = m_world->GetComponent<VirtualCamera>(m_prevCamID);

        XMVECTOR srcPos, srcLook, srcUp;
        float    srcFov, srcNear, srcFar;

        // 遷移元がまだ生きていても、無効化されていれば値は信用できない
        // その場合は前フレームのバックアップから繋ぐ
        if (prevVCam && prevVCam->isValid)
        {
            srcPos  = XMLoadFloat3(&prevVCam->resultPos);
            srcLook = XMLoadFloat3(&prevVCam->resultLookAt);
            srcUp   = XMLoadFloat3(&prevVCam->resultUp);
            srcFov  = prevVCam->fov;
            srcNear = prevVCam->nearClip;
            srcFar  = prevVCam->farClip;
        }
        else
        {
            srcPos  = XMLoadFloat3(&m_lastPos);
            srcLook = XMLoadFloat3(&m_lastLook);
            srcUp   = XMLoadFloat3(&m_lastUp);
            srcFov  = m_lastFov;
            srcNear = m_lastNear;
            srcFar  = m_lastFar;
        }

        finalPos  = XMVectorLerp(srcPos, targetPos, t);
        finalLook = XMVectorLerp(srcLook, targetLook, t);
        finalUp   = XMVectorLerp(srcUp, targetUp, t);
        finalFov  = srcFov + (targetFov - srcFov) * t;
        finalNear = srcNear + (targetNear - srcNear) * t;
        finalFar  = srcFar + (targetFar - srcFar) * t;

        if (t >= 1.0f)
        {
            m_prevCamID = InvalidEntityID;
        }
    }
    else
    {
        // ブレンドなし
        finalPos  = targetPos;
        finalLook = targetLook;
        finalUp   = targetUp;
        finalFov  = targetFov;
        finalNear = targetNear;
        finalFar  = targetFar;
    }

    // 4. メインカメラへ反映
    XMFLOAT3 fPos, fLook, fUp;
    XMStoreFloat3(&fPos, finalPos);
    XMStoreFloat3(&fLook, finalLook);

    // Up は各 vCam が申告した値を使う（ダッチアングル等の傾け演出に対応するため）
    // 補間の結果ゼロベクトルに縮退した場合だけワールド上向きへフォールバックする
    finalUp = XMVector3Normalize(finalUp);
    if (XMVector3Equal(finalUp, XMVectorZero()) || XMVector3IsNaN(finalUp))
    {
        finalUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    }
    XMStoreFloat3(&fUp, finalUp);

    mainCamera->SetLookAt(fPos, fLook, fUp);

    // near が 0 以下や far <= near だと射影行列が壊れるので下限を設ける
    float safeNear = (std::max)(finalNear, 0.01f);
    float safeFar  = (std::max)(finalFar, safeNear + 0.1f);

    // アスペクト比は Camera が保持している最新値（ウィンドウリサイズ時に
    // Graphics 側が更新する）をそのまま引き継ぐ。ここでは FOV とクリップ面だけ触る
    float aspect = mainCamera->GetAspect();

    mainCamera->SetPerspectiveFov(DirectX::XMConvertToRadians(finalFov), aspect, safeNear, safeFar);

    // 次に遷移元が消えたときの補間元として保存する
    m_lastPos  = fPos;
    m_lastLook = fLook;
    m_lastUp   = fUp;
    m_lastFov  = finalFov;
    m_lastNear = safeNear;
    m_lastFar  = safeFar;
}



REGISTER_RENDER_SYSTEM(CameraBrainSystem, Priority::RenderStage::R01b_CameraBrain);
