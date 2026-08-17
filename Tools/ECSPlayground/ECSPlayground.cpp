// ===========================================================================
// ECSPlayground.cpp
// ---------------------------------------------------------------------------
// CCL ECS だけを切り出して動かすコンソールアプリ。
//
// 【目的】
//   実ゲームでは起動直後にドローン100体の生成やアニメーション・物理が同時に
//   走るため、ECS の内部処理だけをデバッガで追うのが事実上不可能です。
//   このファイルは「エンティティが数体しか存在しない世界」を作り、
//   ECS のコードだけをステップ実行できるようにするための実験場です。
//
//   DirectX も Jolt も ImGui も一切リンクしていません。デバッガが止まる行は
//   100% ECS のコードです。
//
// 【使い方】
//   Build.bat でビルドし、Bin/ECSPlayground.exe を Visual Studio で開くか、
//   devenv Bin\ECSPlayground.exe で起動してブレークポイントを置いてください。
//   各実験の "★BP" と書かれた行が、最初に止めるべき場所です。
//
// 【注意】
//   本ファイルはエンジン本体ではなく学習用ツールなので、出力に printf を
//   使っています（エンジン本体では CCL_LOG_* を使う規約です）。
// ===========================================================================

#include "Engine/ECS/CCL_ECSContext.h"
#include "Engine/ECS/System/CCL_IfSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

// コンソールの文字コードを合わせるためだけに使用（ECS 本体とは無関係）
#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

using namespace CCL::ECS;


/// @brief コンソールを UTF-8 表示に切り替える。
///
/// このプロジェクトは /utf-8 でビルドしているため、ソース中の日本語文字列は
/// UTF-8 のバイト列として exe に埋め込まれます。ところが日本語版 Windows の
/// コンソール既定コードページは CP932(Shift-JIS) なので、何もしないと
/// printf した日本語がすべて文字化けします。
/// 起動直後に出力コードページを UTF-8 へ合わせることで解決します。
static void EnableUtf8Console()
{
#if defined(_WIN32)
    ::SetConsoleOutputCP(CP_UTF8);   // 表示側
    ::SetConsoleCP(CP_UTF8);         // 入力側（メニューの番号入力）
#endif
}


// ===========================================================================
// 実験用コンポーネント
// ---------------------------------------------------------------------------
// ECS のコンポーネントはただの構造体です。基底クラスも継承も要りません。
// TypeInfo<T>::ID() が型ごとに一意な TypeID を自動採番してくれます。
// ===========================================================================

struct Position { float x, y, z; };
struct Velocity { float x, y, z; };
struct Health   { int   hp;      };


// ===========================================================================
// 表示ヘルパー
// ===========================================================================

static void Line(const char* title)
{
    std::printf("\n===========================================================\n");
    std::printf(" %s\n", title);
    std::printf("===========================================================\n");
}

/// @brief World が今持っている全 Chunk の状態を一覧表示する
static void DumpChunks(World& world, const char* label)
{
    auto& chunks = world.GetChunkManager().GetChunks();
    std::printf("[%s] Chunk数 = %zu\n", label, chunks.size());

    for (size_t i = 0; i < chunks.size(); ++i)
    {
        Chunk* c = chunks[i].get();
        if (!c) { std::printf("  Chunk[%zu] (null)\n", i); continue; }

        std::printf("  Chunk[%zu] entity=%zu/%zu  stride=%zuB  型数=%zu  型ID={",
                    i, c->GetEntityCount(), c->GetCapacity(),
                    c->GetChunkSize(), c->GetArchetype().size());

        const Archetype& arch = c->GetArchetype();
        for (size_t t = 0; t < arch.size(); ++t)
            std::printf("%s%u", (t ? "," : ""), (unsigned)arch[t].id);

        std::printf("}\n");
    }
}

/// @brief EntityID を「index:世代」の形で表示する
static void PrintEntityID(const char* label, EntityID id)
{
    std::printf("  %s = index:%u / 世代:%u  (raw=0x%llX)\n",
                label,
                EntityHandle::GetIndex(id),
                EntityHandle::GetGeneration(id),
                (unsigned long long)id);
}

