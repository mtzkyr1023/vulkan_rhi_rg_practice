# プロジェクト構成

クロス API (Vulkan 1.4 / D3D12) の RHI + レンダーグラフ練習レンダラー。依存方向は一方向:

```
proj001 (アプリ) → mv_GameCore (ゲーム基盤) → mv_RendererCore (機能) → mv_RendererBackend (RHI 抽象)
                └──────────────────────────→ mv_RendererCore
```

mv_GameCore は mv_RendererCore の util ヘッダ (math/types) のみに依存し、レンダラー本体とはリンクしない。

- ビルド: `my_project.slnx` (MSBuild, x64 Debug)
- シェーダ: `shaders/compile_shaders.ps1` が全 `.hlsl` を SPIR-V (`.spv`, Vulkan SDK の dxc) と署名済み DXIL (`.cso`, Windows SDK の dxc) の両方にコンパイル。エントリポイントは VSMain / PSMain / CSMain 固定で、宣言があるステージだけがビルドされる。SM 6.6 (バインドレス用)
- バックエンド切り替えは `proj001/include/engine.h` の `useVulkan_` 1 箇所。SPIR-V は `-fvk-use-dx-layout` で D3D のパッキング規則に揃えており、CPU 側の構造体は 1 定義で両対応

---

## mv_RendererBackend — RHI 抽象層

`mv::rhi::IRHI` インターフェースと、その Vulkan / D3D12 実装。上位層は API を一切知らない。

```
include/rhi/
  rhi.h              IRHI: デバイス・スワップチェーン・リソース・描画/計算コマンド・
                     即時コマンド (beginImmediateCommands)・バインドグループ更新
  resource.h         TextureDesc / BufferDesc / EResourceState / ETextureFormat / バリア構造体
  pipeline.h         パイプライン・レイアウト・バインディング記述 (EDescriptorType 等)
  commandbuffer.h    コマンドバッファのハンドルと共通部
include/memory/
  tlsf_allocator.h   TLSF アロケータ (両バックエンドのメモリサブアロケーションに使用)
include/rhi/vk1_4/   Vulkan 実装 (device / swapchain / resource / pipeline /
                     descriptor / command / frame_resource / memory / rhi)
include/rhi/dx12_0/  D3D12 実装 (同構成)
```

設計上の要点:

- **ハンドルベース**。`TextureHandle` などは u32。解放されたハンドルはフリーリストで desc 一致 (サイズ・usage 等) を条件に再利用される
- **バインドレス前提**。Vulkan は descriptor indexing、D3D12 は unbounded descriptor range。1 つのバインディングに上限 4096 のテクスチャ配列
- **push constants**: Vulkan は `[[vk::push_constant]]`、D3D12 は space9 の root constants に同一構造体をマップ。保証サイズは 128 バイト
- **ステート遷移**は `EResourceState` の before/after 指定。`eUndefined` を before に渡すと「現在の状態から」の意味 (バックエンドが追跡値を使う)。同一ステート指定 (write→write) は D3D12 では UAV バリアになる
- **3D テクスチャ** (`TextureDesc::volume`)、ストレージイメージ、コンピュートパイプライン、即時実行 (ベイク用・submit して待つ) に対応
- 既知の注意: `EResourceState` に `eCopySrc/eCopyDst` と `eTransferSrc/eTransferDst` が重複しており、バックエンドがマップするのは前者のみ (整理タスクが別途進行中)

## mv_RendererCore — 機能モジュール群

RHI の上に立つレンダラー機能。各モジュールは「シェーダを与えられて初期化され、コマンドバッファに記録する」小さなクラスで統一されている。

