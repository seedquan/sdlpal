# 子项目 B —— HD 资源管线(RGM 头像首期)设计文档

- 日期:2026-08-10
- 状态:已批准,待实现计划
- 所属大工程:SDLPal 真·高清重制引擎(个人自用)
- 前置:依赖子项目 A(已合入 master)提供的 `PAL_HashSprite`(FNV-1a 64,对去前缀 RLE 内容)作为资源身份。
- 版权说明:本管线只处理使用者本人拥有的正版游戏数据,仅供个人使用;不分发任何软星(SoftStar)版权美术,超分产物同样不可分发。工具默认本地运行,不上传素材。

---

## 1. 背景与动机

子项目 A 证明了双平面高清渲染管线可行,但运行时用的是**最近邻占位放大**,没有真正的画质提升。真正的高清来自离线 AI 超分的美术资源。子项目 B 建立这条离线管线,第一版聚焦**人物头像(`RGM.MKF`)**:视觉冲击大、数量有限、调色板稳定,是端到端跑通管线并验证"AI 超分对这套 1995 像素美术是否好看"的最低风险目标。

已确认头像的运行时绘制路径为 `PAL_RLEBlitToSurface`(状态页头像 `uigame.c:1132`;对话头像 `text.c:1285/1330`),它委托到 `PAL_RLEBlitToSurfaceWithShadow(..., bShadow=FALSE)` —— 正是子项目 A 拦截并记录的 plain 路径。因此头像已被 A 的身份机制覆盖,C 可按内容哈希查表叠加,无需新增钩子。

## 2. 目标与非目标

### 2.1 目标
1. 一条离线两段式管线:抽取器(C)+ 超分脚本(shell),产出按内容哈希索引的 4× 高清 RGBA + manifest。
2. 抽取器算出的哈希与运行时 `PAL_HashSprite` **完全一致**(单元测试锁死)。
3. 先在小样本上选定超分模型,确认画质后再批量跑全部 RGM 头像。

### 2.2 非目标(明确推迟)
- 其他 `.mkf`(战斗背景 FBP、场景/角色/敌人精灵)→ 后续子项目。
- C 的运行时高清加载与叠加替换 → 子项目 C。
- 增量/缓存优化、GUI、非 RGM 资源。

## 3. 架构:两段式

```
RGM.MKF ──[抽取器 hd_extract (C)]──> 源 PNG(RGBA) + manifest.json
源 PNG ──[hd_upscale.sh -> realesrgan-ncnn-vulkan]──> hd_assets/<hash>.png (4× RGBA)
```

- **阶段一 抽取器** `tools/hd_extract`(C 可执行):读 `RGM.MKF`,逐 chunk 解码 → 用参考调色板上色 → 写 PNG(带 alpha);对每个 chunk 的原始 RLE 字节算 `PAL_HashSprite`;产出 `manifest.json`。
- **阶段二 超分脚本** `tools/hd_upscale.sh`:遍历源 PNG,调 `realesrgan-ncnn-vulkan -s 4 -n <model>`,输出到 `hd_assets/<hash>.png`。

两段解耦:抽取器只依赖引擎代码;超分只依赖外部二进制。manifest 是二者与子项目 C 的契约。

## 4. 最关键约束:哈希一致性

C 运行时以 `PAL_HashSprite`(去 `0x02` 前缀后对 宽高头+RLE 数据 做 FNV-1a 64)识别精灵。**抽取器必须对同一批字节算出同一个哈希**,否则 C 查不到高清图。

做法:抽取器**直接复用引擎 `palcommon.c`** 的 `PAL_MKFReadChunk` / `PAL_MKFGetChunkCount` / RLE 解码 / `PAL_HashSprite`。子项目 A 已证明 `palcommon.c` 可独立编译(`tests/run-standalone.sh` 用同样方式链接它)。抽取器沿用相同的独立编译/链接方式(include 路径 + SDL3 framework headers + 少量符号 stub)。

**验收**:抽取器对 `RGM.MKF` 若干 chunk 算的哈希,必须等于对同样字节调用 `PAL_HashSprite` 的结果(单元测试直接比对)。

## 5. 调色板烘焙

头像在**标准调色板(索引 0,非夜晚)**下绘制。抽取器:
1. 从 `PAT.MKF` 读取 0 号调色板(256×RGB)。为控制依赖面,抽取器**直接读 PAT.MKF 的 0 号 chunk 解析调色板**,不拉入 `palette.c` 的其余逻辑(除非必要)。
2. 解码 RLE 得到索引缓冲 + 覆盖掩膜。
3. 索引 → 0 号调色板 RGB;未覆盖像素 alpha=0,覆盖像素 alpha=255。
4. 写 RGBA PNG(复用仓库已有的 stb:`stb_image_write`)。

高清图颜色因此是烘焙死的。运行时的全局调色板特效(淡入淡出)由子项目 C 在高清平面层面以 RGB 变换实现;头像基本在稳定调色板下出现,烘焙 0 号调色板是正确的。manifest 记录 `palette:0` 以备后续。

