#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#include <string>
#include <vector>
#include <algorithm>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

#ifdef ENABLE_EDITOR
// ComponentMetaImGui::Help の短縮名（直前の項目に "(?)" ヘルプを付ける）
static void Help(const char *text) { ComponentMetaImGui::Help(text); }
#endif

template <>
struct ComponentMeta<CameraBodyTPSComponent>
{
    CCL_META_USING_THIS(CameraBodyTPSComponent)

    static constexpr const char *displayName       = "Camera Body Action TPS";
    static constexpr const char *category          = CCL::Categories::Camera;
    static constexpr int         sortPriority      = 350;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(CameraBodyTPSComponent &comp, unsigned long long /*entityID*/ = 0, void * /*world*/ = nullptr)
    {
        namespace mi = ComponentMetaImGui;
        bool changed = false;

        ImGui::SeparatorText(u8"追従・注視ターゲット設定");

        auto RenderEntityLinkBox = [](const char *label, CCL::Serialization::EntityLink &link) -> bool {
            bool        dndChanged = false;
            std::string btnLabel   = (link.id == 0)
                                         ? std::string(label) + ": None (Drag Entity)"
                                         : std::string(label) + " ID: " + std::to_string(link.id);

            ImGui::Button(btnLabel.c_str(), ImVec2(-1, 0));

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_ENTITY_ID"))
                {
                    link.id    = *(const CCL::ECS::EntityID *)payload->Data;
                    dndChanged = true;
                }
                ImGui::EndDragDropTarget();
            }

            return dndChanged;
        };

        changed |= RenderEntityLinkBox("Player Target", comp.target);
        Help(u8"カメラが追従する対象。\n"
             u8"通常はプレイヤーのEntityを指定します。");

        changed |= RenderEntityLinkBox("LockOn Enemy", comp.lockOnTarget);
        Help(u8"ロックオン時に注視する敵のEntity。\n"
             u8"通常はゲーム側のロックオンシステムが設定します。");

        ImGui::SeparatorText(u8"ロックオン設定");

        changed |= mi::Checkbox(u8"ロックオン有効化", &comp.isLockOnActive, u8"変数名: isLockOnActive");
        Help(u8"ロックオンを有効にします。\n"
             u8"通常はゲーム側が自動的にON/OFFします。");

        changed |= mi::DragFloat(u8"ロックオン中のピッチ", &comp.lockOnPitch, 0.5f, -60.0f, 60.0f, u8"変数名: lockOnPitch");
        Help(u8"ロックオン中に保つカメラの上下角度。\n"
             u8"値を下げるほどカメラが高くなり、上から見下ろす視点になります。\n"
             u8"値を上げるほどカメラが低くなり、下から見上げる視点になります。\n"
             u8"上空のドローンを見たいときは正の値にします。");

        changed |= mi::DragFloat(u8"ロックオン中のピッチ補間", &comp.lockOnPitchDamping, 0.1f, 0.0f, 50.0f, u8"変数名: lockOnPitchDamping");
        Help(u8"現在のピッチからlockOnPitchへ寄る速さ。\n"
             u8"大きいほど素早く切り替わり、小さいほどゆっくり切り替わります。\n"
             u8"0にするとほぼ切り替わらなくなります。");

        changed |= mi::DragFloat(u8"敵への注視偏重率", &comp.lockOnSubjugationRatio, 0.01f, 0.0f, 0.5f, u8"変数名: lockOnSubjugationRatio");
        Help(u8"プレイヤーと敵の間のどこを見るかを決めます。\n"
             u8"0 = プレイヤー中心。\n"
             u8"値を大きくするほど敵側へ注視点が移動します。\n"
             u8"大きすぎるとプレイヤーが画面端へ寄りやすくなります。");

