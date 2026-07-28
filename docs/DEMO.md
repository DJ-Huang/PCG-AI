# PCG Graph AI — End-to-End Demo

Reproduce the full loop: **Web edit → Graph JSON → pcg-server cook → Unity preview**.

## Prerequisites

- CMake 3.20+（macOS / Windows）
- Node.js 18+
- Unity / Tuanjie Editor
- Localhost `pcg-server`（Unity 不再加载 PcgCore dylib/dll）

## 1. Build and start C++ backend

```bash
./scripts/build-pcg-server.sh
./scripts/run-pcg-server.sh
```

Windows: `.\scripts\build-pcg-server.ps1 -Run`

Health: `curl -s http://127.0.0.1:17890/v1/health`  
Details: [`pcg-server.md`](pcg-server.md)

## 2. Editor loop

### Web editor

```bash
cd web/pcg-editor
npm install
npm run dev
```

1. Open http://localhost:5173
2. Edit the graph
3. Click **Send to Unity** → writes `schema/editor-export.pcg`

### Unity Editor

1. Open `Unity/` project
2. **PCG → Server → Health Check**（确认后端在线）
3. **PCG → Set Watched Graph…** → `schema/editor-export.pcg`
4. Enable Auto Reload / Reload；Scene 预览应更新

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Cook / Health 失败 | 启动 `run-pcg-server.sh`；检查端口 17890 |
| 预览无变化 | 确认 watched `.pcg` 路径；手动 Reload |
| `UnknownNode` | `scripts/sync-manifest.sh` 后重建 `pcg-server` |