/// @brief 指定エンティティが今どの Chunk の何番目にいるかを表示する
static void PrintLocation(World& world, const char* label, EntityID id)
{
    size_t indexInChunk = 0;
    size_t chunkIdx = world.GetChunkManager().SearchEntityIn(id, &indexInChunk);

    if (chunkIdx == InvalidIndex) {
        std::printf("  %s : まだ Chunk に居ません（実体化前 or 破棄済み）\n", label);
        return;
    }
    std::printf("  %s : Chunk[%zu] の %zu 番目\n", label, chunkIdx, indexInChunk);
}


// ===========================================================================
// 実験1: Chunk のメモリレイアウト（SoA）を目で見る
// ---------------------------------------------------------------------------
// 学習ステップ2に対応。ECS を自分の言葉で説明できるかは、ほぼこの図を
// 描けるかで決まります。
// ===========================================================================

static void Experiment1_MemoryLayout()
{
    Line("実験1: Chunk のメモリレイアウト（SoA）");

    World world;

    // Archetype = 「このエンティティが持つ型の集合」。実データは持ちません。
    Archetype arch = ArchetypeHelper::Generate<Position, Velocity>();

    EntityID first = InvalidEntityID;
    for (int i = 0; i < 8; ++i)
    {
        EntityID e = world.Spawn(arch)
                          .Set(Position{ (float)i, 0.0f, 0.0f })
                          .Set(Velocity{ 1.0f, 0.0f, 0.0f });
        if (i == 0) first = e;
    }

    world.ScrutinyAndApply();   // ★BP ここを跨ぐと実体が Chunk に載る

    size_t indexInChunk = 0;
    size_t chunkIdx = world.GetChunkManager().SearchEntityIn(first, &indexInChunk);
    Chunk* c = world.GetChunkManager().GetChunks()[chunkIdx].get();

    // SoA の核心：型ごとに「連続した1本の配列」になっている
    Position* pos = c->GetComponentArray<Position>();   // ★BP ここで pos/vel の値を見る
    Velocity* vel = c->GetComponentArray<Velocity>();

    const long long gap = (long long)((uint8_t*)vel - (uint8_t*)pos);

    std::printf("Chunk[%zu] : 現在 %zu 体 / 容量 %zu 体。1体あたり %zu バイト。\n",
                chunkIdx, c->GetEntityCount(), c->GetCapacity(), c->GetChunkSize());
    std::printf("Position 配列の先頭 = %p\n", (void*)pos);
    std::printf("Velocity 配列の先頭 = %p\n", (void*)vel);
    std::printf("→ 2つの配列は %lld バイト離れている\n", gap);

    // ここが誤解しやすい最重要ポイント。
    // 各コンポーネント配列の幅は「今いる数」ではなく「Chunkの容量」で決まる。
    std::printf("\n【重要】この %lld は 8体分(%zuB)ではありません。\n",
                gap, 8 * sizeof(Position));
    std::printf("  容量 %zu 体 × sizeof(Position) %zu = %zu バイトを\n",
                c->GetCapacity(), sizeof(Position), c->GetCapacity() * sizeof(Position));
    std::printf("  16バイト境界に切り上げた値が %lld です。\n", gap);
    std::printf("  つまり Chunk は生成時に「容量いっぱい分」の領域を一括確保し、\n");
    std::printf("  その中に前から詰めていきます（容量は 16KB / stride で決まる）。\n");

    std::printf("\nPosition 配列の中身（アドレスが sizeof(Position)=%zu ずつ増える）:\n",
                sizeof(Position));
    for (size_t i = 0; i < c->GetEntityCount(); ++i)
        std::printf("  [%zu] %p  x=%.1f\n", i, (void*)&pos[i], pos[i].x);

    std::printf("\n※ AoS（普通のクラス配列）なら Position と Velocity が交互に並びます。\n");
    std::printf("   SoA では Position だけを舐めるループがキャッシュを無駄にしません。\n");
}


// ===========================================================================
// 実験2: 構造変更はなぜ遅延するのか
// ---------------------------------------------------------------------------
// 学習ステップ3の前半。ECS の API があの形をしている理由そのものです。
// ===========================================================================