        changed |= mi::DragFloat(u8"注視ずれ上限", &comp.lockOnMaxLookShift, 0.1f, 0.0f, 15.0f, u8"変数名: lockOnMaxLookShift");
        Help(u8"ロックオン時にプレイヤーから敵側へずらせる注視点の最大距離(m)。\n"
             u8"大きいほど敵を中心にしやすくなります。\n"
             u8"小さいほどプレイヤーを画面内に残しやすくなります。");

        changed |= mi::DragFloat(u8"離間による引き量", &comp.lockOnDistanceGain, 0.01f, 0.0f, 2.0f, u8"変数名: lockOnDistanceGain");
        Help(u8"プレイヤーと敵の距離に応じてカメラを後ろへ引く量。\n"
             u8"大きいほど敵が遠くなったときカメラも大きく引きます。\n"
             u8"0にすると距離による追加の引きはありません。");

        changed |= mi::DragFloat(u8"引き量の上限", &comp.lockOnMaxExtraDistance, 0.1f, 0.0f, 30.0f, u8"変数名: lockOnMaxExtraDistance");
        Help(u8"ロックオンによって追加で引けるカメラ距離の最大値(m)。\n"
             u8"大きいほど遠距離の敵まで画面に収めやすくなります。");

        changed |= mi::Checkbox(u8"ロックオン中の手動操作", &comp.lockOnAllowManualCamera, u8"変数名: lockOnAllowManualCamera");
        Help(u8"ON = ロックオン中でも右スティックやマウスでカメラを操作できます。\n"
             u8"OFF = ロックオン中はカメラ操作を制限します。");

        changed |= mi::DragFloat(u8"Yawデッドゾーン", &comp.lockOnYawDeadZone, 0.5f, 0.0f, 180.0f, u8"変数名: lockOnYawDeadZone");
        Help(u8"敵が画面中央からずれていても自動補正しない角度範囲。\n"
             u8"大きいほど自動補正が発生しにくくなります。\n"
             u8"小さいほど敵を中央へ戻そうとします。");

        changed |= mi::DragFloat(u8"自動補正の強さ", &comp.lockOnAssistStrength, 0.1f, 0.0f, 20.0f, u8"変数名: lockOnAssistStrength");
        Help(u8"ロックオン中に敵方向へカメラを戻す強さ。\n"
             u8"大きいほど敵へ素早く向きます。\n"
             u8"大きすぎるとカメラが忙しく動く場合があります。");

        changed |= mi::DragFloat(u8"自動補正最大速度", &comp.lockOnMaxCorrectionSpeed, 1.0f, 0.0f, 500.0f, u8"変数名: lockOnMaxCorrectionSpeed");
        Help(u8"ロックオン自動補正でカメラが1秒間に回転できる最大速度(度/秒)。\n"
             u8"大きいほど敵へ素早く向きます。\n"
             u8"小さいほどゆっくり自然に補正します。");

        changed |= mi::DragFloat(u8"手動操作後の補正停止時間", &comp.lockOnManualOverrideTime, 0.01f, 0.0f, 5.0f, u8"変数名: lockOnManualOverrideTime");
        Help(u8"プレイヤーがカメラを操作した後、自動補正を停止する時間(秒)。\n"
             u8"大きいほどプレイヤー操作を優先します。\n"
             u8"小さいほどすぐに敵への自動補正が再開します。");

        ImGui::SeparatorText(u8"位置・追従");

        changed |= mi::DragFloat3(u8"注視点オフセット", &comp.targetOffset.x, 0.1f, 0.0f, 0.0f, u8"変数名: targetOffset");
        Help(u8"プレイヤーの基準位置から注視点をずらします。\n"
             u8"Yを上げると頭・肩付近を注視します。\n"
             u8"Yを下げると胸・足元寄りになります。");

        changed |= mi::DragFloat(u8"カメラ距離", &comp.distance, 0.1f, 1.0f, 100.0f, u8"変数名: distance");
        Help(u8"通常時のプレイヤーからカメラまでの距離(m)。\n"
             u8"大きいほどカメラが後ろへ下がります。\n"
             u8"小さいほどキャラクターへ近づきます。");

