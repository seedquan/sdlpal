# 子项目 C —— HD 资源运行时接入(RGM 头像首期)设计文档

- 日期:2026-08-10
- 状态:已批准,待实现计划
- 所属大工程:SDLPal 真·高清重制引擎(个人自用)
- 前置:
  - 子项目 A(已合入 master):双平面渲染核心;`VIDEO_HD_Present` 叠加循环用 `PAL_HDRenderSprite` 做最近邻占位放大;`HDDRAWCMD` 含 `hash` 字段;`gConfig.fHDRemaster` 开关;`PAL_HDInvalidateCommands` 在精灵释放点清空命令。
  - 子项目 B(已合入 master):离线管线产出 `hd_assets/<16位哈希>.png`(4× RGBA)+ `manifest.json`,哈希与运行时 `PAL_HashSprite` 一致。资源存放于本地 `~/PAL/hd_assets/`。
- 版权说明:仅处理使用者本人拥有的正版数据与其本地超分产物,个人使用,不分发。

---

## 1. 背景与动机

A 建立了高清渲染管线但只有占位放大;B 离线产出了高清头像。C 把二者接上:运行时按内容哈希查表,命中就贴高清图,让头像在游戏里**真正以高清显示**——这是整个大工程第一次肉眼可见的画质提升。查表机制以哈希为键,天然通用:日后 B 扩展出的背景/精灵高清图无需改 C 即可生效。

关键前提已确认(见 `video.c` 的 `VIDEO_HD_Present`):叠加循环已持有 `cmds[c].hash`,当前对每个 plain 命令调用 `PAL_HDRenderSprite(cmds[c].sprite, gpPalette->colors, HD_SCALE, …)`。C 只在其前面加一次哈希查表。运行时已内置 stb_image(`video.c` 已用 `STBIMG_Load`),加载 PNG 无需新依赖。

## 2. 目标与非目标

### 2.1 目标
1. 新增惰性 哈希→RGBA 缓存模块 `hdassets.c/.h`。
2. 在 `VIDEO_HD_Present` 叠加循环加一处查表:命中贴高清图,未命中回退占位。
3. 用"调色板闸门"实现 v1 特效回退:仅当实时调色板等于烘焙参考(0 号)时用高清图,否则该帧回退占位。
4. 复用 `HDRemaster`;缺资源优雅降级;`HDRemaster=0` 零回归。

### 2.2 非目标(明确推迟)
- per-pixel 淡变/特效数学(v1 用调色板闸门整帧回退,不做逐像素)。
- 背景/角色/敌人精灵资源(等 B 扩展;查表通用,届时自动生效)。
- 纹理图集、预加载、增量热重载、GUI。

## 3. 架构

### 3.1 资源缓存模块 `hdassets.c/.h`
```c
void HDAssets_Init(const char *dir);   /* dir 默认 "hd_assets";清空缓存,缓存参考调色板 */
void HDAssets_Free(void);              /* 释放所有缓存 RGBA */
/* 命中:返回 0,*outRGBA 指向缓存的 4× RGBA8(w*h*4 字节),*outW/*outH 为高清尺寸。
   未命中:返回 -1。首次请求某 hash 时惰性 stbi_load "<dir>/<016llx>.png";
   命中与未命中都缓存(负缓存),之后同一 hash 不再触碰磁盘。 */
INT  HDAssets_Get(uint64_t hash, const uint8_t **outRGBA, INT *outW, INT *outH);
/* v1 特效闸门:当前实时调色板是否等于烘焙参考(0号日间)。TRUE 才允许用高清图。 */
BOOL HDAssets_PaletteMatchesReference(const SDL_Color *live);
```
- 缓存实现:简单哈希表或有序数组 + 二分;条目 `{hash, uint8_t *rgba(或NULL表负缓存), int w, int h, bool loaded}`。
- 参考调色板:`HDAssets_Init` 里调 `PAL_GetPalette(0, FALSE)` 复制 256 色(只存 r/g/b)。

### 3.2 `VIDEO_HD_Present` 接入
在叠加循环内,对每个 `plain` 命令:
1. 若 `gPaletteMatchesRef`(本帧起始算一次:`HDAssets_PaletteMatchesReference(gpPalette->colors)`)为真,且 `HDAssets_Get(cmds[c].hash, &rgba, &ow, &oh) == 0` → 贴 `rgba`(在 `cmds[c].x*HD_SCALE / cmds[c].y*HD_SCALE`,1:1,仅 alpha>0 的像素)。
2. 否则 → 现有 `PAL_HDRenderSprite` 占位路径(不变)。
- 每帧只算一次调色板比较(`gPaletteMatchesRef`),不在每个命令里重复。

### 3.3 生命周期
- `HDAssets_Init("hd_assets")` 在视频子系统启动、且 `gConfig.fHDRemaster` 为真时调用一次(`VIDEO_Startup` 或首次 `VIDEO_HD_Present`)。
- `HDAssets_Free()` 在 `VIDEO_Shutdown`。

## 4. 特效处理(v1 调色板闸门)

高清图颜色烘焙自 0 号日间调色板。运行时用改 `gpPalette` 实现淡入淡出/黑夜/中毒等全局效果。v1 不逐像素补偿,而是:
- 每帧比较 `gpPalette->colors` 与缓存的参考(256 色 r/g/b memcmp,极廉价)。
- **相等** → 无全局特效 → 用高清图。
- **不等** → 有特效(或非标准场景调色板)→ 整帧叠加回退占位放大(占位随实时调色板上色,颜色永远正确)。