```
include/rg/         render_graph.h    パス宣言 (setup/execute ラムダ) から自動バリア生成。
                                      transient / import、フレーム末に initialState へ復元
include/material/   material_system.h シーン set(0) / バインドレス set(1) のレイアウト所有、
                                      マテリアルバッファ、テクスチャ登録、パイプライン変種キャッシュ
include/asset/      gltf_loader.h     glTF → 単一のグローバル頂点/インデックス配列 (cgltf)
include/virtual_texture/              ページアトラス + ページテーブル + フィードバック解決
include/shadow/     cascaded_shadow_map.h  4 カスケード 2048² アトラス。ログ分割・テクセルスナップ・
                                      シーン半径 ×2.4 の距離フィット
include/env/        environment.h     大気散乱スカイ・SH9 放射照度 (CPU 投影)・prefilter チェーン
include/compute/    mip_generator.h   ミップ生成 (dispatch)
                    buffer_fill.h     バッファクリア (フィードバック用)
                    terrain_builder.h ハイトマップ・メッシュ・マテリアルベイクを GPU 生成
                    environment_baker.h  スカイ+prefilter を dispatch でベイク。
                                      level 0 は「雲入りでベイク→ミップ/SH に反映→雲なしで再描画」
include/clouds/     cloud_renderer.h  ボリューメトリック雲: 128³ shape + 32³ detail + 512² weather、
                                      半解像度マーチ + 深度考慮アップサンプル合成、
                                      512² 雲影マップ (毎フレーム、カメラ追従)
include/water/      water_surface.h   解析平面の水面: 波法線・Beer-Lambert 吸収・
                                      フル解像度 SSR (compute マーチ) + キューブ反射の confidence 合成
include/fog/        height_fog.h      指数高度フォグ + ライトシャフト (カスケード/雲影を
                                      サンプルするジッタ付きマーチ、圏外は閉形式積分)
include/terrain/    terrain.h         ハイトマップ→asset::Model 化 (下流は地形を知らない)
include/props/      prop_renderer.h   エンティティ配置モデルの前方描画。レンダラーで唯一
                                      モデル行列を持つパス (VB は per-draw 行列を復元できない)。
                                      物理形状はボディ原点に中心を置くので、描画行列は
                                      バウンズ中心を先に畳んで両者の「真ん中」を一致させる
                                      (GltfLoader::setRetainGeometry で CPU 頂点を保持し、
                                      凸包や半エクステントをレンダーメッシュ自身から採寸)
include/anim/       animator.h        スケルタルアニメーション再生: チャネルサンプリング →
                                      レストポーズに上書き → jointOrder (親→子) で
                                      グローバル化 → inverseBind を畳んでパレット。全 CPU。
                                      クォータニオンは nlerp (キー間隔なら slerp と不可分)
include/props/      skinned_prop_renderer.h  prop パスの変形版: 頂点 4 ジョイント LBS。
                                      パレットはフレームインフライト分の storage buffer に
                                      float4 行として置く (structured float4x4 は 2 コンパイラ
                                      間のレイアウト問題を招くので行 4 本で構成)。
                                      ジョイント index は f32x4 頂点属性 (eFloat4 は必ずある)
include/voxel/      sculpt_volume.h   掘れる/盛れるマーチングキューブ: CPU 密度グリッド
                                      (40³ セル、コーナーサンプル正=中身)、Bourke の古典
                                      テーブル、法線は密度勾配の負向き。編集はキー押下レート
                                      なので全面 CPU 再メッシュで十分 (数 ms)。頂点バッファは
                                      フレームインフライト分二重化。同じ三角形スープが
                                      描画 (prop パス、ワールド焼き込み・identity 行列) と
                                      Bullet の btBvhTriangleMeshShape (編集毎に丸ごと再構築)
                                      の両方に流れる — 見えている彫りと立てる彫りが常に一致
                    sculpt_gpu.h      同じ密度の GPU メッシュパス (デフォルト): 密度を
                                      host-visible バッファへアップロード → コンピュート MC
                                      (セル毎 1 スレッド、テーブルは storage buffer) →
                                      InterlockedAdd で頂点追記 → drawIndirect。草カリングと
                                      同じ追記+間接描画+バリアの形。CPU 側は Bullet 用に併存
                                      (物理は CPU にしか住めない)。UI でパス切替可。
                                      連続変形 (Waves): 毎フレーム deform CS がベース密度を
                                      進行波の変位で縦に再サンプリング → その結果を march。
                                      ベースは不変なので彫刻も物理も無傷、波を止めれば元通り。
                                      ブラシも GPU: queueBrush → brush CS が GPU 密度を
                                      その場で加筆 (デバイスローカル化、全量アップロードは
                                      place/reset 時のみ)。CPU は物理用ミラーにブラシだけ
                                      即時適用し、メッシュ再構築はストローク休止後 0.3s に
                                      遅延 — 連打中の CPU 再メッシュ+BVH コストがゼロ。
                                      **チャンク化**: パイプライン/テーブル共有、バッファ類は
                                      チャンク毎 (addChunk)。初回 Play 突入時にスポーン周辺
                                      5×5 チャンク (24m 角、計 120m 四方) を地形から初期化
                                      (placeFromGround: ground+2.5m の岩盤キャップ、密度は
                                      y に線形なので MC が斜面法線ごと地表を正確に再現)。
                                      ブラシは重なる全チャンクに同一適用 → 共有コーナーが
                                      同値になり継ぎ目は割れない。物理はチャンク毎スロットで
                                      ダーティのみ遅延リビルド。既知の限界: 地形メッシュは
                                      マスクできないため、キャップ厚 (2.5m) を超えて掘ると
                                      埋まっている地形面が床として現れる。
                                      **ストリーミング**: チャンクは絶対セル座標 floor(xz/24m)
                                      で識別し、25 スロットへトーラス写像 ((c mod 5))。窓が
                                      プレイヤーのセルを追い、欠けたセルはキュー経由で
                                      毎フレーム 1 個だけ補充 (境界越え 5 個が 5 フレームに
                                      分散、ヒッチなし)。編集済みセルは退場時に密度を
                                      unordered_map へスタッシュ、再入場で復元 — 掘った洞窟は
                                      世界のどこへ行って戻っても残る
include/hud/        hud_renderer.h    ゲーム側の画面 (クロスヘア・タイトル・プロンプト)。
                                      ImGui は開発者パネル、こちらはプレイヤーの層。組み込み
                                      8x8 ピクセルフォント (font8x8_basic, PD) を起動時に
                                      アトラス化、白スロットで矩形も同一ドローに同乗。
                                      バックバッファに UI パス内で ImGui の直前に描く
include/debug/      debug_line_renderer.h  ワールド空間ラインの上書き描画 (LineList、深度
                                      テストあり書き込みなし)。頂点は毎フレーム CPU から
                                      書き直し (フレームインフライト分の host-visible VB)。
                                      物理ワイヤーフレーム (Bullet btIDebugDraw 収集) が使う
include/post/       post_process.h    ポストチェーン基盤 (TAA→Bloom→Tonemap→FXAA→Lens)
                    effects.h         各エフェクト
include/ui/         imgui_renderer.h  ImGui バックエンド
include/util/       math.h (行ベクトル/RH/深度0..1) / noise.h (地形用ノイズ) /
                    types.h (INVALID_HANDLE 等) / parallel.h
```