        changed |= mi::DragFloat(u8"寄り距離", &comp.closeUpDistance, 0.1f, 0.5f, 100.0f, u8"変数名: closeUpDistance");
        Help(u8"寄り要求中のカメラ距離(m)。\n"
             u8"カメラ距離より小さくすると寄りになります。\n"
             u8"要求のON/OFFはゲーム側が行います。");

        ImGui::SeparatorText(u8"視野角・レンズ");

        changed |= mi::DragFloat(u8"基本視野角", &comp.baseFov, 0.1f, 10.0f, 120.0f, u8"変数名: baseFov");
        Help(u8"通常時の視野角(FOV)。\n"
             u8"大きいほど広い範囲が映り、画面の遠近感が強くなります。\n"
             u8"小さいほど望遠レンズのようになります。");

        changed |= mi::DragFloat(u8"ダッシュ視野角", &comp.dashFov, 0.1f, 10.0f, 160.0f, u8"変数名: dashFov");
        Help(u8"ダッシュ中の視野角(FOV)。\n"
             u8"大きくするとダッシュ時にスピード感が強くなります。");

        ImGui::SeparatorText(u8"回転・オービット");

        changed |= mi::DragFloat(u8"初期ピッチ角", &comp.currentPitch, 0.5f, -85.0f, 85.0f, u8"変数名: currentPitch");
        Help(u8"カメラの初期上下角度。\n"
             u8"負の値ほど上から見下ろします。\n"
             u8"正の値ほど下から見上げます。");

        changed |= mi::DragFloat(u8"初期ヨー角", &comp.currentYaw, 0.5f, -360.0f, 360.0f, u8"変数名: currentYaw");
        Help(u8"カメラの初期左右角度。\n"
             u8"プレイヤーに対してどの方向からカメラを開始するかを決めます。");

        ImGui::SeparatorText(u8"回転制限");

        changed |= mi::DragFloat(u8"最小ピッチ制限", &comp.minPitch, 0.5f, -89.0f, 89.0f, u8"変数名: minPitch");
        Help(u8"カメラが上方向へ回り込める限界角度。\n"
             u8"より小さくすると高い位置から見下ろせます。");

        changed |= mi::DragFloat(u8"最大ピッチ制限", &comp.maxPitch, 0.5f, -89.0f, 89.0f, u8"変数名: maxPitch");
        Help(u8"カメラが下方向へ回り込める限界角度。\n"
             u8"より大きくすると低い位置から見上げられます。");

        ImGui::SeparatorText(u8"操作感度");

        changed |= mi::DragFloat(u8"回転速度X", &comp.lookSpeedX, 1.0f, 0.0f, 500.0f, u8"変数名: lookSpeedX");
        Help(u8"ゲームパッド右スティック左右の回転速度。\n"
             u8"大きいほど少しスティックを倒しただけで速く回転します。");

        changed |= mi::DragFloat(u8"回転速度Y", &comp.lookSpeedY, 1.0f, 0.0f, 500.0f, u8"変数名: lookSpeedY");
        Help(u8"ゲームパッド右スティック上下の回転速度。\n"
             u8"大きいほど上下視点が速く動きます。");

        ImGui::SeparatorText(u8"マウス視点");

        changed |= mi::Checkbox(u8"右クリック中のみ視点回転", &comp.mouseLookRequiresButton, u8"変数名: mouseLookRequiresButton");
        Help(u8"ON = 右クリック中だけマウスで視点を操作します。\n"
             u8"OFF = 常にマウスで視点を操作します。");

        changed |= mi::DragFloat(u8"マウス水平感度", &comp.mouseSensitivityX, 0.01f, 0.0f, 2.0f, u8"変数名: mouseSensitivityX");
        Help(u8"マウス左右移動に対するカメラの回転感度。\n"
             u8"大きいほど少ないマウス移動で大きく回転します。");

