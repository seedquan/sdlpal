# 战斗背景高清化(FBP 抽取 + 运行时合成)设计文档

- 日期:2026-08-10
- 状态:已批准,待实现计划
- 所属大工程:SDLPal 真·高清重制引擎(个人自用)
- 前置:
  - 子项目 A/B/C(已合入 master):双平面渲染核心、离线超分管线(RGM 头像)、运行时按内容哈希查表叠加(`hdassets.c`)。
  - `HDAssets_Get(hash)` 按 `hd_assets/<016x>.png` 惰性加载 4× RGBA;查表通用,与资源类型无关。
- 版权说明:仅处理使用者本人正版数据及其本地超分产物,个人使用,不分发。

---

## 1. 背景与动机

头像高清化已在游戏内生效。下一步是**战斗背景(FBP.MKF)**。关键机制(已确认):

- 战斗背景是 `FBP.MKF` 里的**单个整屏 chunk**,在 `PAL_LoadBattleBackground`(`battle.c:988`)用 `PAL_MKFDecompressChunk(buf, 320*200, wNumBattleField, fpFBP)` 直接**解压**成一块 320×200 的 **8-bit 平铺位图**(64000 字节,非 RLE)。之后 `PAL_FBPBlitToSurface(buf, lpBackground)` 铺到离屏背景面。
- 背景经**整面原始拷贝**(`VIDEO_CopyEntireSurface(lpSceneBuf, gpScreen)`)进 `gpScreen`,战斗精灵/菜单/伤害数字再画在其上。这条整面拷贝**不是**被记录的 RLE 绘制,所以子项目 C 的“逐精灵哈希叠加”机制看不到它——需要新机制。
- `battle.c:735` 显示战斗在**当前场景调色板**(`gpGlobals->wNumPalette` / `fNightPalette`)下显示背景;同一 FBP 背景可能在不同场景调色板下出现。

## 2. 目标与非目标

### 2.1 目标
1. **离线抽取**:把 `FBP.MKF` 每个背景 chunk 解压 → 上色 → PNG + 哈希 → AI 4× 超分,产出高清背景资源。
2. **运行时显示**:战斗时按背景内容哈希查表;命中则用高清背景合成(新“逐像素差分”模式),让战斗里看得到高清背景。

### 2.2 非目标(v1 明确推迟)
- 战斗精灵/菜单/HP 数字/伤害数字高清(仍占位放大)。
- 动画特效(水/火在背景上的动画帧)高清(它们改动背景,走占位)。
- 多调色板精确配色(v1 烘焙单一参考调色板,见 §5)。

## 3. 架构

### 3.1 资源身份:`PAL_HashBytes`
新增 `uint64_t PAL_HashBytes(const void *data, size_t len);`(FNV-1a 64,对任意平铺字节)。战斗背景对**解压后的 64000 字节索引数据**求哈希。抽取器与运行时对**同一批字节**求同一哈希 → 天然匹配(与头像同一契约思路,但头像用 `PAL_HashSprite` 走 RLE 格式,背景走平铺 `PAL_HashBytes`)。

### 3.2 Part 1 —— FBP 离线抽取
新增/扩展抽取器(`tools/hd_extract_fbp` 或复用 `hd_extract` 加一个模式):
1. 打开 `fbp.mkf`;对每个 chunk `PAL_MKFDecompressChunk` → 64000 字节平铺索引。
   - **构建要点**:`PAL_MKFDecompressChunk` 依赖全局 `Decompress` 函数指针。头像抽取用的是**未压缩**读取(`PAL_MKFReadChunk`),把 `Decompress` stub 成 NULL 即可;FBP 是**压缩**的(DOS 版为 YJ1),因此 FBP 抽取器必须**链接 `yj1.c` 并设 `Decompress = YJ1_Decompress`**。
2. 用参考调色板(§5)把 64000 个索引上色成 320×200 RGBA(平铺,无 RLE 解码)。
3. `HDX_WritePNG` 写源 PNG(命名用 `PAL_HashBytes` 的 16 位十六进制);写进 manifest(`{hash, mkf:"fbp", chunk, w:320, h:200, ...}`)。
4. 复用 `tools/hd_upscale.sh` + `realesrgan-x4plus-anime` 批量 4× → `hd_assets/<hash>.png`(与头像同一输出契约与目录)。

### 3.3 Part 2 —— 运行时身份 + 合成
**身份(battle.c):** 在 `PAL_LoadBattleBackground` 解压后(`battle.c:988` 之后),把 `buf`(64000 索引)复制进全局 `g_hdBattleBg[64000]`,并算 `g_hdBattleBgHash = PAL_HashBytes(buf, 64000)`,置 `g_hdBattleBgActive = TRUE`。战斗结束(`PAL_EndBattle` / 退出战斗)清 `g_hdBattleBgActive = FALSE`。