モジュール共通の作法:

- 記述子は**変更されたときだけ**再ポイント (`boundDepth_` パターン)。フライト中のフレームが読んでいる set の書き換えは両 API で競合、Vulkan では validation エラー
- エンジン所有のテクスチャ (深度・速度・雲影・SSR 反射先など) はグラフに import するか、雲マーチのようにパス内で手動バリア
- CPU/GPU 両実装を持つものは CPU 側を残す (terrain / environment)。GPU 経路が失われてもビルドは劣化動作する

## mv_GameCore — ゲーム基盤モジュール

レンダリングを知らないゲームプレイ側の部品。静的ライブラリ。

```
include/game/
  game_clock.h            固定タイムステップ (60 Hz + 補間 alpha)。ヘッダオンリー
  input.h / input.cpp     役割ベースのキー列挙 (EKey)、ポーリング + エッジ検出。
                          仮想キーの対応は input.cpp の 1 箇所のみ
  transform.h             位置・yaw/pitch/roll・スケール → 行ベクトル行列。ヘッダオンリー
  world.h / world.cpp     エンティティレジストリ: 世代付きハンドル + フリーリスト。
                          ECS ではない (必要になるまで構造体プール)
  height_field.h/.cpp     ゲーム側の「地面」: サンプラコールバック経由で高さ・法線・
                          レイキャスト (二分法)。地形データの所有者を知らない。
                          エンジンが渡すサンプラは**三角形厳密** — 描画メッシュと同じ
                          (i+1,j)-(i,j+1) 対角割りの平面で補間する (バイリニアはセル縁
                          でしか一致せず、セル中央でキツネの足が沈む)
  character_controller.h/.cpp  Bullet のキネマティックカプセルに乗るキャラクター。手触り
                          (指数収束の操舵・ジャンプ) と 前/現在ペアの補間はここ、衝突は
                          全部スイープ側。step 前に update(操舵)、step 後に sync(読み戻し)
  physics_world.h/.cpp    Bullet ラッパー (pimpl、ヘッダに bt 型を出さない)。地形は
                          btHeightfieldTerrainShape (ハイトマップの実体を参照)、動的ボディは
                          球・箱・凸包 (btShapeHull でレンダーメッシュから間引き)、
                          btKinematicCharacterController (up 軸は明示必須・デフォルトは X)、
                          行列は getOpenGLMatrix がそのまま行ベクトル行優先に一致。
                          raycast() は world->rayTest 一発で地形/プロップ/水面を等しく見る:
                          水面はレイ専用衝突グループの静的平面で、ボディとは絶対にペアに
                          ならない (グループ/マスクとも水ビットのみ)。自キャラは常に除外。
                          step 後にマニフォールドから衝突イベントを収穫 (最強接触点の
                          インパルス付き、微小インパルスは足切り) → takeContactEvents
  audio.h/.cpp            XAudio2 ラッパー (pimpl)。モノ 44.1kHz float バッファ登録 +
                          ボイスプールで撃ちっ放し再生。3D は距離減衰 + ヨーからのパン
                          だけの安い空間化。COM は自前で初期化/返却
  sound_synth.h/.cpp      効果音を合成する「アセット」: impact/splash/footstep/jump/land。
                          固定シード xorshift + ワンポール LP。ファイル無し・ライセンス
                          無し、実録音への差し替えは SoundHandle の向こうで自由
  game_state.h            Title/Playing/Paused のステートマシン (ヘッダオンリー)。合法な
                          遷移が表になっており、違法な要求は半端に適用されず拒否される。
                          「入るとき何が起きるか」はエンジン側 (startPlay のテレポート等)。
                          ポーズは固定クロックを tick しないだけ — 再開はそのステップの続き
```