static void Experiment2_DeferredApply()
{
    Line("実験2: 構造変更の遅延（Spawn してもすぐには実体化しない）");

    World world;
    Archetype arch = ArchetypeHelper::Generate<Position>();

    EntityID e = world.Spawn(arch).Set(Position{ 1.0f, 2.0f, 3.0f });

    std::printf("Spawn 直後:\n");
    PrintEntityID("EntityID", e);
    std::printf("  IsEntityValid = %s （ID はもう有効）\n",
                world.IsEntityValid(e) ? "true" : "false");
    PrintLocation(world, "居場所", e);
    std::printf("  GetComponent<Position> = %p （まだ nullptr のはず）\n",
                (void*)world.GetComponent<Position>(e));
    DumpChunks(world, "ScrutinyAndApply 前");

    world.ScrutinyAndApply();   // ★BP ここに入ってステップ実行する

    std::printf("\nScrutinyAndApply 後:\n");
    PrintLocation(world, "居場所", e);
    Position* p = world.GetComponent<Position>(e);
    std::printf("  GetComponent<Position> = %p  値=(%.1f, %.1f, %.1f)\n",
                (void*)p, p->x, p->y, p->z);
    DumpChunks(world, "ScrutinyAndApply 後");

    std::printf("\n※ なぜ即座にやらないか: System が ForEach でループを回している最中に\n");
    std::printf("   Chunk のメモリが動くと、ループが握っているポインタが無効になるから。\n");
    std::printf("   だから構造変更は必ずフレームの境界（ここ）でまとめて適用されます。\n");
}


// ===========================================================================
// 実験3: 破棄・穴詰め・世代によるID再利用
// ---------------------------------------------------------------------------
// 学習ステップ3の後半。ダングリングIDを防ぐ仕組みを見ます。
// ===========================================================================

static void Experiment3_DestroyAndGeneration()
{
    Line("実験3: 破棄と世代カウンタによる ID 再利用");

    World world;
    Archetype arch = ArchetypeHelper::Generate<Position>();

    EntityID ids[5];
    for (int i = 0; i < 5; ++i)
        ids[i] = world.Spawn(arch).Set(Position{ (float)i, 0.0f, 0.0f });
    world.ScrutinyAndApply();

    std::printf("5体生成後:\n");
    for (int i = 0; i < 5; ++i) PrintLocation(world, "entity", ids[i]);

    // 真ん中（index 2）を破棄する。
    //
    // ※ world.Destroy() ではなく RequestDestroyEntity() を使っている理由:
    //    Destroy() は DestroyTag を付けるだけで、実際の破棄は
    //    HierarchyCleanupSystem（Engine/Transform 側にあり、ECSコアには含まれない）
    //    が行います。この Playground にはそのSystemが居ないので、
    //    フレームワーク内部用の RequestDestroyEntity() を直接呼びます。
    std::printf("\nids[2] を破棄:\n");
    PrintEntityID("破棄する ID", ids[2]);
    // PendigOpsでDestroyを予約するだけ
    world.RequestDestroyEntity(ids[2]);

    world.ScrutinyAndApply();   // ★BP CompactHoles が穴を前詰めする瞬間

    std::printf("\n破棄＋穴詰め後:\n");
    for (int i = 0; i < 5; ++i)
    {
        std::printf("  ids[%d] valid=%s ", i, world.IsEntityValid(ids[i]) ? "true " : "false");
        PrintLocation(world, "", ids[i]);
    }
    std::printf("→ 最後尾に居た entity が、空いた穴の位置へ移動しています（前詰め）。\n");

    // 破棄された index は世代を +1 して再利用される
    EntityID reused = world.Spawn(arch).Set(Position{ 99.0f, 0.0f, 0.0f });
    world.ScrutinyAndApply();

    std::printf("\n新しく1体 Spawn すると:\n");
    PrintEntityID("破棄した古いID", ids[2]);
    PrintEntityID("新しいID      ", reused);
    std::printf("  index は同じ、世代だけ違う → 古い ID は自動的に無効になる\n");
    std::printf("  古いID の IsEntityValid = %s\n",
                world.IsEntityValid(ids[2]) ? "true" : "false");
    std::printf("  新しいID の IsEntityValid = %s\n",
                world.IsEntityValid(reused) ? "true" : "false");
}


// ===========================================================================
// 実験4: アーキタイプ遷移（コンポーネントを足すと Chunk を引っ越す）
// ===========================================================================

