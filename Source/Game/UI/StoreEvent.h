//#include <vector>
//#include <string>
//#include <unordered_map>
//
//
//#include "Engine/UI/UIManager.h"
//
//#include "InventoryEvent.h"
//
//#include "Engine/GamePlay/Core/Scene/SceneManager.h"
//
//#include "Engine/Graphics/Renderer/ModelRenderer.h"
//#include "Engine/Graphics/Renderer/Sprite.h"
//
//#include "Engine/UI/Text/FNT.h"
//
////#define FONT_SPRITE_PATH "Data/Font/font.png"
////#define FONT_SPRITE_PATH "Data/Font/font_0.png", "Data/Font/font.fnt"
//
//struct StoreEvent {};
//
//class StoreSystem
//{
//  private:
//    UIManager       *uiManager;
//    InventorySystem *inventory;
//
//    TextManager &textManager;
//
//    bool isOpened = false;
//
//    AttachmentUIGroup store_spawn_ui;
//    AttachmentUIGroup store_travel_ui;
//    AttachmentUIGroup store_onHit_ui;
//    AttachmentUIGroup store_end_ui;
//
//    std::shared_ptr<UIElement> storageSlot;
//
//    std::string path = "Assets/Textures/attachment/";
//
//    int maxValue = 300;
//    int minValue = 100;
//    int stepValue = 10;
//
//    //std::shared_ptr<FNT> valueSprite;
//    //std::unordered_map<int, std::shared_ptr<Sprite>> spawnValueSprites;
//    //std::unordered_map<int, int>                     spawnValues;
//    //std::unordered_map<int, std::shared_ptr<Sprite>> travelValueSprites;
//    //std::unordered_map<int, int>                     travelValues;
//    //std::unordered_map<int, std::shared_ptr<Sprite>> onHitValueSprites;
//    //std::unordered_map<int, int>                     onHitValues;
//    //std::unordered_map<int, std::shared_ptr<Sprite>> endValueSprites;
//    //std::unordered_map<int, int>                     endValues;
//public:
//    StoreSystem(UIManager *uiManager, TextManager &textManager, InventorySystem *inventory)
//      : uiManager(uiManager), textManager(textManager), inventory(inventory)
//    {
//        //valueSprite = std::make_shared<FNT>(Graphics::Instance().GetDevice(), "Data/Font/font.png", "Data/Font/font.fnt");
//
//        uiManager->LoadSprite("UI_storageSlot", FILE_STORAGE_SLOT_SPRITE);
//
//        //spawnValueSprites.clear();
//        //travelValueSprites.clear();
//        //onHitValueSprites.clear();
//        //endValueSprites.clear();
//
//        // 各グループのストレージ位置クリア
//        store_spawn_ui.storagePositions.clear();
//        store_spawn_ui.storageEffects.clear();
//        store_travel_ui.storagePositions.clear();
//        store_travel_ui.storageEffects.clear();
//        store_onHit_ui.storagePositions.clear();
//        store_onHit_ui.storageEffects.clear();
//        store_end_ui.storagePositions.clear();
//        store_end_ui.storageEffects.clear();
//
//        GenerateRandomAttachments(store_spawn_ui);
//        GenerateRandomAttachments(store_travel_ui);
//        GenerateRandomAttachments(store_onHit_ui);
//        GenerateRandomAttachments(store_end_ui);
//    }
//
//    void Clear()
//    {
//        isOpened = false;
//
//        // UI グループのクリア
//        auto clearGroup = [&](AttachmentUIGroup &g) {
//            g.elements.clear();
//            g.slotIndex.clear();
//            g.slotPositions.clear();
//            g.originalPositions.clear();
//            g.storageEffects.clear();
//            g.storagePositions.clear();
//            g.storageValues.clear();
//        };
//
//        clearGroup(store_spawn_ui);
//        clearGroup(store_travel_ui);
//        clearGroup(store_onHit_ui);
//        clearGroup(store_end_ui);
//    }
//
//    void Open(StoreEvent &event)
//    {
//        if (isOpened) return;
//        isOpened = true;
//
//        BuildStoreUI(store_spawn_ui, inventory->GetSpawnAttachmentUIGroup(), 0, {0, 1, 0, 1});
//        BuildStoreUI(store_travel_ui, inventory->GetTravelAttachmentUIGroup(), 1, {0, 0, 1, 1});
//        BuildStoreUI(store_onHit_ui, inventory->GetOnHitAttachmentUIGroup(), 2, {1, 0, 0, 1});
//        BuildStoreUI(store_end_ui, inventory->GetEndAttachmentUIGroup(), 3, {1, 0, 1, 1});
//
//        // ストレージスロットの描画
//        storageSlot = std::make_shared<UIElement>("UI_storageSlot", FILE_STORAGE_SLOT_SPRITE);
//        // 一番下に配置
//        float storageX = Graphics::Instance().GetScreenWidth() / 2.0f;
//        float storageY = Graphics::Instance().GetScreenHeight() * 3.0f / 4.0f;
//        storageSlot->SetPosition(storageX, storageY);
//        storageSlot->SetSize(1024.0f, 256.0f);
//        uiManager->AddElementToBack(storageSlot);
//        uiManager->AddUIGroup("Store", storageSlot->GetName());
//    }
//
//    void Update(StoreEvent &event)
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
//        updateGroup(store_spawn_ui);
//        updateGroup(store_travel_ui);
//        updateGroup(store_onHit_ui);
//        updateGroup(store_end_ui);
//    }
//
//    void Close(StoreEvent &event)
//    {
//        if (!isOpened) return;
//
//        textManager.RemoveGroupText("Hover");
//        
//        auto finalizeStoreGroup = [&](AttachmentUIGroup &group) {
//            // スロットが-2（購入済み）の時は削除
//            int i = 0;
//            while (i < (int)group.slotIndex.size()) {
//                if (group.slotIndex[i] == -2) {
//                    if (i < (int)group.storageEffects.size()) {
//                        group.storageEffects.erase(group.storageEffects.begin() + i);
//                    }
//                    if (i < (int)group.storageValues.size()) {
//                        group.storageValues.erase(group.storageValues.begin() + i);
//                    }
//                    group.slotIndex.erase(group.slotIndex.begin() + i);
//                }
//                else {
//                    ++i;
//                }
//            }
//
//            for (auto &element : group.elements) {
//                if (element) {
//                    uiManager->RemoveElement(element);
//                }
//            }
//            group.elements.clear();
//            group.slotIndex.clear();
//            group.slotPositions.clear();
//        };
//
//        finalizeStoreGroup(store_spawn_ui);
//        finalizeStoreGroup(store_travel_ui);
//        finalizeStoreGroup(store_onHit_ui);
//        finalizeStoreGroup(store_end_ui);
//
//        textManager.RemoveGroupText("Store");
//
//        uiManager->RemoveUIGroup("Store");
//        
//        isOpened = false;
//    }
//private:
//    void BuyItem(AttachmentUIGroup &storeGroup, int index, int row, std::string name, DirectX::XMFLOAT2 position)
//    {
//        auto element = storeGroup.elements[index];
//        if (!element) return;
//
//        auto btn = static_cast<AttachmentButton *>(element.get());
//
//        AttachmentUIGroup *targetGroup = nullptr;
//        switch (row) {
//        case 0: targetGroup = &inventory->GetSpawnAttachmentUIGroup(); break;
//        case 1: targetGroup = &inventory->GetTravelAttachmentUIGroup(); break;
//        case 2: targetGroup = &inventory->GetOnHitAttachmentUIGroup(); break;
//        case 3: targetGroup = &inventory->GetEndAttachmentUIGroup(); break;
//        }
//
//        textManager.RemoveText(element->GetName() + "_Value");
//        uiManager->RemoveElement(element);
//
//        if (targetGroup) {
//            inventory->AddAttachmentToStorage(*targetGroup, btn->effect, name, position);
//        }
//
//        //storeGroup.elements.erase(storeGroup.elements.begin() + index);
//        //storeGroup.slotIndex.erase(storeGroup.slotIndex.begin() + index);
//        storeGroup.slotIndex[index] = -2;
//
//        //if (index < storeGroup.storageEffects.size()) {
//        //    storeGroup.storageEffects.erase(storeGroup.storageEffects.begin() + index);
//        //}
//        //if (index < storeGroup.storageValues.size()) { // 【重要】値段も一緒に消す
//        //    storeGroup.storageValues.erase(storeGroup.storageValues.begin() + index);
//        //}
//    }
//
//    void BuildStoreUI(AttachmentUIGroup &group,
//        AttachmentUIGroup               &inventoryGroup,
//        int                              row,
//        const DirectX::XMFLOAT4         &color
//    )
//    {
//        const int   columns  = 5;
//        const float spacingX = 80.0f;
//        const float spacingY = 80.0f;
//
//        float baseX = Graphics::Instance().GetScreenWidth() / 2.0f - (columns * spacingX) / 2.0f;
//        float baseY = 200.0f + row * 80.0f;
//
//        group.slotPositions.clear();
//        // group.slotIndex.clear();
//        group.elements.clear();
//        // group.originalPositions.clear();
//
//        for (int i = 0; i < group.storageEffects.size(); ++i) {
//            EffectID effect = group.storageEffects[i];
//
//            //if (group.slotIndex.size() > i) {
//            //    if (group.slotIndex[i] == -1) continue;
//            //}
//
//            // グリッド配置
//            int col = i % columns;
//            int r   = i / columns;
//
//            // スロット位置登録
//            DirectX::XMFLOAT2 pos = {baseX + col * spacingX, baseY + r * spacingY};
//            group.slotPositions.push_back(pos);
//
//            // UI index
//            int uiIndex = static_cast<int>(group.elements.size());
//
//            int currentPrice = group.storageValues[i];
//
//            // ドロップ処理
//            auto onRelease = [this, &group, &inventoryGroup, currentPrice, row, effect, uiIndex]() {
//                ResolveDropPosition(group,
//                    inventoryGroup,
//                    group.elements[uiIndex].get(),
//                    effect,
//                    uiIndex,
//                    currentPrice,
//                    row
//                );
//            };
//
//            // UI 生成
//            auto ui = CreateUIFromEffect(effect, uiIndex, onRelease);
//            ui->SetName("UI_" + std::to_string(row) + "_" + std::to_string(uiIndex));
//            ui->SetPosition(pos.x, pos.y);
//            ui->SetSize(64.0f, 64.0f);
//            ui->SetColor(color.x, color.y, color.z, color.w);
//            uiManager->AddElement(ui);
//            uiManager->AddUIGroup("Store", ui->GetName());
//
//            group.elements.push_back(ui);
//
//            // 元の位置登録
//            //if (group.originalPositions.size() <= i) {
//            //    group.slotIndex.push_back(0);
//            //    group.originalPositions.push_back(pos);
//            //}
//            //else {
//            //    ui->SetPosition(group.originalPositions[i].x, group.originalPositions[i].y);
//            //}
//            group.slotIndex.push_back(0);
//            group.originalPositions.push_back(pos);
//
//            // 価格テキストの生成
//            Text text{};
//            text.text        = std::to_string(currentPrice);
//            text.x           = group.slotPositions[i].x;
//            text.y           = group.slotPositions[i].y;
//            text.width       = 20.0f;
//            text.height      = 20.0f;
//            std::string name = group.elements[i]->GetName() + "_Value";
//            textManager.AddText(name, text);
//            textManager.AddGroupText("Store", name);
//        }
//    }
//
//    void GenerateRandomAttachments(AttachmentUIGroup &group)
//    {
//        // ランダム
//        int maxCount = 5;
//        int count    = 1 + (rand() % maxCount);
//
//        for (int i = 0; i < count; ++i) {
//
//            // ランダムに EffectID を選択
//            EffectID effect;
//            switch (rand() % 7) {
//            case 0: effect = EffectID::Chaos; break;
//            case 1: effect = EffectID::Split; break;
//            case 2: effect = EffectID::Pierce; break;
//            case 3: effect = EffectID::Accelerate; break;
//            case 4: effect = EffectID::Explosion; break;
//            case 5: effect = EffectID::Slow; break;
//            case 6: effect = EffectID::Reflection; break;
//            }
//
//            // ストレージに追加（UI はまだ作らない）
//            group.storageEffects.push_back(effect);
//
//            // 価格の決定
//            int value = minValue + (stepValue * (rand() % ((maxValue - minValue) / stepValue + 1)));
//            group.storageValues.push_back(value);
//        }
//    }
//
//    // ドロップ位置の処理
//    void ResolveDropPosition(AttachmentUIGroup &group,
//        AttachmentUIGroup                      &inventoryGroup,
//        UIElement                              *uiElement,
//        EffectID                                effect,
//        int                                     index,
//        int                                     value,
//        int                                     row
//    )
//    {
//        if (!group.elements[index]) return;
//
//        auto center = uiElement->GetPosition();
//
//        // 元の場所がストレージかどうか
//        bool isFromStorage = (group.slotIndex[index] == -1);
//
//        // ストレージにドロップ
//        if (storageSlot->HitTest(center.x, center.y)) {
//
//            auto& playerData = SceneManager::Instance().GetPlayerData();
//            if (playerData.money >= value)
//            {
//                if (!isFromStorage) {
//                    // ストレージ位置に移動
//                    playerData.money -= value;
//                    group.slotIndex[index] = -1;
//                    BuyItem(group, index, row, group.elements[index]->GetName(), center);
//                }
//
//                group.originalPositions[index] = center;
//
//                return;
//            }
//        }
//
//        // どこにもヒットしていない
//        auto pos = group.originalPositions[index];
//        uiElement->MoveTo(pos.x, pos.y);
//    }
//
//  private:
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
//              switch (element) {
//        case EffectID::Chaos: return add_ui(EffectID::Chaos, "chaos", "chaos.png",                     u8"魔法弾を無秩序状態にする"); break;
//        case EffectID::Split: return add_ui(EffectID::Split, "division", "division.png",               u8"魔法弾を分裂させる"); break;
//        case EffectID::Pierce: return add_ui(EffectID::Pierce, "penetration", "penetration.png",       u8"魔法弾に貫通効果を与える"); break;
//        case EffectID::Accelerate: return add_ui(EffectID::Accelerate, "speed", "speed.png",           u8"魔法弾を加速させる"); break;
//        case EffectID::Explosion: return add_ui(EffectID::Explosion, "explosion", "explosion.png",     u8"魔法弾を爆発させる"); break;
//        case EffectID::Slow: return add_ui(EffectID::Slow, "deceleration", "deceleration.png",         u8"魔法弾を減速させる"); break;
//        case EffectID::Reflection: return add_ui(EffectID::Reflection, "reflection", "reflection.png", u8"魔法弾に反射効果を与える"); break;
//        default: return nullptr;
//        }
//    
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
//            if (group.slotIndex[i] == slot && group.elements[i]) {
//                return i;
//            }
//        }
//        return -1;
//    }
//};