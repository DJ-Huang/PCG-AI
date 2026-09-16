<p align="right">
  <a href="README.md">English</a> | <strong>日本語</strong>
</p>

<p align="center">
  <img src="docs/picg-logo.png" alt="PICG — Procedural Intelligent Content Generation" width="400">
</p>

# PICG

**Procedural Intelligent Content Generation**

PICG は、編集可能なグラフアセットとインテリジェントなコンテンツワークフローを中心に設計された、クロスエンジン対応のプロシージャルコンテンツ生成フレームワークです。React ベースのグラフエディタ、C++17 のジオメトリランタイム、localhost 上で動作する HTTP/MCP Cook サーバー、そして Unity/Tuanjie 統合を組み合わせています。

このプロジェクトの中心となる考え方は、**オーサリングツールやエンジン統合を交換可能なフロントエンドとして扱いながら、グラフデータと実行セマンティクスを移植可能かつ一貫した形に保つこと**です。グラフは手動でも Agent 支援ワークフローでも作成でき、同一のネイティブコアで実行し、複数の環境でプレビューできます。

> **プロジェクトステータス:** 現在も活発に開発中です。安定版リリースまでは、グラフスキーマ、API、統合仕様が変更される可能性があります。

## 主な機能

- **Graph-first オーサリング** — 編集可能でバージョン管理されたグラフファイルを、エディタ、ランタイム、各種統合の共通契約として使用します。
- **ネイティブのプロシージャルランタイム** — C API を通じて公開される C++17 ベースのジオメトリおよびグラフ実行コア。
- **Web オーサリングとプレビュー** — React Flow ベースのエディタ、リアルタイム 3D プレビュー、GLB エクスポートワークフロー。
- **Unity / Tuanjie 統合** — 外部 `pcg-server` ランタイムを利用するエディタ側のグラフワークフロー。
- **Agent 対応ワークフロー** — 検証、Cook、プレビューキャプチャ、自動化されたコンテンツワークフロー向けの localhost HTTP/MCP エンドポイント。
- **再利用可能なコンテンツ構成要素** — manifest ベースのノード定義、サブグラフ、スキーマ、厳選されたサンプル。

## アーキテクチャ

PICG は、オーサリング、実行、エンジン統合を独立したレイヤーに分離しています。

```text
Web / Unity / Tuanjie / Agent workflows
                 │
                 │ Graph JSON
                 ▼
             pcg-server
                 │
                 ▼
              pcg-core
                 │
        geometry / points / materials
                 │
        ┌────────┴────────┐
        ▼                 ▼
   Web preview       Engine preview
   + GLB export      + FBX / scene data
```

これにより、グラフ契約はエディタに依存せず、C++ ランタイムを単一の実行レイヤーとして利用できます。

詳細は [Architecture](docs/architecture.md) を参照してください。

## クイックスタート

### 前提条件

- Node.js `^20.19.0` または `>=22.12.0`
- CMake 3.20+
- C++17 対応コンパイラ
- CMake が自動的に提供しない環境では libcurl の開発ファイル
- Unity プロジェクトを利用する場合は Unity 2022.3 または Tuanjie 1.6.x

リポジトリをクローンし、サブモジュールを初期化します。

```bash
git clone --recurse-submodules https://github.com/DJ-Huang/PICG.git
cd PICG
```

macOS または Linux では、ネイティブサーバーと Web エディタをまとめて起動できます。

```bash
./scripts/run-pcg-web.sh
```

このスクリプトは、不足しているネイティブ成果物をビルドし、必要な Web 依存関係をインストールした上で、以下を起動します。

- Web editor: `http://127.0.0.1:5173`
- Health endpoint: `http://127.0.0.1:17890/v1/health`
- MCP endpoint: `http://127.0.0.1:17890/mcp`

個別に起動する場合:

```bash
./scripts/build-pcg-server.sh
./scripts/run-pcg-server.sh

cd web/pcg-editor
npm ci
npm run dev
```

Windows では、次のコマンドでサーバーをビルドして起動できます。

