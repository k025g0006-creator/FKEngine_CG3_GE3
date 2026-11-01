# FKEngine

| ブランチ | Debug | Development | Release |
| --- | --- | --- | --- |
| main | [![Debug build](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/build-debug.yml/badge.svg?branch=main&event=push)](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/build-debug.yml?query=branch%3Amain) | [![Development build](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/build-development.yml/badge.svg?branch=main&event=push)](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/build-development.yml?query=branch%3Amain) | [![Release build](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/build-release.yml/badge.svg?branch=main&event=push)](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/build-release.yml?query=branch%3Amain) |

`FKAlpha.slnx`をVisual Studioで開いて使用します。構成は`Debug`・`Development`・`Release`、プラットフォームは`x64`です。現在のツールセットは`v145`です。

## フォルダ構成

| 場所 | 内容 |
| --- | --- |
| `Source/App` | アプリケーションの開始処理とシーン（main.cpp） |
| `Source/Engine/Core` | ウィンドウ、ログ、文字列処理 |
| `Source/Engine/Graphics` | DirectX基盤と描画リソース |
| `Source/Engine/Input` | 入力処理 |
| `Source/Engine/Audio` | 音声処理 |
| `Source/Engine/Math` | ベクトル・行列 |
| `Source/Engine/Debug` | デバッグカメラ、リソースリーク確認 |
| `Resources` | モデル、テクスチャ、音声などの素材 |
| `Resources/Shaders` | HLSLシェーダーと共通定義 |
| `externals` | 使用中の外部ライブラリ（DirectXTex・ImGui） |
| `Config` | 共通ビルド設定、ImGuiの配置設定、スペルチェック辞書 |
| `../FKEngine_Build` | プロジェクト外に置く実行ファイル、静的ライブラリ、中間ファイル、ログ |
| `Archive` | 旧コピー、提出用フォルダ、旧設定の保管場所 |

## ビルドと実行

- 共通出力先：`../FKEngine_Build/`（現在の配置では`C:\FKEngine_Build`）
- 実行ファイル：`../FKEngine_Build/Bin/x64/<構成>/FKAlpha.exe`
- 外部ライブラリ：`../FKEngine_Build/Lib/x64/<構成>/DirectXTex.lib`、Debugでは`ImGui.lib`も生成
- 中間ファイル：`../FKEngine_Build/Obj/x64/<構成>/<プロジェクト>/`
- DirectXTexの生成シェーダー：`../FKEngine_Build/Generated/DirectXTex/x64/<構成>/`
- 実行時ログ・クラッシュダンプ：実行ファイルの隣の`Logs/`・`Dumps/`

出力先は`Config/BuildSettings.props`の`FKEngineOutputRoot`で一括設定しています。プロジェクトをコピー・提出する際は、隣の`FKEngine_Build`を含める必要はありません。整理前の生成物と過去のログも同フォルダへ引き継いでいます。

Visual Studioでの作業ディレクトリはプロジェクトのルートです。ビルド後には、必要な素材・シェーダー・DXCのDLLが実行ファイルの隣にもコピーされます。実行時のImGui設定は実行ファイル側の`Config/imgui.ini`へ保存します。実行フォルダを別の場所へ渡すときは、`Resources`と`Config`、DLLを含めてコピーしてください。

DirectXTexとImGuiはソリューション内の静的ライブラリプロジェクトとしてビルドします。アプリケーションはプロジェクト参照でリンクし、DirectXTex用の旧NuGetパッケージは使用しません。ImGuiはアプリケーションのDebug構成で有効になります。

新しいC++ファイルは担当する`Source`配下に置いてプロジェクトへ登録し、ソリューションエクスプローラーでも同じフォルダのフィルターへ追加してください。

## GE3・CG3とmainの使い分け

mainを動作確認済みの共通版として使います。GE3のクラス化・入力処理・フォルダ整理と、CG3のパーティクル・地面・各種ライトを共存させています。日本語のImGuiでライトや減衰を調整できます。