static void Experiment4_ArchetypeTransition()
{
    Line("実験4: アーキタイプ遷移（Chunk 間の引っ越し）");

    World world;
    Archetype arch = ArchetypeHelper::Generate<Position, Velocity>();

    EntityID e = world.Spawn(arch)
                      .Set(Position{ 7.0f, 8.0f, 9.0f })
                      .Set(Velocity{ 1.0f, 0.0f, 0.0f });
    world.ScrutinyAndApply();

    std::printf("Health を足す前:\n");
    PrintLocation(world, "居場所", e);
    DumpChunks(world, "追加前");

    // Health を後付けする → 型構成が変わるので別 Chunk へ移動が必要になる
    world.AddComponent<Health>(e, Health{ 100 });

    world.ScrutinyAndApply();   // ★BP MoveEntityBetweenChunks が走る

    std::printf("\nHealth を足した後:\n");
    PrintLocation(world, "居場所", e);
    DumpChunks(world, "追加後");

    Position* p = world.GetComponent<Position>(e);
    Health*   h = world.GetComponent<Health>(e);
    std::printf("  Position は引っ越し後も保持 = (%.1f, %.1f, %.1f)\n", p->x, p->y, p->z);
    std::printf("  Health = %d\n", h->hp);
    std::printf("\n※ 元の Chunk は空になっても解放されません（既知の弱点 7 番）。\n");
}


// ===========================================================================
// 実験5: System を1本動かす（ForEach の中身を見る）
// ---------------------------------------------------------------------------
// 学習ステップ4に対応。ECSContext を使い、実ゲームと同じ
// 「ScrutinyAndApply → ResolveTargetChunks → UpdateLogic」の3段構えを回します。
// ===========================================================================

class PlaygroundMoveSystem
    : public IfSystem<PlaygroundMoveSystem, Write<Position>, Read<Velocity>>
{
public:
    PlaygroundMoveSystem() : IfSystem("PlaygroundMoveSystem") {}

    void Update(float dt) override
    {
        // Read<T> は const&、Write<T> は & で受け取る（型レベルで強制される）
        ForEach([dt](Position& p, const Velocity& v) {   // ★BP ここに入る
            p.x += v.x * dt;
            p.y += v.y * dt;
            p.z += v.z * dt;
        });
    }
};

static void Experiment5_System()
{
    Line("実験5: System と ForEach");

    // JobSystem は実ゲームでは EngineServices が持つ共有プール。
    // ここでは Playground が自前で1つ作って注入する。
    JobSystem  jobSystem;
    ECSContext ctx(&jobSystem);

    World* world = ctx.GetWorld();

    // 通常は .cpp 末尾の REGISTER_LOGIC_SYSTEM マクロで自動登録しますが、
    // ここでは登録の中身を見せるため意図的に直接呼んでいます。
    ctx.GetSystemManager()->RegisterSystem<PlaygroundMoveSystem>(
        Priority::LogicStage::L02_Update);

    Archetype moving = ArchetypeHelper::Generate<Position, Velocity>();
    Archetype still  = ArchetypeHelper::Generate<Position>();

    EntityID a = world->Spawn(moving)
                       .Set(Position{ 0.0f, 0.0f, 0.0f })
                       .Set(Velocity{ 10.0f, 0.0f, 0.0f });
    EntityID b = world->Spawn(moving)
                       .Set(Position{ 0.0f, 0.0f, 0.0f })
                       .Set(Velocity{ 0.0f, -5.0f, 0.0f });
    // Velocity を持たない = System のフィルタに合致しないので動かない
    EntityID c = world->Spawn(still).Set(Position{ 100.0f, 100.0f, 100.0f });

    const float dt = 0.5f;
    for (int frame = 0; frame < 3; ++frame)
    {
        ctx.UpdateLogic(dt);   // ★BP この中の3ステップを追う

        Position* pa = world->GetComponent<Position>(a);
        Position* pb = world->GetComponent<Position>(b);
        Position* pc = world->GetComponent<Position>(c);

        std::printf("frame %d : a=(%.1f, %.1f)  b=(%.1f, %.1f)  c=(%.1f, %.1f)\n",
                    frame, pa->x, pa->y, pb->x, pb->y, pc->x, pc->y);
    }

    std::printf("\n※ c は Velocity を持たないため ResolveTargetChunks の段階で\n");
    std::printf("   System の担当 Chunk から外れ、ForEach に一度も渡っていません。\n");
}


