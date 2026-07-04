# 规则文件 AGENTS.md 新增“围绕 Development_Plan.md 规则开发”的硬性规约 —— Analysis_Modification_Plan-125.md

## 1. 任务背景
在实际的人机协作开发中，AI 常常会脱离开发者的主导，进行无视原始语意的盲目开发、脑补或偏离规划的过度设计（如引入带有 I/O 穿透的“伪内存加速”）。这直接导致项目在非必要的问题上反复浪费时间且进度停滞。为了建立“步步为营、严防扩权”的最高安全防线，需要将项目规约文档 `Development_Plan.md` 提升为“项目宪法”级地位，约束 AI 的一切设计与审计行为。

## 2. 问题定位
当前 [AGENTS.md](file:///g:/C++/ArcMeta/ArcMeta/AGENTS.md) 规则中，虽然确立了 Jules 的“纯分析师角色”以及“禁止直接修改代码”等一系列红线，但尚未对项目演进核心路线图 [Development_Plan.md](file:///g:/C++/ArcMeta/ArcMeta/Analysis_Modification_Plan/Development_Plan.md) 的强制服从关系进行明确定义，缺乏一道对偏离规划行为的拦截铁律。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 开发应用必须围绕Development_Plan.md里的开发计划/规则来进行开发（对应用户原话） | 4.1 拟定追加至 AGENTS.md 的核心规约条文 | ✅ |
| 2    | 只要违背了Development_Plan.md的开发计划，则必须修正（对应用户原话） | 4.2 拦截与自我纠偏执行逻辑 | ✅ |

## 4. 详细解决方案

### 4.1 拟定追加至 AGENTS.md 的核心规约条文
建议将以下规约条文手动追加至项目根目录下 [AGENTS.md](file:///g:/C++/ArcMeta/ArcMeta/AGENTS.md) 的“**八、禁止行为清单**”以及“**九、Jules 的自检 Checklist**”中：

#### 追加至“八、禁止行为清单”（新增项）：
> **[开发计划绝对依从红线]**：AI 编写的所有方案设计、架构分析与问题审计，必须强制绝对围绕 `Analysis_Modification_Plan/Development_Plan.md` 中定义的开发计划与规则展开。**严禁**提出任何偏离、超越或违背该计划的方案逻辑；任何与该计划相抵触的设计，一经发现必须立即予以废弃、拦截并退回修正。

#### 追加至“九、Jules 的自检 Checklist”（新增检查项）：
> - [ ] 我已核对本次输出方案的所有细节，确认它们与 `Development_Plan.md` 中规定的核心开发计划/规则保持绝对一致，没有任何违背或偏离。

### 4.2 拦截与自我纠偏执行逻辑
- 当用户在探讨阶段或方案评审中指出方案与 `Development_Plan.md` 存在偏差时，Jules 必须立即停止推进，无条件推翻违规方案。
- 结合“〇.1 禁止内置话术”规则，Jules 将直接不带任何道歉和检讨话术，回溯到最近一个符合 `Development_Plan.md` 约定的步骤，重新对齐规约进行设计与输出。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] 拟定 [AGENTS.md](file:///g:/C++/ArcMeta/ArcMeta/AGENTS.md) 规约的追加条文文案。
- [ ] 同步更新 [Development_Plan.md](file:///g:/C++/ArcMeta/ArcMeta/Analysis_Modification_Plan/Development_Plan.md)。

**明确禁止越界修改的范围：**
- [ ] 根据 [AGENTS.md](file:///g:/C++/ArcMeta/ArcMeta/AGENTS.md) 自带的最高红线限制（*“本文件由用户授权生成，Jules 不得自行修改本文件的任何内容”*），**严禁** AI 试图使用任何文件修改工具直接篡改或写入 `AGENTS.md`。

## 6. 实现准则与预警【核心】
- **预警**：该规则一经用户手动添加，便成为 Jules 的最高审查指令之一。后续任何话题下的 `Analysis_Modification_Plan-N.md` 方案在进入 Step 1 理解及 Step 3 撰写时，都必须把“不违背 `Development_Plan.md`”放在首位。
- **操作建议**：请用户手动复制第 4.1 节拟定的追加条文，粘贴至 [AGENTS.md](file:///g:/C++/ArcMeta/ArcMeta/AGENTS.md) 文件的对应章节中。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 全局行为规则修改 | 暂无现有 Memories.md 针对此项的特定规范 | ✅ （已在方案中显式标注建议用户后续补充） |

## 8. 待确认事项（可选）
- 无。