作法: 高さ取得は `std::function<f32(x,z)>` で受ける（レンダラーのハイトマップでも将来の別データでも同じコードが動く）。シミュレーションは固定ステップで回し、カメラは補間された目線を描く。

glTF ローダ補足: `loadSkinned` はスキン付きモデル専用のパス — 頂点はメッシュ空間の生値
(スキンがノード変換を無効化するのは仕様)、JOINTS_0/WEIGHTS_0 を float4 で保持、スケルトン
(逆バインドは glTF 列優先バイト列 = 行ベクトル行優先バイト列なので直コピー) とクリップ全部。
**Khronos Fox は非インデックス** — indices が無い primitive には連番を合成する。
Fox.glb (CC-BY 4.0: PixelMannen モデル / @tomkranis アニメーション) が assets/models に同梱。

## proj001 — アプリケーション

```
include/engine.h   全パラメータのデフォルトと Engine クラス (useVulkan_ もここ)
src/engine.cpp     初期化・フレーム構築 (レンダーグラフのパス列)・ImGui パネル
src/winmain.cpp    Win32 エントリ・リサイズ通知
```

フレームのパス列 (レンダーグラフ):

```
Skybox → Cloud Shadows (dispatch) → Shadow Cascades → Visibility Pass →
Visibility Resolve (シェーディング) → [Clouds ⇄ Water SSR + Water] →
Height Fog (+ light shafts) → Post Process (TAA→Bloom→Tonemap→FXAA→Lens) → UI
```

- 太陽は skybox.hlsl が**解析的に**描く (ディスク + 狭いハロー、lightDirection 直結)。
  環境キューブに焼かないのは IBL/反射に千倍テクセルを入れないため。IBL スケールの外で
  加算するので Ambient を絞っても太陽は消えない。雲コンポジットが上に乗るため曇天では
  自然に隠れる。注入輝度は控えめ (disc×8) — 盛るとブルームが周辺ごと白飛びさせて
  肝心のディスクが埋没する、を一度やった