// ===========================================================================
// ベンチマーク用の共通部品
// ===========================================================================

// 最適化でループごと消されるのを防ぐための捨て先。
// 計算結果をここに書いておくと、コンパイラは「使われている」と判断して
// ループを削除できなくなる。
static volatile float g_sink = 0.0f;

/// @brief f を trials 回まわして、最速のミリ秒を返す。
///
/// 平均ではなく「最速」を採るのが計測の定石。
/// 遅い回はOSのスレッド切り替えや他プロセスの影響を含んでいるだけで、
/// 測りたいコードの実力ではないため。
template <typename F>
static double BenchBestMs(int trials, F&& f)
{
    double best = 1e18;
    for (int t = 0; t < trials; ++t)
    {
        const auto s = std::chrono::steady_clock::now();
        f();
        const auto e = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(e - s).count();
        best = (std::min)(best, ms);
    }
    return best;
}


// ===========================================================================
// 実験6: AoS と SoA の速度を実測する
// ---------------------------------------------------------------------------
// 面接で「ECSは速い」と言うなら、この数字を持っておきたい。
//
// 【測り方の考え方】
//   同じ「Position.x を全部足す」という処理を、
//     AoS : 1体分のデータがひとかたまりに並んだ配列
//     SoA : Position だけが連続して並んだ ECS の Chunk
//   の2通りで実行して時間を比べる。
//
//   ★AoS が不利になるのは「使わないデータまで巻き込んで読む」から。
//     なので 1体あたりのサイズが大きいほど差が開く。
//     それを見せるために「細い構成」と「太い構成」の2種類を測る。
// ===========================================================================

// --- 細い構成: 24バイト（Position + Velocity）---
struct NarrowAoS
{
    Position pos;
    Velocity vel;
};

// --- 太い構成: 152バイト（実際のゲームのキャラに近い量）---
struct Matrix4x4 { float m[16]; };   // 64B  行列
struct AIState   { int   a[8];  };   // 32B  AI の状態
struct RenderData{ int   b[8];  };   // 32B  描画情報

struct FatAoS
{
    Position   pos;   // 12B  ← 今回使うのはこれだけ
    Velocity   vel;   // 12B
    Matrix4x4  mat;   // 64B
    AIState    ai;    // 32B
    RenderData rd;    // 32B
};

/// @brief ECS 側に N 体作り、Position.x に連番を入れる
static void BuildSoAWorld(World& world, const Archetype& arch, int count)
{
    // Set を使わず素の Spawn にしているのは、
    // 10万件の PendingOp を積むと生成側が重くなり、
    // 「メモリ配置の比較」というこの実験の主旨がぼやけるため。
    for (int i = 0; i < count; ++i) world.RequestSpawnEntity(arch);
    world.ScrutinyAndApply();

    // 実体化後に Chunk の配列へ直接書き込む
    float v = 0.0f;
    for (auto& up : world.GetChunkManager().GetChunks())
    {
        Chunk* c = up.get();
        if (!c) continue;
        Position* p = c->GetComponentArray<Position>();
        if (!p) continue;
        const size_t n = c->GetEntityCount();
        for (size_t i = 0; i < n; ++i) { p[i].x = v; v += 1.0f; }
    }
}

/// @brief ECS の Chunk を舐めて Position.x を合計する（SoA 側の計測対象）
static float SumSoA(World& world)
{
    float sum = 0.0f;
    for (auto& up : world.GetChunkManager().GetChunks())
    {
        Chunk* c = up.get();
        if (!c) continue;
        Position* p = c->GetComponentArray<Position>();   // ループの外で1回だけ取る
        if (!p) continue;
        const size_t n = c->GetEntityCount();
        for (size_t i = 0; i < n; ++i) sum += p[i].x;     // 12バイト刻みの連続アクセス
    }
    return sum;
}