1. 作業前にmainを最新にし、新しい作業はmainから分けたブランチで進めます。
2. 既存のGE3・CG3で続きを作る場合は、最初にmainをその作業ブランチへマージします。これにより整理後のファイル配置とクラス経由の呼び出しを引き継げます。
3. 変更をコミットしたらmainへマージし、競合は両方の機能が残るように解消します。
4. Debug・Development・Releaseをビルドし、実行時に使うHLSLも確認してからmainをpushします。

ソースは`Source/App/main.cpp`、シェーダーは`Resources/Shaders`が使用中のファイルです。`Archive`内の旧コピーを編集しても実行結果には反映されません。

起動と描画の確認には`FKAlpha.exe --smoke-test`も使えます。3フレーム描画後に通常の終了処理で終了し、実行ファイル横の`Logs`へ確認結果を残します。このモードでは確認中の音声再生を省きます。

## GitHub Actionsの自動ビルド

コミットをpushすると、GitHubの新しいWindows環境でDebug・Development・Releaseをそれぞれビルドします。プルリクエストでも同じ確認を実行します。これらの設定を含むmainから作業ブランチを作成するか、既存のGE3・CG3へmainをマージしてから使ってください。

1. ファイルを編集し、必要なソース・モデル・画像などをコミットしてpushします。
2. GitHubの[Actions](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions)で結果を確認します。
3. 成功するとREADME先頭のバッジが`passing`、失敗すると`failing`になります。バッジをクリックすると該当構成の実行履歴を開けます。

各実行では、プロジェクト参照ファイルの存在、競合マーカーの残存、C++のビルド、全HLSLのコンパイル、実行フォルダへの素材のコピーを確認します。ローカルの生成済みファイルは使いません。必要なファイルを`.gitignore`で除外したままpushすると、この確認で検出できます。実機での描画確認にはPC側の`--smoke-test`を使ってください。

成功した実行のページ下部のArtifactsから、構成別の実行ファイル・素材一式とビルドログを取得できます。保存期間は7日です。

共通処理は`.github/workflows/windows-build.yml`と`Tools/Build-CI.ps1`にあり、構成ごとのワークフローから呼び出します。実行環境は`windows-2025-vs2026`（Visual Studio 2026 / v145）です。

ローカルでもPowerShell 7から同じ確認ができます。

```powershell
./Tools/Build-CI.ps1 -Configuration Debug -OutputRoot C:\FKEngine_Build\CI-Local
```