- Clouds と Water の順序は**カメラ高度で入れ替わる**: 雲底より上では Water→Clouds (雲が湖を隠す)、下では Clouds→Water (水中でも雲が水越しに見える)
- ImGui は 8 タブ構成: Scene / Clouds / Terrain / Water / Post / Sky / Shadows / Streaming。タブは選択中のみウィジェットが実行される (dirty フラグの再ベイクはタブ表示中に発火)
- シーンは地形シーン (2000 m, 513², GPU 生成) と Sponza を切り替え可能。ビジビリティバッファはグローバル頂点配列から id で再フェッチするため、切り替えは set(2) の差し替えだけ

## shaders/ — HLSL (SPIR-V / DXIL 両対応)

共有ヘッダ (`.hlsli`):

| ヘッダ | 内容 |
|---|---|
| common.hlsli | SceneConstants cbuffer (set0)・バインドレス配列 (set1)・velocity/デバッグ補助・`cloudShadowFactor` |
| shadow.hlsli | カスケード選択・表面用 `shadowFactor` (法線オフセット+PCF)・空気用 `shadowVisibilityAt` (1 タップ) |
| pbr.hlsli / ibl.hlsli | GGX BRDF・SH 放射照度・prefilter 反射。direct 項に地形影 × 雲影 |
| clouds.hlsli / cloud_density.hlsli | 雲の定数と密度場。密度場はビューマーチ・雲影ベイク・環境ベイクの 3 者で共有 |
| water_common.hlsli | WaterConstants・波の勾配ノイズ・レイ再構成/投影。水面描画と SSR マーチで共有 |
| env.hlsli | 大気散乱 (Rayleigh+Mie)・キューブ面⇔方向 |
| noise.hlsli / noise3.hlsli | 2D/3D の tileable ノイズ (value/Perlin/simplex/Worley, fBm 系) |
| vt.hlsli / terrain.hlsli / post.hlsli / bloom_cs.hlsli | 仮想テクスチャ・地形・ポスト共通 |

エントリ (`.hlsl`) は機能名で対応が取れる: `vb`/`vb_shade` (ビジビリティバッファ)、`cloud_*`、`water`/`water_ssr`、`fog`、`terrain_*`、`env_*`、`bloom_*`/`taa`/`tonemap`/`fxaa`/`lens` (ポスト)、`shadow_depth`、`skybox`、`model` (フォワード経路)、`mipgen`/`bufferfill` (ユーティリティ)。

### バインディング規約 (space = set)

- **space0** = シーン set: b0 SceneConstants / t1,t2 VT バッファ / t3 影アトラス / s4 比較サンプラ / t5 環境キューブ / t6 雲影マップ
- **space1** = バインドレス set: t0 マテリアル / t1[] テクスチャ配列 (無制限) / s2[4] サンプラプリセット
- **space2** = パス固有 set (vb_shade のジオメトリ、fog の深度など)
- **space9** = push constants / root constants (`MV_CUSTOM_PUSH_CONSTANTS` で独自構造体に差し替え)

CPU 側構造体には必ず `// Must match ...` コメントで対応先を明記する。

## thirdparty / assets

- `thirdparty/`: imgui, cgltf, stb, bullet3 (3.25 を mv_Bullet.vcxproj で静的ライブラリ化。
  LinearMath + BulletCollision + BulletDynamics のみ、Debug でも /O2)
- `assets/models/Sponza`: テストシーン (マテリアル多数・共有テクスチャ・アルファマスク)

---

## 落とし穴メモ (このリポジトリで実際に踏んだもの)

- **シェーダの反映は compile_shaders.ps1 → msbuild の順**。アプリは `x64/Debug/shaders/` を読み、コピーするのはビルド。コンパイルだけして実行すると古いシェーダを測ることになる
- 初期化順: モジュールが `materialSystem_` のレイアウトを借りる場合、その初期化は material system の**後**に置く (INVALID_HANDLE を掴んで黙って無効化される)
- グラフの transient はフレームごとにハンドルが変わり得る。記述子に名前を焼くテクスチャはエンジン所有 + import にする
- 深度→距離は逆行列ではなく射影係数 2 つ (`linear = B / (raw + A)`)。レイはカメラ基底 + tanHalfFov から再構成 (スカイボックスと同じ規約)