好处:永不出现"高清图不跟特效"的错配。代价:头像出现在非标准调色板场景时回退占位(v1 已知限制,记录于 §9)。A 的 `PAL_HDInvalidateCommands` 已在场景切换/战斗结束清空命令,进一步降低"特效中残留高清叠加"的可能。

## 5. 配置

- 复用 `gConfig.fHDRemaster`(总开关)。
- **v1 硬编码资源目录 `"hd_assets"`**(相对游戏工作目录;运行时 cwd 即游戏数据目录)。不新增 `HDAssetPath` 配置项(YAGNI,留待后续),因此 v1 **不改 `palcfg.c/.h`**。
- 资源目录或某张图缺失 → `HDAssets_Get` 未命中 → 占位,优雅降级。

## 6. 加载与内存

- 惰性:仅当某 hash 首次出现在叠加命令中才 `stbi_load`;之后走缓存。
- 负缓存:未命中也记一条 `{hash, rgba=NULL}`,避免每帧重复 stat/load。
- 内存上限:88 张 4× RGBA 约 44MB(实际仅加载出现过的);`HDAssets_Free` 释放。

## 7. 测试策略

- **单元(独立 gtest,复用 `tests/run-standalone.sh` 链接方式,无需游戏数据):**
  - 用 B 的 `HDX_WritePNG` 写一张已知内容、以某 hash 命名的合成 PNG 到临时目录 → `HDAssets_Init(临时目录)` → `HDAssets_Get(hash)` 命中,`*outW/*outH` 与写入尺寸一致,像素可核对。
  - `HDAssets_Get(不存在的hash)` 返回 -1(负缓存)。
  - 再次 `HDAssets_Get(hash)` 命中且返回同一指针(走缓存,未重复加载)。
  - `HDAssets_PaletteMatchesReference`:相同调色板返回 TRUE,改一个颜色返回 FALSE。(参考调色板在测试里用 stub / 直接注入,避免依赖 pat.mkf。)
- **集成/人工:** `HDRemaster=1` + `~/PAL/hd_assets` 存在 → 进对话/状态页,头像明显更清晰;把 `hd_assets` 改名移走 → 头像回退占位(仍可玩);`HDRemaster=0` → 经典路径,与改动前逐像素一致(零回归)。

## 8. 影响/新增文件(预估)

- 新增 `hdassets.c` / `hdassets.h` —— 缓存 + 查表 + 调色板闸门 + 参考调色板。
- 修改 `video.c` —— 底部 `#include "hdassets.c"`(unity 方式,见下);`VIDEO_HD_Present` 加查表与每帧闸门;`VIDEO_Startup`/`VIDEO_Shutdown` 加 Init/Free(经 `hdassets.h` 声明)。
- 修改 `tests/run-standalone.sh` + `tests/test_rleblit.cpp` —— hdassets 单元测试。

**Xcode 工程零改动方案(已定,不再留待定):** 新文件 `hdassets.c` **不加入 Pal app target**;而是在 `video.c` 末尾 `#include "hdassets.c"`,使其作为 `video.c` 翻译单元的一部分被 Pal target 编译——沿用 A/B "不动 pbxproj" 的一贯做法。独立 harness 则把 `hdassets.c` 当作**自己的**一个 TU 单独编译链接(同 `hd_extract_core.c`)。两个二进制各有一份,互不冲突(无 ODR 问题)。依赖:`stbi_load` 在 app 侧由现有 stb 实现提供(`video.c` 已用 `STBIMG_Load`),在 harness 侧由 B 引入的 `STB_IMAGE_IMPLEMENTATION` TU 提供;`PAL_GetPalette` 在 app 侧现成,harness 侧对参考调色板测试用注入/stub 规避 pat.mkf 依赖。

## 9. 风险与待定

1. **对齐**:高清图尺寸须恰为命令精灵的 `w*4 × h*4`。B 已保证(尺寸校验通过);越界有现成 `dxv/dyv` 守卫。若某图尺寸不符,按守卫裁剪,不崩。
2. **非标准调色板场景回退**:头像在非 0 号调色板场景显示时,闸门判不等 → 回退占位。v1 已知限制,可接受(头像多在标准调色板的对话/状态页)。
3. **参考调色板取用时机**:`PAL_GetPalette(0, FALSE)` 需 pat.mkf 已可读(游戏启动后成立)。`HDAssets_Init` 放在资源就绪后调用。
4. **Xcode 工程接入**:见 §8,优先并入 `video.c` 规避 pbxproj 手术。
5. **调色板 alpha 字段**:比较只比 r/g/b(`gpPalette` 与 `PAL_GetPalette` 的 a 可能不同),避免误判。

## 10. 成功标准

1. 单元:命中/未命中/缓存复用/调色板闸门 全绿(合成 PNG,无游戏数据)。
2. 集成:`HDRemaster=1` + 资源就位时,游戏内头像可见地更高清;移走资源回退占位仍可玩;`HDRemaster=0` 零回归。
3. **这是整个大工程第一次"看得见"的高清效果。**

## 11. 后续衔接

- **B 扩展**:同一抽取器泛化到 FBP(战斗背景)、MGO/ABC/F(精灵)→ 产出更多 `hd_assets/<hash>.png`,C 的查表**无需改动**即可生效。
- **C 后续**:如需特效下也保持高清,再引入逐像素/着色器色彩变换(超出 v1)。