template <typename AoSType>
static void RunOneLayoutCase(const char* label, int count, size_t usefulBytes, int trials)
{
    // ---- AoS 側の準備 ----
    std::vector<AoSType> aos(count);
    for (int i = 0; i < count; ++i) aos[i].pos.x = (float)i;

    // ---- SoA 側の準備 ----
    World world;
    Archetype arch;
    if (sizeof(AoSType) == sizeof(NarrowAoS))
        arch = ArchetypeHelper::Generate<Position, Velocity>();
    else
        arch = ArchetypeHelper::Generate<Position, Velocity, Matrix4x4, AIState, RenderData>();
    BuildSoAWorld(world, arch, count);

    const double aosMs = BenchBestMs(trials, [&]() {
        float sum = 0.0f;
        for (int i = 0; i < count; ++i) sum += aos[i].pos.x;   // stride = sizeof(AoSType)
        g_sink = sum;
    });

    const double soaMs = BenchBestMs(trials, [&]() {
        g_sink = SumSoA(world);
    });

    const size_t aosTouched = (size_t)count * sizeof(AoSType);
    const size_t soaTouched = (size_t)count * usefulBytes;

    std::printf("\n--- %s ---\n", label);
    std::printf("  1体 %zu バイト / %d 体\n", sizeof(AoSType), count);
    std::printf("  AoS : %8.3f ms   読み込む量 %7.1f MB\n",
                aosMs, aosTouched / (1024.0 * 1024.0));
    std::printf("  SoA : %8.3f ms   読み込む量 %7.1f MB\n",
                soaMs, soaTouched / (1024.0 * 1024.0));
    std::printf("  → SoA が %.2f 倍速い（読み込む量の差は %.1f 倍）\n",
                aosMs / soaMs, (double)aosTouched / (double)soaTouched);
}

static void Experiment6_AoSvsSoA()
{
    Line("実験6: AoS と SoA の速度比較（実測）");

    std::printf("「Position.x を全部足す」処理を、AoS（1体ごとにまとめて配置）と\n");
    std::printf("SoA（型ごとに連続配置）で実行して時間を比べます。\n");
    std::printf("使うのは Position の 12 バイトだけ。残りは読む必要のないデータです。\n");
    std::printf("\n※データ生成に少し時間がかかります（数秒）。\n");

    // ---- ケース1: キャッシュに収まる小さいデータ ----
    //   ここでは差がほとんど出ない。それを先に見せるのが大事。
    RunOneLayoutCase<FatAoS>(
        "ケース1: 太い構成・少数（キャッシュに収まる）", 20000, sizeof(Position), 50);

    // ---- ケース2: キャッシュに収まらない大きいデータ ----
    RunOneLayoutCase<FatAoS>(
        "ケース2: 太い構成・大量（キャッシュに収まらない）", 300000, sizeof(Position), 10);

    // ---- ケース3: 1体が細い。AoS側の総量をケース2と揃えて公平に比較する ----
    //   152B × 30万 ≒ 24B × 200万。どちらもキャッシュに収まらない量なので、
    //   ここでの差は純粋に「1体あたりの無駄の量」だけを反映する。
    RunOneLayoutCase<NarrowAoS>(
        "ケース3: 細い構成・大量（総量はケース2と同等）", 2000000, sizeof(Position), 10);

    std::printf("\n===== 読み取り方 =====\n");
    std::printf("\n[前提] CPU はメモリを 64 バイト単位（キャッシュライン）で読みます。\n");
    std::printf("  AoS では Position の隣に使わないデータが並んでいるため、\n");
    std::printf("  それらも巻き込んで読み込まれ、その分が無駄になります。\n");

    std::printf("\n[ケース1で差が出ない理由]  ★ここが一番大事★\n");
    std::printf("  データが小さいと、AoS でも全部 CPU のキャッシュに載ってしまいます。\n");
    std::printf("  一度載ってしまえばメモリまで取りに行く必要がないので、\n");
    std::printf("  並び方の差が消えます。\n");
    std::printf("  → 「ECSは常に速い」は誤りです。データ量がキャッシュを超えて\n");
    std::printf("     初めて効いてきます。\n");

    std::printf("\n[ケース2で差が出る理由]\n");
    std::printf("  キャッシュに収まらないので、実際にメモリまで読みに行く回数が効きます。\n");
    std::printf("  AoS は使わないデータまで運ぶので、その分だけ遅くなります。\n");

    std::printf("\n[ケース2と3の差が、この実験の結論]\n");
    std::printf("  2と3は AoS 側が読む総量をほぼ同じに揃えてあります（どちらも約45MB）。\n");
    std::printf("  それでも倍率が違うのは、1体あたりの「無駄の割合」が違うからです。\n");
    std::printf("    ケース2: 152 バイト中 12 バイトしか使わない → 無駄だらけ → 大差\n");
    std::printf("    ケース3:  24 バイト中 12 バイトを使う      → 無駄は半分 → 小差\n");
    std::printf("  → 効果は「1体あたり、使わないデータがどれだけ多いか」で決まります。\n");
    std::printf("     コンポーネントの種類が多いゲームほど ECS が効く、ということです。\n");

    std::printf("\n[実測値が理論値ほど伸びない理由]\n");
    std::printf("  CPU には先読み（プリフェッチ）機能があり、連続アクセスなら\n");
    std::printf("  次に必要なデータを裏で先に取ってきてくれます。\n");
    std::printf("  AoS も「連続」ではあるので、この恩恵をある程度受けられます。\n");
}