```powershell
.\scripts\build-pcg-server.ps1 -Run
cd web\pcg-editor
npm ci
npm run dev
```

初回セットアップと Unity ワークフローの詳細は [Getting Started](docs/getting-started.md) を参照してください。

## リポジトリ構成

```text
PICG/
├── Agent/                 Agent skills and reusable procedural workflow tooling
├── docs/                  User, architecture, and integration documentation
├── examples/              Graphs, tests, subgraphs, showcases, and storyboards
├── library/               Canonical built-in subgraph library
├── pcg-core/              C++ graph runtime and geometry algorithms
├── pcg-fbx-exporter/      Standalone FBX export library
├── pcg-server/            Local HTTP/MCP cook and agent backend
├── schema/                Graph schemas and node manifest
├── scripts/               Build, run, sync, and validation commands
├── Unity/                 Unity/Tuanjie integration project and curated samples
└── web/pcg-editor/        Vite + React authoring application
```

## サンプル

リポジトリ全体のサンプルは [examples/](examples/README.md) にあります。

- `examples/graphs/` — 実行可能な機能グラフおよびプロダクション向けグラフ
- `examples/tests/` — 最小構成の回帰テスト用フィクスチャ
- `examples/subgraphs/` — リンクされたサブグラフのサンプル
- `examples/showcases/` — 完成形のリファレンスベースケーススタディ
- `examples/storyboards/` — シーケンスおよびレイアウトのブロックアウト

Unity 固有のシーン、マテリアル、サンプル規約については [Unity/README.md](Unity/README.md) を参照してください。

ブラウザでリポジトリ内のグラフを確認する場合:

```text
http://127.0.0.1:5173/review?graph=examples/graphs/stone-arch-bridge.pcg
```

## ビルドと検証

### ネイティブランタイム

```bash
./scripts/build-pcg-core.sh --run-tests
```

### Web エディタ

```bash
cd web/pcg-editor
npm ci
npm run lint
npm run build
npx vitest run
```

### リポジトリ契約の検証

```bash
python3 scripts/validate-manifest.py
python3 scripts/validate-subgraph-schema.py
python3 scripts/validate-builtin-library.py
```

### サーバーのスモークテスト

`pcg-server` を起動した後に実行します。

```bash
./scripts/verify-pcg-server.sh
```

スクリプト一覧については [scripts/README.md](scripts/README.md) を参照してください。

## Unity / Tuanjie

`Unity/` をプロジェクトルートとして開きます。Unity は `PcgCore` や FBX exporter をプロセス内で直接ロードしません。Cook を行う前に `pcg-server` を起動し、**PCG → Server → Health Check** で接続を確認してください。

このリポジトリには、マシンローカルな Unity package reference は含まれていません。任意のローカルエディタ統合は `Packages/manifest.json` にコミットせず、各開発者の環境へ個別にインストールしてください。

シーンの場所、サンプル規約、生成ディレクトリのルールについては [Unity/README.md](Unity/README.md) を参照してください。

## ドキュメント

まず [documentation index](docs/README.md) を参照してください。主要なガイドは以下です。

- [Getting Started](docs/getting-started.md)
- [Architecture](docs/architecture.md)
- [PCG server](docs/pcg-server.md)
- [Node reference](docs/node-reference.md)
- [Subgraph library](library/README.md)

## コントリビューションとセキュリティ

Pull Request を作成する前に [CONTRIBUTING.md](CONTRIBUTING.md) を、脆弱性を報告する前に [SECURITY.md](SECURITY.md) を確認してください。コミュニティの行動規範は [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)、メディアの出所とサードパーティー条件は [ASSET_LICENSES.md](ASSET_LICENSES.md) に記載されています。

## ライセンス状況

現時点ではオープンソースライセンスは選定されていません。著作権者によって `LICENSE` ファイルが追加されるまでは、このリポジトリは法的にはオープンソースではなく、再利用権も付与されません。公開リリース前に MIT、Apache-2.0、またはその他のライセンスを選定し、アセット権利の確認を完了してください。
