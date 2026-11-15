# BloodLance

Effekseer 1.80.8 付属のサンプル `01_Pierre02/BloodLance` を使用しています。作者は Pierre、サンプルのライセンスは CC0 です。

このフォルダの `BloodLance.efkefc` は Effekseer で編集できます。モデルと画像への相対パスを保つため、`Model` と `Texture` は同じフォルダ内に置きます。

## CG3 での再生

Visual Studio 2026 の C++ ワークロードと EffekseerForCpp 1.80.8 を使います。別のPCでセットアップする場合は、リポジトリのルートで次を実行します。`SdkSource` には `CMakeLists.txt` が入っているフォルダを指定します。

```powershell
.\SetupEffekseer.ps1 -SdkSource 'C:\path\to\EffekseerForCpp1.80.8'
```

BloodLance は素材として保存しています。SDK が見つかる構成では、現在のデモで使用している CosmicMist を M キーまたは ImGui の「CosmicMist を再生 (M)」で発動できます。

ライブラリは `x64/EffekseerSDK` に配置されます。別の場所に同じ構成のSDKを用意した場合は、MSBuild の `EffekseerSdkDir` プロパティで指定できます。