        changed |= mi::DragFloat(u8"マウス垂直感度", &comp.mouseSensitivityY, 0.01f, 0.0f, 2.0f, u8"変数名: mouseSensitivityY");
        Help(u8"マウス上下移動に対するカメラの回転感度。\n"
             u8"大きいほど上下視点が速く動きます。");

        changed |= mi::Checkbox(u8"上下反転", &comp.mouseInvertY, u8"変数名: mouseInvertY");
        Help(u8"ONにするとマウス上下操作を反転します。");

        changed |= mi::Checkbox(u8"カーソル中央固定", &comp.enableCursorLock, u8"変数名: enableCursorLock");
        Help(u8"ONにするとマウスルック中にカーソルを画面中央へ固定します。\n"
             u8"画面端にカーソルが到達して視点操作が止まるのを防ぎます。");

        ImGui::SeparatorText(u8"滑らかさ");

        changed |= mi::DragFloat(u8"位置追従遅延", &comp.positionDamping, 0.1f, 0.0f, 50.0f, u8"変数名: positionDamping");
        Help(u8"プレイヤーへのカメラ追従の速さ。\n"
             u8"大きいほどプレイヤーへ素早く追従します。\n"
             u8"小さいほど遅れて追従し、ゴムのような動きになります。");

        changed |= mi::DragFloat(u8"視野角補間速度", &comp.fovDamping, 0.1f, 0.0f, 50.0f, u8"変数名: fovDamping");
        Help(u8"通常FOVとダッシュFOVの切り替わり速度。\n"
             u8"大きいほど素早く変化します。\n"
             u8"小さいほどゆっくり滑らかに変化します。");

        ImGui::SeparatorText(u8"演出の上書き");

        changed |= mi::DragFloat(u8"上書き中の引く速さ", &comp.framingFollowDamping, 0.5f, 1.0f, 100.0f, u8"変数名: framingFollowDamping");
        Help(u8"演出（ラッシュなど）がカメラを引くときの追従の速さ。\n"
             u8"地形に当たっていないときだけ使います。\n"
             u8"小さいと引きが演出に間に合いません。");

        changed |= mi::DragFloat(u8"上書きの注視ずれ上限", &comp.framingMaxLookShift, 0.1f, 0.0f, 15.0f, u8"変数名: framingMaxLookShift");
        Help(u8"演出が注視点を敵へ寄せるときの、ずらす量の上限(m)。\n"
             u8"大きいほど敵が画面中央に来ますが、プレイヤーが画面外へ出やすくなります。");

        ImGui::SeparatorText(u8"カメラ衝突");

        changed |= mi::Checkbox(u8"衝突回避", &comp.collisionEnabled, u8"変数名: collisionEnabled");
        Help(u8"ONにすると壁や地形にカメラがめり込まないようにします。");

        changed |= mi::DragFloat(u8"衝突半径", &comp.collisionRadius, 0.01f, 0.0f, 2.0f, u8"変数名: collisionRadius");
        Help(u8"壁からカメラをどれだけ離すかの余白(m)。\n"
             u8"大きいほど壁から離れた位置で止まります。");

        changed |= mi::DragFloat(u8"最小距離", &comp.collisionMinDistance, 0.05f, 0.1f, 10.0f, u8"変数名: collisionMinDistance");
        Help(u8"壁に押し込まれた際のカメラとプレイヤーの最小距離(m)。\n"
             u8"大きいほどカメラがプレイヤーへ近づきにくくなります。");

        changed |= mi::DragFloat(u8"寄せる速さ", &comp.collisionInDamping, 0.5f, 1.0f, 100.0f, u8"変数名: collisionInDamping");
        Help(u8"壁にぶつかったときカメラを手前へ寄せる速さ。\n"
             u8"大きいほど素早く壁から離れます。");