// ===========================================================================
// 実験7: ForEach と ForEachParallel の比較
// ---------------------------------------------------------------------------
// 並列化がどれだけ効くかは「処理の重さ」で全く変わる、という話。
// ===========================================================================

class BenchMoveSystem
    : public IfSystem<BenchMoveSystem, Write<Position>, Read<Velocity>>
{
public:
    bool parallel = false;   ///< true なら ForEachParallel を使う
    bool heavy    = false;   ///< true なら1体あたりの計算量を増やす

    BenchMoveSystem() : IfSystem("BenchMoveSystem") {}

    void Update(float dt) override
    {
        if (heavy)
        {
            // CPU をしっかり使う処理（計算律速）
            auto f = [](Position& p, const Velocity& v) {
                float a = p.x;
                for (int k = 0; k < 24; ++k)
                    a = std::sqrt(a * a + v.x * v.x) * 0.999f + 0.001f;
                p.x = a;
            };
            if (parallel) ForEachParallel(f); else ForEach(f);
        }
        else
        {
            // ほとんど計算しない処理（メモリ律速）
            auto f = [dt](Position& p, const Velocity& v) {
                p.x += v.x * dt;
                p.y += v.y * dt;
                p.z += v.z * dt;
            };
            if (parallel) ForEachParallel(f); else ForEach(f);
        }
    }
};

static void Experiment7_ParallelScaling()
{
    Line("実験7: ForEach と ForEachParallel の比較");

    const int count  = 200000;
    const int trials = 10;
    const unsigned hw = std::thread::hardware_concurrency();

    std::printf("エンティティ数 %d 体 / 論理コア数 %u\n", count, hw);

    JobSystem  jobSystem;
    ECSContext ctx(&jobSystem);
    World* world = ctx.GetWorld();

    BenchMoveSystem* sys =
        ctx.GetSystemManager()->RegisterSystem<BenchMoveSystem>(
            Priority::LogicStage::L02_Update);

    Archetype arch = ArchetypeHelper::Generate<Position, Velocity>();
    for (int i = 0; i < count; ++i) world->Spawn(arch);

    // 1回まわして構造を確定させ、System に担当 Chunk を配る。
    // （これをやらないと _targetChunks が空のままで何も処理されない）
    ctx.UpdateLogic(0.016f);

    // Velocity に値を入れておく（全部 0 だと計算が退化するため）
    for (auto& up : world->GetChunkManager().GetChunks())
    {
        Chunk* c = up.get();
        if (!c) continue;
        Velocity* v = c->GetComponentArray<Velocity>();
        if (!v) continue;
        for (size_t i = 0; i < c->GetEntityCount(); ++i) v[i].x = 1.0f;
    }

    // ここからは System::Update を直接呼ぶ。
    // ScrutinyAndApply やスケジューラの時間を混ぜず、
    // ForEach 本体だけを測るため。
    auto measure = [&](bool par, bool hv) {
        sys->parallel = par;
        sys->heavy    = hv;
        return BenchBestMs(trials, [&]() { sys->Update(0.016f); });
    };

    std::printf("\n--- 軽い処理（p += v * dt だけ）---\n");
    const double lightSerial   = measure(false, false);
    const double lightParallel = measure(true,  false);
    const double lightGain     = lightSerial / lightParallel;
    std::printf("  ForEach         : %8.3f ms\n", lightSerial);
    std::printf("  ForEachParallel : %8.3f ms\n", lightParallel);
    std::printf("  → %.2f 倍（コア数 %u に対して効率 %.0f%%）\n",
                lightGain, hw, lightGain / hw * 100.0);

    std::printf("\n--- 重い処理（1体につき sqrt を24回）---\n");
    const double heavySerial   = measure(false, true);
    const double heavyParallel = measure(true,  true);
    const double heavyGain     = heavySerial / heavyParallel;
    std::printf("  ForEach         : %8.3f ms\n", heavySerial);
    std::printf("  ForEachParallel : %8.3f ms\n", heavyParallel);
    std::printf("  → %.2f 倍（コア数 %u に対して効率 %.0f%%）\n",
                heavyGain, hw, heavyGain / hw * 100.0);

    std::printf("\n===== 読み取り方 =====\n");
    std::printf("\n[見るべきは倍率そのものより「効率」]\n");
    std::printf("  コア数が %u なら、理想は %u 倍です。実際にどこまで届いたかを見ます。\n", hw, hw);

    std::printf("\n[軽い処理の効率が低い理由]\n");
    std::printf("  この処理は計算がほとんど無く、速度がメモリの転送能力で決まります\n");
    std::printf("  （メモリ律速）。スレッドを増やしてもメモリ帯域は増えないので、\n");
    std::printf("  待ち時間は減らず、理想倍率には届きません。\n");
    std::printf("  加えて、仕事を配って集めるコストが相対的に大きく効いてきます。\n");

    std::printf("\n[重い処理の効率が高い理由]\n");
    std::printf("  計算そのものが重い（計算律速）ので、コアを増やした分だけ\n");
    std::printf("  素直に速くなります。並列化が最も効くのはこのタイプです。\n");

    std::printf("\n★結論: 並列化すれば何でも速くなるわけではありません。\n");
    std::printf("  「1体あたりの計算が重いSystem」を選んで並列化するのが正解です。\n");
    std::printf("  軽い処理を並列化しても、コア数ほどの効果は得られません。\n");
}