**合成(video.c `VIDEO_HD_Present`,逐像素差分):** 当 `g_hdBattleBgActive` 且 `HDAssets_Get(g_hdBattleBgHash)` 命中时,底层不再简单放大 `gpScreenReal`,而是逐像素判定:
```
对每个高清像素 (dx,dy),源坐标 sx=dx/4, sy=dy/4:
  若 gpScreen 索引[sy*320+sx] == g_hdBattleBg[sy*320+sx]  // 该像素仍是背景,未被覆盖
      → 用高清背景像素 HDbg[dy*1280+dx]
  否则(被精灵/菜单/特效覆盖)
      → 用放大后的 gpScreenReal 像素(占位)
```
这样得到**清晰的高清背景 + 其上一切照旧(占位)**,无需追踪画了什么。精灵叠加层(头像等)仍照常在其上绘制。

**边界/回退:** 未命中或非战斗 → 走现有普通底层放大,零变化。

## 4. 关键取舍与限制

- **静态背景**:`g_hdBattleBg` 存的是解压后的基础背景;动画特效(水/火)会改动 `gpScreen` 对应像素 → 差分判为“被覆盖” → 那些像素走占位(占位=放大,略糊)。可接受 v1。
- **精灵/UI 占位**:战斗精灵、菜单、数字都走差分的“被覆盖”分支 = 占位放大(不高清)。只有背景高清。
- **配色(§5)**:高清背景颜色烘焙自单一参考调色板;实际战斗调色板不同,可能有轻微偏色。

## 5. 调色板

战斗在场景调色板(`wNumPalette`,可为夜晚)下显示背景。抽取器离线无战斗上下文,**v1 烘焙 0 号日间调色板**(与头像一致)。已知限制:在使用非 0 号调色板的战斗中,高清背景色相可能与周围占位元素略有差异。后续改进方向:按 (chunk×调色板) 多份烘焙,或运行时色彩变换(超出 v1)。

## 6. 复用与新增

- **新增** `PAL_HashBytes`(`palcommon.c/.h`)。
- **新增** FBP 抽取(`tools/hd_extract_fbp.c` + 构建脚本链接 `yj1.c`,设 `Decompress`)。
- **修改** `battle.c`:`PAL_LoadBattleBackground` 存背景+哈希+active;战斗结束清 active。
- **修改** `video.c`:`VIDEO_HD_Present` 增加逐像素差分底层(仅当战斗高清背景命中时)。
- **复用** `hdassets.c`(按哈希查表加载 `hd_assets/<hash>.png`)、`stb_image_write.h`、`tests/run-standalone.sh` 独立 harness、`realesrgan` 超分脚本。
- 运行时读取 `g_hdBattleBg` 的全局需 `video.c` 可见(声明放 battle 相关头或 `global.h`)。

## 7. 测试策略

- **单元(独立 harness,无游戏数据):**
  - `PAL_HashBytes`:同字节同哈希、异字节异哈希、空指针/0 长度处理。
  - FBP 平铺解码上色:给定 64000 合成索引 + 合成调色板 → RGBA 正确(抽取器核心函数)。
  - 逐像素差分逻辑:给定小尺寸 gpScreen 索引 / 背景索引 / HDbg / 放大源,验证“相等取 HD、不等取占位”的选择正确(把该判定抽成纯函数 `HD_BattleBgPixel(...)` 便于测)。
- **集成/人工:** FBP 抽取器对 `~/PAL/fbp.mkf` 实跑 → 产出背景 PNG + manifest;超分;进一场战斗,肉眼确认背景高清、精灵/菜单占位、无崩溃;`HDRemaster=0` 零回归。

## 8. 成功标准
1. 单元:哈希/解码/差分全绿(合成数据)。
2. 抽取器跑完 `fbp.mkf`,产出背景源 PNG + manifest,哈希与运行时 `PAL_HashBytes` 对若干实测样本一致。
3. 进战斗肉眼可见高清背景;`HDRemaster=0` 与改动前一致。

## 9. 风险与待定
1. **YJ1/YJ2 版本**:已确认 `YJ1_Decompress` 与 `YJ2_Decompress` 均在 `yj1.c`(声明于 `palcommon.h:306/313`),`global.c:205` 按 `fIsWIN95` 选择。抽取器链接 `yj1.c`,DOS 数据设 `Decompress = YJ1_Decompress`(用户当前为 PAL_DOS)。
2. **战斗结束清理点**:准确找到战斗退出处清 `g_hdBattleBgActive`,避免非战斗帧误用背景哈希。
3. **性能**:逐像素差分是每帧再走一遍 ~1M 像素比较+取值(与现有底层放大同量级),战斗中约 2× 底层开销;Apple Silicon 预期可行,战斗场景实测。
4. **配色**(§5):非 0 调色板战斗轻微偏色,v1 接受。

## 10. 后续衔接
- 战斗精灵(F/ABC/MGO)高清:走子项目 C 的逐精灵哈希叠加(精灵是被记录的 RLE 绘制,机制已具备),是独立后续。
- 多调色板精确配色:后续按需引入。