## 6. 透明度处理

头像含透明区(RLE 透明游程 → alpha 0)。超分需保留干净的 alpha 边:
- 首选:`realesrgan-ncnn-vulkan` 直接处理 RGBA(其对 alpha 通道单独超分)。
- 备选:若出现白边/黑边,改为 RGB 与 alpha 掩膜分别 4× 再合并。
选型 spike 阶段确认哪种方式边缘干净。

## 7. 输出契约(供子项目 C)

- 目录:`hd_assets/`(与游戏数据同级,例如 `~/PAL/hd_assets/`)。
- 文件:`hd_assets/<16位十六进制哈希>.png`,4× RGBA。
- `manifest.json`:数组,每条
  ```json
  { "hash": "0x…", "src": "src/<hash>.png", "w": 48, "h": 55,
    "hd_w": 192, "hd_h": 220, "scale": 4, "palette": 0, "mkf": "rgm", "chunk": 12 }
  ```
- C 端用 `stb_image` 加载 `hd_assets/<hash>.png`;命中则 blit 高清 RGBA,否则回退 A 的占位路径。(C 的实现属子项目 C;B 只固定契约。)

## 8. 先选型(降风险第一步)

1. 抽取器先产出全部 RGM 源 PNG。
2. 挑 ~8 张代表性头像,用 2–3 个模型各跑一遍:`realesrgan-x4plus-anime`、`realesr-animevideov3`、`realesrgan-x4plus`。
3. 并排目检,选边缘/风格最合适者;确认 alpha 方式。
4. 选定后再用 `hd_upscale.sh` 批量跑全部。
若所有模型都把像素画糊坏 → 回退选项:高质量非 AI 放大(如 xBRZ)也能接进同一契约。

## 9. 范围边界(YAGNI)

- **做:** `tools/hd_extract`(RGM→PNG+manifest+哈希)、`tools/hd_upscale.sh`、选型、批量产物。
- **不做:** 其他 mkf、C 运行时加载、增量/缓存、GUI、把工具接进 Xcode 工程(独立编译即可)。

## 10. 成功标准

1. `hd_extract` 跑完 `RGM.MKF`,产出全部头像源 PNG + `manifest.json`。
2. 抽取器算的哈希与 `PAL_HashSprite` 对若干实测样本一致(单元测试通过)。
3. 选型后 `hd_upscale.sh` 产出 4× 高清 RGBA,透明边干净。
4. 使用者并排看过样张,确认 AI 超分值得继续。

## 11. 测试策略

- **单元(独立 gtest,复用 `tests/run-standalone.sh` 的链接方式):**
  - 抽取器对已知 RLE 样本算的哈希 == `PAL_HashSprite(样本)`。
  - 解码产出的像素数 == 宽×高;覆盖掩膜与 RLE 语义一致。
  - PNG 尺寸 == 精灵宽高。
- **集成/人工:** 对 `RGM.MKF` 实跑,抽查若干 chunk 的哈希与源 PNG;样张并排目检。

## 12. 影响/新增文件(预估)

- 新增 `tools/hd_extract.c` —— 抽取器主程序(读 RGM/PAT、解码、上色、哈希、写 PNG、写 manifest)。
- 新增 `tools/hd_upscale.sh` —— 超分批处理脚本。
- 新增 `tools/hd_extract_build.sh` —— 独立编译抽取器(仿 `tests/run-standalone.sh` 的 include/framework/stub 方式)。
- 复用 `palcommon.c`(MKF/RLE/哈希)、`stb_image_write.h`(PNG 输出;若仓库无写库则新增单文件 stb_image_write)。
- 可能新增极小 stub(satisfy `palcommon.c` 未用到的外部符号,同 `tests/standalone-stubs.c`)。

## 13. 风险与待定

1. **AI 糊像素画**:选型 spike 提前暴露;有 xBRZ 回退。
2. **alpha 白边**:spike 确认 RGBA 直超 vs 掩膜分离。
3. **抽取器依赖面**:目标只依赖 `palcommon.c` + 直接读 PAT.MKF 0 号调色板;若解析调色板需引擎更多代码,再评估是否引入 `palette.c` 的最小子集。
4. **stb_image_write**:已确认仓库无 PNG 写库(只有读:`stb_image.h`/`SDL_stbimage.h`)。B 引入单头文件 `stb_image_write.h`(公有领域,无版权顾虑)。调色板已确认来自 `pat.mkf`(见 `palette.c:53` `PAL_GetPalette`),抽取器可直接读 `pat.mkf` 解析 0 号调色板。
5. **realesrgan-ncnn-vulkan 安装**:brew 或官方发行二进制;Apple Silicon 走 Metal/MoltenVK。

## 14. 后续衔接

- **子项目 C**:运行时按 manifest/`hd_assets` 加载,替换 A 的占位放大;全局特效(淡变)在高清平面做 RGB 变换;先接头像,再扩展到背景/精灵。
- **后续 B 扩展**:同一抽取器泛化到 FBP(战斗背景)、MGO/ABC/F(精灵),复用哈希契约。