        changed |= mi::DragFloat(u8"戻す速さ", &comp.collisionOutDamping, 0.5f, 1.0f, 100.0f, u8"変数名: collisionOutDamping");
        Help(u8"壁から離れたときカメラを元の距離へ戻す速さ。\n"
             u8"小さいほどゆっくり戻り、大きいほど素早く戻ります。");

        changed |= mi::DragFloat(u8"頭寄せ", &comp.collisionHeadBias, 0.01f, 0.0f, 1.0f, u8"変数名: collisionHeadBias");
        Help(u8"壁に押されてカメラが近づいたときの高さ補正。\n"
             u8"0 = 通常の位置。\n"
             u8"1 = プレイヤーの注視点の高さへ強く寄せます。\n"
             u8"大きいほど足元が映りにくくなります。");

        ImGui::SeparatorText(u8"ルックアヘッド");

        changed |= mi::DragFloat(u8"前方注視係数", &comp.lookaheadMultiplier, 0.05f, 0.0f, 5.0f, u8"変数名: lookaheadMultiplier");
        Help(u8"プレイヤーの移動方向へ注視点を先行させる量。\n"
             u8"大きいほど進行方向を先に見るようになります。\n"
             u8"0にするとルックアヘッドを無効化します。");

        changed |= mi::DragFloat(u8"注視遅延", &comp.lookaheadDamping, 0.1f, 0.0f, 50.0f, u8"変数名: lookaheadDamping");
        Help(u8"ルックアヘッド位置へ移動する速さ。\n"
             u8"大きいほど素早く移動し、小さいほど遅れて追従します。");

        return changed;
    }