// ===========================================================================
// main
// ===========================================================================

static void RunExperiment(int n)
{
    switch (n)
    {
    case 1: Experiment1_MemoryLayout();        break;
    case 2: Experiment2_DeferredApply();       break;
    case 3: Experiment3_DestroyAndGeneration();break;
    case 4: Experiment4_ArchetypeTransition(); break;
    case 5: Experiment5_System();              break;
    case 6: Experiment6_AoSvsSoA();            break;
    case 7: Experiment7_ParallelScaling();     break;
    default: std::printf("1〜7 の番号を入力してください。\n"); break;
    }
}

int main()
{
    EnableUtf8Console();   // 日本語が化けないように最初に呼ぶ

    std::printf("=========================================\n");
    std::printf(" CCL ECS Playground\n");
    std::printf("=========================================\n");
    std::printf("ECS だけを切り出した実験場です。\n");
    std::printf("ブレークポイントを置いてから番号を選ぶと、その実験だけを追えます。\n");

    for (;;)
    {
        std::printf("\n-----------------------------------------\n");
        std::printf(" 1 : Chunk のメモリレイアウト（SoA）\n");
        std::printf(" 2 : 構造変更の遅延（Spawn の実体化タイミング）\n");
        std::printf(" 3 : 破棄・穴詰め・世代による ID 再利用\n");
        std::printf(" 4 : アーキタイプ遷移（Chunk の引っ越し）\n");
        std::printf(" 5 : System と ForEach\n");
        std::printf(" 6 : 【ベンチ】AoS と SoA の速度比較\n");
        std::printf(" 7 : 【ベンチ】ForEach と ForEachParallel の比較\n");
        std::printf(" a : 全部まとめて実行\n");
        std::printf(" q : 終了\n");
        std::printf("-----------------------------------------\n");
        std::printf("番号を入力して Enter > ");
        std::fflush(stdout);

        char buf[64] = {};
        if (!std::fgets(buf, sizeof(buf), stdin)) break;   // Ctrl+Z / EOF で終了

        const char c = buf[0];

        if (c == 'q' || c == 'Q') break;

        if (c == 'a' || c == 'A') {
            for (int i = 1; i <= 7; ++i) RunExperiment(i);
            continue;
        }

        if (c >= '1' && c <= '7') {
            RunExperiment(c - '0');
            continue;
        }

        std::printf("入力が認識できませんでした。\n");
    }

    std::printf("\n終了します。\n");
    return 0;
}