バッジの表示方法は[GitHub公式ドキュメント](https://docs.github.com/en/actions/how-tos/monitor-workflows/add-a-status-badge)に従っています。初回の実行が完了するまでは、バッジに結果が表示されないことがあります。

## 不要なファイルの検査

| ブランチ | 不要ファイル検査 |
| --- | --- |
| GE3 | [![Repository file check](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/check-repository-files.yml/badge.svg?branch=GE3&event=push)](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/check-repository-files.yml?query=branch%3AGE3) |
| main | [![Repository file check](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/check-repository-files.yml/badge.svg?branch=main&event=push)](https://github.com/k025g0006-creator/FKEngine_CG3_GE3/actions/workflows/check-repository-files.yml?query=branch%3Amain) |

`main`・`GE3`へのpushと、`main`向けのプルリクエストで、Gitの管理対象に不要なファイルが混ざっていないか確認します。mainのバッジは、この設定をmainへマージして実行した後に結果が表示されます。

- `.pdb`・`.obj`などのビルド生成物、実行ファイル、DLL、個人用のVisual Studio設定を検出します。
- `Build`・`x64`・`.vs`・`logs`・NuGetキャッシュなどの出力フォルダも検出します。
- `.gitignore`で除外されているのに、すでにGitで追跡されているファイルも検出します。
- `Resources`内のモデル用`.obj`は許可します。ImGui同梱の4本のGLFWライブラリは`Config/RepositoryFilePolicy.json`に理由付きで登録しています。`Source/Engine/Debug`はソースフォルダなので許可します。

手元で確認する場合は、リポジトリのルートで次を実行します。

```powershell
./Tools/Check-RepositoryFiles.ps1
```

不要な生成物を誤ってGitに追加していた場合は、`.gitignore`へ登録したうえで、対象を`git rm --cached -- 対象のパス`でGitの管理から外してコミットします。`--cached`を指定すると、手元のファイルは残ります。必要なモデル・画像・外部ライブラリは削除せず、生成物との違いを確認してください。

検査はファイルを変更・削除せず、見つけたパスと理由を表示して失敗します。GitHubの実行ページのArtifactsからJSON形式の検査結果を取得できます。検出ルールの動作確認も、使い捨てのGitリポジトリで自動実行します。

Gitの除外規則と追跡済みファイルの確認には、[git ls-filesの公式仕様](https://git-scm.com/docs/git-ls-files)を使っています。

### スプライトのクラス構成

- Source/Engine/Graphics/SpriteBase：共通のRootSignature、PSO、専用シェーダー、画面サイズ。アプリ側で1つ生成します。
- Source/Engine/Graphics/Sprite：1枚分の頂点・インデックス・定数バッファ、位置、サイズ、回転、基準点、色・透明度、UV。画像ごとに生成します。

TextureManager::InitializeとSpriteBase::Initialize後に、Sprite::Initializeへ共通設定と画像ファイルパスを渡します。テクスチャとSRVはTextureManagerが共有・保持するので、全Spriteの解放後にTextureManager::Finalizeを呼びます。3D/Particleの描画後にSpriteBase::SetCommonRenderStateを1回呼び、各SpriteのUpdate、Drawを呼びます。各インスタンスのComPtrがバッファを解放するので、手動のReleaseは不要です。

初期状態は同じSpriteBaseから2枚の画像を表示します。ImGuiの「スプライト」で操作対象を選び、位置・サイズ・回転・色・透明度・基準点・UVを個別に変更できます。初期UVは左上(0,0)、右下(1,1)です。
SpriteBaseの初期化は、Initialize → CreateGraphicsPipelineState → CreateRootSignatureの順に呼び出し、RootSignatureの完成後にPSOを生成します。描画時はDirectXBase::PreDraw → 3D/Particle描画 → SpriteBase::SetCommonRenderState → 各Sprite::Drawの順です。DirectXBaseは非所有ポインタで参照し、GetDirectXBaseから取得できます。

Spriteの初期化はCreateVertexData、CreateMaterialData、CreateTransformationMatrixDataに分割しています。VertexData（位置・UV・法線）、Material（色・照明フラグ・UV行列）、TransformationMatrix（WVP・World）はSprite内の構造体で、対応するGPUバッファも各インスタンスが所有します。Sprite専用シェーダーも同じレイアウトに揃えています。
数学の共有型はSource/Engine/MathのVector2.h、Vector3.h、Vector4.h、Matrix4x4.h、Transform.hへ分離しています。変形情報はFKEngine::Transform、行列などの自由関数はMath名前空間です。Matrix4x4の演算子は型と同じスコープに置き、Mathの計算へ委譲します。

ImGuiの「スプライト」では、UVチェッカー・モンスターボール画像・円形画像・草・フェンスを選んで1～20枚ずつ生成できます（最大256枚）。選択した1枚の位置・回転・色と透明度・サイズは、Spriteのsetter/getterを通じて編集します。複製は設定を引き継ぎ、少し位置をずらして追加します。個別削除・全削除後も再生成できます。可変リストはstd::vector<std::unique_ptr<Sprite>>で所有し、削除時と終了時に自動解放します。共通のSpriteBaseは変更しません。
描画のsmoke-testは、13枚への追加・複製、個別削除と全削除、空の状態から1枚への再生成を3フレームで確認します。通常起動ではこの自動操作を行いません。

### テクスチャ管理

TextureManagerはGetInstanceで取得し、mainが所有するDirectXBaseをInitializeへ渡します。LoadTextureは画像を読み込んでキャッシュへ格納し、同じ画像の再読み込み要求は既存データを再利用します。GetSrvHandleGPUで描画用ハンドル、GetMetadataで元画像の幅・高さ等を取得できます。パスの相対・絶対、./、区切り文字、Windowsの大小文字を正規化します。
DirectXBase::kMaxSRVCountは512です。0番はImGui、1番はInstancing用に予約し、2～511番をTextureManagerが割り当てるため、管理できる画像は最大510枚です。読み込む前に最大数分のvector容量をreserveし、上限超過はエラーで通知します。
画像のデコードとMipMap生成はTextureManager、GPUリソースの生成・転送はDirectXBaseの役割です。転送用UploadHeapはTextureData内に保持し、DirectXBase::PostDrawのGPU完了待ち後にReleaseUploadResourcesで解放します。終了順は、GPU完了 → 全Sprite解放 → TextureManager::Finalize → DirectXBase解放です。Finalize後のGetInstanceは新しい管理インスタンスを作るため、終了処理では呼び出しません。
ImGuiの「選択中の画像」で既存Spriteの画像を切り替えられます。「読み込み済み画像」はキャッシュ枚数であり、Spriteを複製しても増えません。「元画像サイズに合わせる」で画像の解像度を表示サイズへ反映します。smoke-testはパス違いでの重複読込防止、読込失敗時のキャッシュ不変、転送完了後のUploadHeap解放も確認します。

### アンカー・反転・画像の切り出し

SpriteのSetAnchorPoint/GetAnchorPointで基準点を指定します。(0,0)は左上、(0.5,0.5)は中央、(1,1)は右下です。SetFlipX/GetFlipX、SetFlipY/GetFlipYで左右・上下を反転します。反転・回転は基準点を中心に行うため、中央を固定したい場合はアンカーを(0.5,0.5)にします。SpriteBaseの両面描画設定を利用するので、反転しても表示されます。
SetTextureLeftTop/GetTextureLeftTopは元画像内の左上座標、SetTextureSize/GetTextureSizeは切り出す幅・高さで、どちらもピクセル単位です。TextureManagerのメタデータで割ってUVへ変換します。範囲は画像内、切り出しサイズは最低1ピクセルに制限します。表示サイズのSetSizeとは独立です。既存の詳細UV変形は、この切り出しUVへ追加で適用されます。
画像を指定したSprite初期化の最後にAdjustTextureSizeを呼び、切り出し範囲と表示サイズを元画像の全体に合わせます。アプリ側でSetSizeすれば任意の表示サイズにできます。画像切り替え時は切り出しだけを全体へ戻し、現在の表示サイズを保持します。
ImGuiでは「画像の基準点」「左右反転」「上下反転」「切り出し左上(px)」「切り出しサイズ(px)」を各Spriteごとに操作できます。「画像全体を使う」は範囲のみをリセットし、「切り出しサイズで表示」は表示サイズを切り出しサイズへ合わせます。複製時も反転・範囲を引き継ぎます。TextureManagerはGetTextureIndexByFilePath、番号指定のGetMetadata/GetSrvHandleGPUにも対応し、範囲外の番号はReleaseでも拒否します。

### 番号指定・画像未指定のSprite初期化

Sprite::Initializeはファイルパス指定に加え、Initialize(SpriteBase*, uint32_t textureIndex = UINT32_MAX)にも対応しています。番号はTextureManager::GetTextureIndexByFilePathで取得します。番号を省略したSpriteは画像なしで初期化され、Updateは可能ですがDrawはスキップされます。後からSetTextureまたはSetTextureIndexで画像を割り当てられ、SetTextureIndex(UINT32_MAX)で解除できます。通常の番号が範囲外ならエラーとし、設定済みの画像は保持します。
TextureManager::GetTextureBuffer(index)は管理中のID3D12Resourceへの非所有ポインタを返します。Spriteの初期サイズ、切り出し範囲、ピクセル→UVの計算はこのリソースのGetDesc().Width/Heightを使用します。UVの計算は、左=切り出しX/幅、右=(切り出しX+切り出し幅)/幅、上=切り出しY/高さ、下=(切り出しY+切り出し高さ)/高さです。TextureManagerの所有権は変えません。
ImGuiの画像選択に「画像なし」を追加しています。画像なしで生成・複製したSpriteにも位置やサイズを設定でき、後から画像を選ぶと描画されます。画像がない間は元画像の情報表示と切り出し操作を無効にします。smoke-testは番号初期化の画像サイズ一致、画像未指定からの設定・描画・解除、不正番号の拒否も確認します。