#else
    static bool CustomGui(CameraBodyTPSComponent &, unsigned long long, void *)
    {
        return false;
    }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(target, "追従ターゲット", "Settings")
            CFIELD_FLOAT3_FAST(targetOffset, "注視点オフセット", 0.1f, "Positioning")
            CFIELD_FLOAT_RANGED(distance, "カメラ距離", 0.1f, 1.0f, 100.0f, "Positioning")
            CFIELD_FLOAT_RANGED(closeUpDistance, "寄り距離", 0.1f, 0.5f, 100.0f, "Positioning")

            CFIELD_FLOAT_RANGED(baseFov, "基本視野角", 1.0f, 10.0f, 120.0f, "Dynamic Lens")
            CFIELD_FLOAT_RANGED(dashFov, "ダッシュ視野角", 1.0f, 10.0f, 160.0f, "Dynamic Lens")

            CFIELD_FLOAT_RANGED(currentPitch, "初期ピッチ角", 1.0f, -85.0f, 85.0f, "Orbit Settings")
            CFIELD_FLOAT_RANGED(currentYaw, "初期ヨー角", 1.0f, -360.0f, 360.0f, "Orbit Settings")
            CFIELD_FLOAT_RANGED(minPitch, "最小ピッチ制限", 1.0f, -89.0f, 89.0f, "Orbit Constraints")
            CFIELD_FLOAT_RANGED(maxPitch, "最大ピッチ制限", 1.0f, -89.0f, 89.0f, "Orbit Constraints")
            CFIELD_FLOAT_RANGED(lookSpeedX, "回転速度X", 1.0f, 0.0f, 500.0f, "Control Settings")
            CFIELD_FLOAT_RANGED(lookSpeedY, "回転速度Y", 1.0f, 0.0f, 500.0f, "Control Settings")

            CFIELD(mouseLookRequiresButton, "右クリック中のみ視点回転", "Mouse Look")
            CFIELD_FLOAT_RANGED(mouseSensitivityX, "マウス水平感度", 0.01f, 0.0f, 2.0f, "Mouse Look")
            CFIELD_FLOAT_RANGED(mouseSensitivityY, "マウス垂直感度", 0.01f, 0.0f, 2.0f, "Mouse Look")
            CFIELD(mouseInvertY, "上下反転", "Mouse Look")
            CFIELD(enableCursorLock, "カーソル中央固定", "Mouse Look")

            CFIELD_FLOAT_RANGED(positionDamping, "位置追従遅延", 0.1f, 0.0f, 50.0f, "Smoothing")
            CFIELD_FLOAT_RANGED(fovDamping, "視野角補間速度", 0.1f, 0.0f, 50.0f, "Smoothing")

            CFIELD_FLOAT_RANGED(framingFollowDamping, "上書き中の引く速さ", 0.5f, 1.0f, 100.0f, "Framing Override")
            CFIELD_FLOAT_RANGED(framingMaxLookShift, "上書きの注視ずれ上限", 0.1f, 0.0f, 15.0f, "Framing Override")

            CFIELD(collisionEnabled, "衝突回避", "Collision")
            CFIELD_FLOAT_RANGED(collisionRadius, "衝突半径", 0.01f, 0.0f, 2.0f, "Collision")
            CFIELD_FLOAT_RANGED(collisionMinDistance, "最小距離", 0.05f, 0.1f, 10.0f, "Collision")
            CFIELD_FLOAT_RANGED(collisionInDamping, "寄せる速さ", 0.5f, 1.0f, 100.0f, "Collision")
            CFIELD_FLOAT_RANGED(collisionOutDamping, "戻す速さ", 0.5f, 1.0f, 100.0f, "Collision")
            CFIELD_FLOAT_RANGED(collisionHeadBias, "頭寄せ", 0.01f, 0.0f, 1.0f, "Collision")

            CFIELD_FLOAT_RANGED(lookaheadMultiplier, "前方注視係数", 0.05f, 0.0f, 5.0f, "Lookahead")
            CFIELD_FLOAT_RANGED(lookaheadDamping, "注視遅延", 0.1f, 0.0f, 50.0f, "Lookahead")

            CFIELD_FLOAT_RANGED(lockOnPitch, "ロックオン中のピッチ", 0.5f, -60.0f, 60.0f, "LockOn")
            CFIELD_FLOAT_RANGED(lockOnPitchDamping, "ロックオン中のピッチ補間", 0.1f, 0.0f, 50.0f, "LockOn")
            CFIELD_FLOAT_RANGED(lockOnSubjugationRatio, "敵への注視偏重率", 0.01f, 0.0f, 0.5f, "LockOn")
            CFIELD_FLOAT_RANGED(lockOnMaxLookShift, "注視ずれ上限", 0.1f, 0.0f, 15.0f, "LockOn")
            CFIELD_FLOAT_RANGED(lockOnDistanceGain, "離間による引き量", 0.01f, 0.0f, 2.0f, "LockOn")
            CFIELD_FLOAT_RANGED(lockOnMaxExtraDistance, "引き量の上限", 0.1f, 0.0f, 30.0f, "LockOn")
            CFIELD(lockOnAllowManualCamera, "ロックオン中の手動操作", "LockOn")
            CFIELD_FLOAT_RANGED(lockOnYawDeadZone, "Yawデッドゾーン", 0.5f, 0.0f, 180.0f, "LockOn")
            CFIELD_FLOAT_RANGED(lockOnAssistStrength, "自動補正の強さ", 0.1f, 0.0f, 20.0f, "LockOn")
            CFIELD_FLOAT_RANGED(lockOnMaxCorrectionSpeed, "自動補正最大速度", 1.0f, 0.0f, 500.0f, "LockOn")
            CFIELD_FLOAT_RANGED(lockOnManualOverrideTime, "手動操作後の補正停止時間", 0.01f, 0.0f, 5.0f, "LockOn")};
        return fields;
    }
};

REGISTER_COMPONENT(CameraBodyTPSComponent, "CameraBodyTPS")
REGISTER_COMPONENT_EDITOR_GUI(CameraBodyTPSComponent)
