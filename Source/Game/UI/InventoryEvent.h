//#pragma once
//
//#include <functional>
//#include <vector>
//
//#include "Engine/Graphics/Core/Graphics.h"
//
//#include "Engine/UI/UIManager.h"
//
//#include "Engine/UI/Text/TextManager.h"
//
//#include "Game/Logics/Combat/Weapon/Core/GunBuilder.h"
//
//#include "Engine/Platform/Input/Input.h"
//
//class AttachmentButton : public UIElement, public std::enable_shared_from_this<AttachmentButton> {
//  public:
//    EffectID effect;
//
//    TextManager &textManager;
//    std::string  str;
//
//  private:
//    std::function<void()> onReleaseCallback;
//
//  public:
//    // コンストラクタ
//    AttachmentButton(const std::string &name,
//        const std::string              &spriteName,
//        EffectID                        effectID,
//        TextManager                    &textManager,
//        std::string                     str,
//        std::function<void()>           onRelease)
//        : UIElement(name, spriteName), effect(effectID), onReleaseCallback(onRelease),
//          textManager(textManager), str(str)
//    {
//    }
//    virtual ~AttachmentButton()
//    {
//        // ボタンが破棄されるときにテキストも消す
//        textManager.RemoveText(m_name);
//    }
//
//    // --- ホバー演出 ---
//    void OnHoverEnter() override
//    {
//        UIElement::OnHoverEnter();
//        ScaleTo(m_baseScale * 1.1f);
//
//        if (textManager.HasText(GetName())) return;
//
//        Text text{};
//        text.text   = str;
//        text.x      = m_position.x;
//        text.y      = m_position.y;
//        text.width  = 32.0f;
//        text.height = 32.0f;
//        TextAnimation animation{};
//        animation.startAlpha = 0.0f;
//        animation.endAlpha   = 1.0f;
//        animation.duration   = 0.3f;
//        animation.type       = EasingType::OutBack;
//        text.animation       = animation;
//        textManager.AddText(m_name, text);
//        textManager.AddGroupText("Hover", GetName());
//    }
//
//    void OnHoverExit() override
//    {
//        UIElement::OnHoverExit();
//        ScaleTo(m_baseScale);
//
//        if (textManager.HasText(GetName())) {
//            auto          text = textManager.GetText(GetName());
//            TextAnimation animation{};
//            float         lifetime = 0.2f;
//            animation.startAlpha   = text->a;
//            animation.endAlpha     = 0.0f;
//            animation.duration     = lifetime;
//            animation.type         = EasingType::InBack;
//            text->animation        = animation;
//            text->lifetime         = TextLifetime{lifetime};
//        }
//    }
//
//    void OnPress() override
//    {
//        UIElement::OnPress();
//        textManager.RemoveText(m_name);
//    }
//
//    void OnRelease() override
//    {
//        auto self = shared_from_this();
//        UIElement::OnRelease();
//        onReleaseCallback();
//    }
//};
//
//struct InventoryEvent {
//    GunBuilder *gunBuilder;
//};
//
//struct AttachmentUIGroup {
//    std::vector<std::shared_ptr<UIElement>> elements;
//    std::vector<int>                        slotIndex;
//    std::vector<DirectX::XMFLOAT2>          slotPositions;
//    std::vector<DirectX::XMFLOAT2>          originalPositions;
//    std::vector<DirectX::XMFLOAT2>          storagePositions;
//    std::vector<EffectID>                   storageEffects;
//    std::vector<int>                        storageValues;
//};
//
//#define FILE_SLOTEDIT_SPRITE "Assets/Textures/slotedit.png"
//#define FILE_SLOT_SPRITE "Assets/Textures/slot.png"
//#define FILE_STORAGE_SLOT_SPRITE "Assets/Textures/Storage.png"
//
//class InventorySystem {
//  private:
//    std::string  path = "Assets/Textures/attachment/";
//    UIManager   *uiManager;
//    TextManager &textManager;
//    bool         isOpened = false;
//
//    // ここで各フェーズのUIグループを管理
//    AttachmentUIGroup spawnUI;
//    AttachmentUIGroup travelUI;
//    AttachmentUIGroup onHitUI;
//    AttachmentUIGroup endUI;
//
//    std::shared_ptr<UIElement> storageSlotedit;
//    std::shared_ptr<UIElement> storageSlot;
//
//    std::shared_ptr<AttachmentUIGroup> movedGroup;
//
//    // 1 行に置く最大数
//    const int maxPerRow = 4;
//    // 横間隔
//    const float slotW = 80.0f;
//    // 縦間隔
//    const float slotH = 80.0f;
//
//  public:
//    InventorySystem(UIManager *uiManager, TextManager &textManager)
//        : uiManager(uiManager), textManager(textManager)
//    {
//        // スプライトの事前ロード
//        uiManager->LoadSprite("UI_slotedit", FILE_SLOTEDIT_SPRITE);
//        uiManager->LoadSprite("UI_slot", FILE_SLOT_SPRITE);
//        uiManager->LoadSprite("UI_storageSlot", FILE_STORAGE_SLOT_SPRITE);
//
//        uiManager->LoadSprite("UI_chaos", (path + "chaos.png").c_str());
//        uiManager->LoadSprite("UI_division", (path + "division.png").c_str());
//        uiManager->LoadSprite("UI_penetration", (path + "penetration.png").c_str());
//        uiManager->LoadSprite("UI_speed", (path + "speed.png").c_str());
//
//        uiManager->LoadSprite("UI_explosion", (path + "explosion.png").c_str());
//        uiManager->LoadSprite("UI_deceleration", (path + "deceleration.png").c_str());
//        uiManager->LoadSprite("UI_reflection", (path + "reflection.png").c_str());
//
//        // 各グループのストレージ位置クリア
//        spawnUI.storagePositions.clear();
//        spawnUI.storageEffects.clear();
//        travelUI.storagePositions.clear();
//        travelUI.storageEffects.clear();
//        onHitUI.storagePositions.clear();
//        onHitUI.storageEffects.clear();
//        endUI.storagePositions.clear();
//        endUI.storageEffects.clear();
//
//        // ランダムに３つエフェクトをストレージに追加しておく
//        // (注: 元のコードのランダム追加処理はそのまま維持)
//        auto spawn = [&](AttachmentUIGroup &spawns, int index) {
//            int   randIndex = rand() % 7;
//            float posX      = Graphics::Instance().GetScreenWidth() / 2.0f + (index - 1) * 80.0f;
//            float posY      = Graphics::Instance().GetScreenHeight() - 300.0f;
//            switch (randIndex) {
//            case 0: AddAttachmentToStorage(spawns, EffectID::Chaos, "Chaos", {posX, posY}); break;
//            case 1: AddAttachmentToStorage(spawns, EffectID::Pierce, "Pierce", {posX, posY}); break;
//            case 2: AddAttachmentToStorage(spawns, EffectID::Split, "Split", {posX, posY}); break;
//            case 3:
//                AddAttachmentToStorage(spawns, EffectID::Accelerate, "Speed", {posX, posY});
//                break;
//            case 4:
//                AddAttachmentToStorage(spawns, EffectID::Explosion, "Explosion", {posX, posY});
//                break;
//            case 5:
//                AddAttachmentToStorage(spawns, EffectID::Slow, "Deceleration", {posX, posY});
//                break;
//            case 6:
//                AddAttachmentToStorage(spawns, EffectID::Reflection, "Reflection", {posX, posY});
//                break;
//            };
//        };
//        for (int i = 0; i < 3; ++i) {
//            int randGroup = rand() % 4;
//            switch (randGroup) {
//            case 0: spawn(spawnUI, i); break;
//            case 1: spawn(travelUI, i); break;
//            case 2: spawn(onHitUI, i); break;
//            case 3: spawn(endUI, i); break;
//            }
//        }
//    }
//
//    AttachmentUIGroup &GetSpawnAttachmentUIGroup() { return spawnUI; }
//    AttachmentUIGroup &GetTravelAttachmentUIGroup() { return travelUI; }
//    AttachmentUIGroup &GetOnHitAttachmentUIGroup() { return onHitUI; }
//    AttachmentUIGroup &GetEndAttachmentUIGroup() { return endUI; }
//
//    void AddAttachmentToStorage(
//        AttachmentUIGroup &group, EffectID effect, std::string name, DirectX::XMFLOAT2 position)
//    {
//        if (effect == EffectID::None) return;
//        group.storageEffects.push_back(effect);
//        group.storagePositions.push_back(position);
//        uiManager->AddUIGroup("Inventory", name);
//    }
//
//    void OpenInventory(InventoryEvent &event)
//    {
//        if (isOpened) return;
//
//        isOpened = true;
//        auto gun = event.gunBuilder;
//
//        // ストレージスロット編集用の描画
//        storageSlotedit = std::make_shared<UIElement>("UI_storageSlotedit", FILE_SLOTEDIT_SPRITE);
//        storageSlotedit->SetPosition(Graphics::Instance().GetScreenWidth() / 2.0f,
//            (Graphics::Instance().GetScreenHeight() / 3.0f));
//        storageSlotedit->SetSize(Graphics::Instance().GetScreenWidth() / 1.2f,
//            (Graphics::Instance().GetScreenHeight() / 1.7f));
//        uiManager->AddElement(storageSlotedit);
//        uiManager->AddUIGroup("Inventory", storageSlotedit->GetName());
//
//        // ストレージスロットの描画
//        storageSlot    = std::make_shared<UIElement>("UI_storageSlot", FILE_STORAGE_SLOT_SPRITE);
//        float storageX = Graphics::Instance().GetScreenWidth() / 2.0f;
//        float storageY = Graphics::Instance().GetScreenHeight() * 3.0f / 4.0f;
//        storageSlot->SetPosition(storageX, storageY);
//        storageSlot->SetSize(1024.0f, 256.0f);
//        uiManager->AddElement(storageSlot);
//        uiManager->AddUIGroup("Inventory", storageSlot->GetName());
//
//        // 横順
//        float spawnX  = 270.0f;
//        float travelX = spawnX + slotW * 6.0f - 100.0f;
//        float onHitX  = travelX + slotW * 6.0f - 90.0f;
//        float endX    = onHitX + slotW * 6.0f - 90.0f;
//
//        int   rows = 1;
//        float spawnY =
//            Graphics::Instance().GetScreenHeight() / 2.0f - (rows * slotH) + slotH * 1.0f;
//        float travelY = spawnY;
//        float onHitY  = travelY;
//        float endY    = onHitY;
//
//        // スロットの描画
//        CreateSlots(gun->maxSpawnSlots, spawnX, spawnY, "spawnSlot_");
//        CreateSlots(gun->maxTravelSlots, travelX, travelY, "travelSlot_");
//        CreateSlots(gun->maxOnHitSlots, onHitX, onHitY, "onHitSlot_");
//        CreateSlots(gun->maxEndSlots, endX, endY, "endSlot_");
//
//        // 各フェーズのUI構築
//        BuildAttachmentUI(
//            gun->spawnMods, spawnUI, gun->maxSpawnSlots, spawnX, spawnY, 0, {0, 1, 0, 1});
//        BuildAttachmentUI(
//            gun->travelMods, travelUI, gun->maxTravelSlots, travelX, travelY, 1, {0, 0, 1, 1});
//        BuildAttachmentUI(
//            gun->onHitMods, onHitUI, gun->maxOnHitSlots, onHitX, onHitY, 2, {1, 0, 0, 1});
//        BuildAttachmentUI(gun->endMods, endUI, gun->maxEndSlots, endX, endY, 3, {1, 0, 1, 1});
//    }
//
//    void UpdateInventory(InventoryEvent &event)
//    {
//        if (!isOpened) return;
//
//        auto &mouse = Input::Instance().GetMouse();
//
//        auto updateGroup = [&](AttachmentUIGroup &group) {
//            for (int i = 0; i < group.elements.size(); ++i) {
//                auto &ui = group.elements[i];
//                // ドラッグ中なら位置をマウスに追従させる
//                if (ui && ui->IsPressed()) {
//                    uiManager->BringElementToFront(ui);
//                    ui->SetPosition(mouse.GetPositionX(), mouse.GetPositionY());
//                }
//            }
//        };
//
//        // 各フェーズのUI更新
//        updateGroup(spawnUI);
//        updateGroup(travelUI);
//        updateGroup(onHitUI);
//        updateGroup(endUI);
//    }
//
//    void CloseInventory(InventoryEvent &event)
//    {
//        if (!isOpened) return;
//
//        // UI要素のクリア
//        uiManager->RemoveUIGroup("Inventory");
//
//        auto gun = event.gunBuilder;
//
//        auto finalizeGroup =
//            [&](AttachmentUIGroup &group, std::vector<EffectID> &gunMods, int maxSlots) {
//                // 現在の状態（スロットかストレージか）をセーブ
//                if (group.elements.size() > 0) {
//                    gunMods = CollectGroupEffects(group, maxSlots);
//
//                    SaveStoragePositions(group);
//
//                    for (auto &element : group.elements) {
//                        if (element) {
//                            uiManager->RemoveElement(element);
//                        }
//                    }
//                }
//                group.elements.clear();
//                group.slotIndex.clear();
//                group.slotPositions.clear();
//                group.originalPositions.clear();
//            };
//
//        finalizeGroup(spawnUI, gun->spawnMods, gun->maxSpawnSlots);
//        finalizeGroup(travelUI, gun->travelMods, gun->maxTravelSlots);
//        finalizeGroup(onHitUI, gun->onHitMods, gun->maxOnHitSlots);
//        finalizeGroup(endUI, gun->endMods, gun->maxEndSlots);
//
//        // ホバー用テキストのクリア
//        textManager.RemoveGroupText("Hover");
//        uiManager->RemoveUIGroup("Store");
//
//        isOpened = false;
//    }
//
//  private:
//    // スロットの描画
//    void CreateSlots(int count, float baseX, float baseY, const std::string &prefix)
//    {
//        for (int i = 0; i < count; ++i) {
//            int row = i / maxPerRow;
//            int col = i % maxPerRow;
//
//            float x = baseX + col * slotW;
//            float y = baseY + row * slotH;
//
//            auto slot = std::make_shared<UIElement>(prefix + std::to_string(i), FILE_SLOT_SPRITE);
//
//            slot->SetPosition(x, y);
//            slot->SetSize(64.0f, 64.0f);
//            uiManager->AddElement(slot);
//            uiManager->AddUIGroup("Inventory", slot->GetName());
//        }
//    }
//
//    // 各グループのUI構築
//    void BuildAttachmentUI(const std::vector<EffectID> &equipped,
//        AttachmentUIGroup                              &group,
//        int                                             slotCount,
//        float                                           baseX,
//        float                                           baseY,
//        int                                             row,
//        DirectX::XMFLOAT4                               color)
//    {
//        group.elements.clear();
//        group.slotPositions.clear();
//        group.slotIndex.clear();
//        group.originalPositions.clear();
//
//        int index = 0;
//
//        // スロットの数だけ初期化
//        for (int i = 0; i < slotCount; ++i) {
//            group.slotPositions.push_back(GetSlotPos(i, baseX, baseY));
//        }
//
//        // 装備中スロットのUIを生成
//        for (int i = 0; i < equipped.size(); ++i) {
//            EffectID          element = equipped[i];
//            DirectX::XMFLOAT2 pos     = GetSlotPos(i, baseX, baseY);
//
//            std::shared_ptr<UIElement> uiElement = nullptr;
//
//            if (element != EffectID::None) {
//                int  currentIndex = index;
//                auto onRelease    = [this, &group, currentIndex]() {
//                    ResolveDropPosition(group, group.elements[currentIndex].get(), currentIndex);
//                };
//
//                uiElement = CreateUIFromEffect(element, index, onRelease);
//                uiElement->SetName(
//                    "Attachment_UI_" + std::to_string(row) + "_" + std::to_string(i));
//                uiElement->SetPosition(pos.x, pos.y);
//                uiElement->SetSize(64.0f, 64.0f);
//                uiElement->SetColor(color.x, color.y, color.z, color.w);
//                uiManager->AddElement(uiElement);
//                uiManager->AddUIGroup("Inventory", uiElement->GetName());
//            }
//
//            group.elements.push_back(uiElement);
//            group.slotIndex.push_back(element == EffectID::None ? -1 : i);
//            group.originalPositions.push_back(pos);
//
//            ++index;
//        }
//
//        // ストレージのUIを生成
//        for (int i = 0; i < group.storageEffects.size(); ++i) {
//            EffectID          effect = group.storageEffects[i];
//            DirectX::XMFLOAT2 pos    = group.storagePositions[i];
//
//            int  currentIndex = index;
//            auto onRelease    = [this, &group, currentIndex]() {
//                ResolveDropPosition(group, group.elements[currentIndex].get(), currentIndex);
//            };
//
//            auto uiElement = CreateUIFromEffect(effect, index, onRelease);
//
//            uiElement->SetPosition(pos.x, pos.y);
//            uiElement->SetSize(64.0f, 64.0f);
//            uiElement->SetColor(color.x, color.y, color.z, color.w);
//            uiManager->AddElement(uiElement);
//
//            group.elements.push_back(uiElement);
//            group.slotIndex.push_back(-1); // ストレージは -1
//            group.originalPositions.push_back(pos);
//
//            ++index;
//        }
//    }
//
//    // 各グループのエフェクトを収集
//    std::vector<EffectID> CollectGroupEffects(AttachmentUIGroup &group, int slotCount)
//    {
//        std::vector<EffectID> result(slotCount, EffectID::None);
//
//        for (int i = 0; i < static_cast<int>(group.elements.size()); ++i) {
//            auto ui = group.elements[i];
//            // 移動などで消えている場合はスキップ
//            if (!ui) continue;
//
//            int slot = group.slotIndex[i];
//
//            if (slot >= 0 && slot < slotCount) {
//                auto btn     = static_cast<AttachmentButton *>(ui.get());
//                result[slot] = btn->effect;
//            }
//        }
//        return result;
//    }
//
//  private:
//    // ストレージ内の位置を保存
//    void SaveStoragePositions(AttachmentUIGroup &group)
//    {
//        group.storageEffects.clear();
//        group.storagePositions.clear();
//
//        for (int i = 0; i < group.elements.size(); ++i) {
//            if (!group.elements[i]) continue;
//
//            if (group.slotIndex[i] == -1) {
//                auto btn = static_cast<AttachmentButton *>(group.elements[i].get());
//
//                group.storageEffects.push_back(btn->effect);
//                group.storagePositions.push_back(group.elements[i]->GetPosition());
//            }
//        }
//    }
//
//    // --- グループの色を取得するヘルパー ---
//    DirectX::XMFLOAT4 GetGroupColor(AttachmentUIGroup &group)
//    {
//        if (&group == &spawnUI) return {0, 1, 0, 1};
//        if (&group == &travelUI) return {0, 0, 1, 1};
//        if (&group == &onHitUI) return {1, 0, 0, 1};
//        if (&group == &endUI) return {1, 0, 1, 1};
//        // デフォルト
//        return {1, 1, 1, 1};
//    }
//
//    // --- ★修正: ドロップ位置の処理（全グループ対応版） ---
//    void ResolveDropPosition(AttachmentUIGroup &group, UIElement *uiElement, int index)
//    {
//        // 既に移動などで無効になっている場合は無視
//        if (!group.elements[index]) return;
//
//        auto center = uiElement->GetPosition();
//
//        // 1. ストレージへのドロップ判定
//        if (storageSlot->HitTest(center.x, center.y)) {
//            // ストレージエリアなら、現在のグループに維持しつつスロットから外す
//            group.slotIndex[index] = -1;
//            uiElement->MoveTo(center.x, center.y);
//            group.originalPositions[index] = center;
//            return;
//        }
//
//        // 2. 全スロットグループへのドロップ判定
//        // チェック対象の全グループ
//        AttachmentUIGroup *targetGroups[] = {&spawnUI, &travelUI, &onHitUI, &endUI};
//
//        for (auto *targetGroup : targetGroups) {
//            // 各スロットとの当たり判定
//            for (int slot = 0; slot < targetGroup->slotPositions.size(); ++slot) {
//                if (HitSlot(*targetGroup, slot, center)) {
//
//                    // スロットにヒット！
//                    // そのスロットに既にアイテムがあるか確認
//                    int targetElementIndex = FindAttachmentInSlot(*targetGroup, slot);
//
//                    // --- ケースA: 空きスロットへ移動 ---
//                    if (targetElementIndex == -1) {
//                        MoveAttachment(
//                            group, index, *targetGroup, slot, targetGroup->slotPositions[slot]);
//                        return;
//                    }
//                    // --- ケースB: 既存アイテムと交換 (Swap) ---
//                    else {
//                        // 自分自身の上にドロップした場合は元の位置に戻す
//                        if (targetGroup == &group && targetElementIndex == index) {
//                            auto slotCenter = group.slotPositions[slot];
//                            uiElement->MoveTo(slotCenter.x, slotCenter.y);
//                            group.originalPositions[index] = slotCenter;
//                            return;
//                        }
//
//                        // 交換処理
//                        SwapAttachment(group, index, *targetGroup, targetElementIndex);
//                        return;
//                    }
//                }
//            }
//        }
//
//        // 3. どこにもヒットしなければ元の場所へ戻る
//        auto pos = group.originalPositions[index];
//        uiElement->MoveTo(pos.x, pos.y);
//    }
//
//    // --- ★追加: アタッチメントの移動処理 ---
//    // 元のグループから削除し、新しいグループに作り直して追加する
//    void MoveAttachment(AttachmentUIGroup &srcGroup,
//        int                                srcIndex,
//        AttachmentUIGroup                 &dstGroup,
//        int                                dstSlotIndex,
//        DirectX::XMFLOAT2                  pos)
//    {
//        auto     oldBtn = std::static_pointer_cast<AttachmentButton>(srcGroup.elements[srcIndex]);
//        EffectID effect = oldBtn->effect;
//        std::string str = oldBtn->str; // 説明文も引き継ぐ
//
//        // 1. 新しいUIの作成
//        int  newIndex  = (int)dstGroup.elements.size();
//        auto onRelease = [this, &dstGroup, newIndex]() {
//            ResolveDropPosition(dstGroup, dstGroup.elements[newIndex].get(), newIndex);
//        };
//
//        // CreateUIFromEffectを使って適切な画像・テキストで再生成
//        // (CreateUIFromEffectは内部でAttachmentButtonを作る)
//        // ※ここでは effectID だけ渡して再度画像パスなどを解決させる
//        auto newUI = CreateUIFromEffect(effect, newIndex, onRelease);
//
//        // パラメータ設定
//        newUI->SetPosition(pos.x, pos.y);
//        newUI->SetSize(64.0f, 64.0f);
//
//        // 色は移動先のグループに合わせる
//        auto col = GetGroupColor(dstGroup);
//        newUI->SetColor(col.x, col.y, col.z, col.w);
//
//        // 管理下に追加
//        uiManager->AddElement(newUI);
//        // 名前もユニークにしておく（CreateUIFromEffectでsuffixがつくが、念のためUIManager登録済み）
//        // グループ登録
//        uiManager->AddUIGroup("Inventory", newUI->GetName());
//
//        // 2. 移動先グループへデータ追加
//        dstGroup.elements.push_back(newUI);
//        dstGroup.slotIndex.push_back(dstSlotIndex);
//        dstGroup.originalPositions.push_back(pos);
//        // ストレージ情報は空で埋めておく（スロット用なので）
//        // 必要なら storageEffects などもリサイズが必要だが、
//        // storageEffects は storageItems の個数分しか使わないロジックになっているので無視してOK。
//        // ただし `CollectGroupEffects` や `SaveStoragePositions` で `elements`
//        // とサイズがずれると困るかも？
//        // -> `SaveStoragePositions` は `elements` ループで `slotIndex == -1` を探すので大丈夫。
//
//        // 3. 元のUIの削除
//        // ベクタから削除するとインデックスがずれて他のコールバックが壊れるため、
//        // ポインタを無効化(nullptr)する方式をとる（CollectGroupEffectsでnullptrチェック済み）
//        uiManager->RemoveElement(srcGroup.elements[srcIndex]);
//        srcGroup.elements[srcIndex]  = nullptr;
//        srcGroup.slotIndex[srcIndex] = -2; // 無効値
//    }
//
//    // --- ★追加: アタッチメントの交換処理 ---
//    void SwapAttachment(
//        AttachmentUIGroup &groupA, int indexA, AttachmentUIGroup &groupB, int indexB)
//    {
//        // それぞれの現在のスロット位置（またはストレージ位置）を取得
//        int slotA = groupA.slotIndex[indexA];
//        int slotB = groupB.slotIndex[indexB]; // ターゲットのスロット
//
//        // 位置情報の取得（スワップ先の座標）
//        // slotB が -1 (ストレージ) の場合もあるので注意
//        DirectX::XMFLOAT2 posForA; // Aがこれから行く場所 (Bの場所)
//        DirectX::XMFLOAT2 posForB; // Bがこれから行く場所 (Aの場所)
//
//        // Bの場所を取得
//        if (slotB == -1)
//            posForA = groupB.elements[indexB]->GetPosition(); // ストレージなら現在の見た目の位置
//        else
//            posForA = groupB.slotPositions[slotB];
//
//        // Aの場所を取得
//        if (slotA == -1)
//            posForB = groupA.elements[indexA]->GetPosition();
//        else
//            posForB = groupA.slotPositions[slotA];
//
//        // AをBのグループ/スロットへ移動
//        MoveAttachment(groupA, indexA, groupB, slotB, posForA);
//
//        // BをAのグループ/スロットへ移動
//        // ★注意: MoveAttachmentで groupB.elements[indexB] は nullptr にはならないが、
//        // 新しい要素が push_back される。
//        // しかし indexB は「交換前の古いB」を指している。
//        // MoveAttachment内で 古いB は nullptr にされる。
//        // なので、ここで呼べば正しく「古いB」を「Aの場所」へ移動できる。
//        MoveAttachment(groupB, indexB, groupA, slotA, posForB);
//    }
//
//    std::shared_ptr<UIElement> CreateUIFromEffect(
//        EffectID element, int index, std::function<void(void)> func)
//    {
//        auto add_ui = [&](EffectID           element,
//                          const std::string &suffix,
//                          const std::string &filename,
//                          const std::string &str) {
//            return std::make_shared<AttachmentButton>("UI_" + suffix + std::to_string(index),
//                path + filename,
//                element,
//                textManager,
//                str,
//                func);
//        };
//
//        switch (element) {
//        case EffectID::Chaos:
//            return add_ui(EffectID::Chaos, "chaos", "chaos.png", u8"カオスアタッチメント：\n魔法弾を無秩序状態にする");
//            break;
//        case EffectID::Split:
//            return add_ui(EffectID::Split, "division", "division.png", u8"分裂アタッチメント：\n魔法弾を分裂させる");
//            break;
//        case EffectID::Pierce:
//            return add_ui(
//                EffectID::Pierce, "penetration", "penetration.png", u8"貫通アタッチメント：\n魔法弾に貫通効果を与える");
//            break;
//        case EffectID::Accelerate:
//            return add_ui(EffectID::Accelerate, "speed", "speed.png", u8"加速アタッチメント：\n魔法弾を加速させる");
//            break;
//        case EffectID::Explosion:
//            return add_ui(
//                EffectID::Explosion, "explosion", "explosion.png", u8"爆発アタッチメント：\n魔法弾を爆発させる");
//            break;
//        case EffectID::Slow:
//            return add_ui(
//                EffectID::Slow, "deceleration", "deceleration.png", u8"減速アタッチメント：\n魔法弾を減速させる");
//            break;
//        case EffectID::Reflection:
//            return add_ui(
//                EffectID::Reflection, "reflection", "reflection.png", u8"反射アタッチメント：\n魔法弾に反射効果を与える");
//            break;
//        default: return nullptr;
//        }
//    }
//
//    // スロットにヒットしているか判定
//    bool HitSlot(AttachmentUIGroup &group, int slotIndex, DirectX::XMFLOAT2 point)
//    {
//        float x = group.slotPositions[slotIndex].x;
//        float y = group.slotPositions[slotIndex].y;
//
//        return (point.x >= x - 32 && point.x <= x + 32 && point.y >= y - 32 && point.y <= y + 32);
//    }
//
//    // スロット内のアタッチメントを探す
//    int FindAttachmentInSlot(AttachmentUIGroup &group, int slot)
//    {
//        for (int i = 0; i < group.slotIndex.size(); ++i) {
//            // 要素が存在し、かつスロット番号が一致するもの
//            if (group.elements[i] && group.slotIndex[i] == slot) {
//                return i;
//            }
//        }
//        return -1;
//    }
//
//    // ストレージ内のアタッチメントを探す
//    int FindAttachmentInStorage(AttachmentUIGroup &group, DirectX::XMFLOAT2 point)
//    {
//        for (int i = 0; i < group.elements.size(); ++i) {
//            if (!group.elements[i]) continue;
//            if (group.slotIndex[i] != -1) continue;
//
//            auto pos = group.elements[i]->GetPosition();
//
//            if (point.x >= pos.x - 32 && point.x <= pos.x + 32 && point.y >= pos.y - 32 &&
//                point.y <= pos.y + 32) {
//                return i;
//            }
//        }
//        return -1;
//    }
//
//    // スロット位置を計算する共通関数
//    DirectX::XMFLOAT2 GetSlotPos(int index, float baseX, float baseY)
//    {
//        int row = index / maxPerRow;
//        int col = index % maxPerRow;
//
//        return {baseX + col * slotW, baseY + row * slotH};
//    }
//};