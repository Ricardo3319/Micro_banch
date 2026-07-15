# IEEE INFOCOM 2027 Submission Guide for RescueSched

> **目标会议：** IEEE INFOCOM 2027
> **项目唯一主线：** *RescueSched: Deadline-Feasible Descriptor Migration for RSS-Sharded RPC Servers*
> **调研日期：** 2026-07-15
> **项目时区：** America/Los_Angeles（当前为 PDT，UTC−07:00）
> **文档性质：** 可审计的投稿规范与项目就绪度指南；不是会议主席或 IEEE 的替代通知。
> **重要原则：** 本文不把上一届规则、IEEE 通用规则或社区惯例冒充为 INFOCOM 2027 当届规则。

---

## 状态标记

本文每条关键规则使用以下状态之一：

- **CONFIRMED**：由 INFOCOM 2027 当届官方页面确认，或由当前 IEEE 官方政策确认。每处均注明适用范围是“INFOCOM 2027”还是“IEEE-wide”。
- **PROVISIONAL**：来自上一届或最近一届官方材料，仅作暂定参考；不得当作 INFOCOM 2027 已确认规则。
- **UNRESOLVED**：当届尚未发布、官方页面当前不可读取、官方来源之间存在未解决差异，或本次调研未找到足以支持结论的官方材料。

### 证据等级

- **官方规则**：具有强制性措辞的当届 INFOCOM/IEEE 官方页面或政策。
- **官方会议信息**：由 IEEE Communications Society 发布的日期、地点、会议组成和范围说明。
- **官方建议**：IEEE Author Center 的准备、开放科学或写作建议；除非另有强制措辞，不等同于 INFOCOM desk-reject 规则。
- **项目证据**：Micro_banch 仓库中的合同、代码说明、实验结果和复现实验计划。
- **经验性判断**：基于网络系统论文评审实践形成的项目建议，不是 INFOCOM 官方规则。
- **学术索引**：用于核对相关工作书目信息，不作为投稿规则依据。

---

# 1. Executive Summary

## 1.1 当前能确认的 INFOCOM 2027 事实

1. **[CONFIRMED | INFOCOM 2027]** IEEE ComSoc 官方页面列出的 **Call for Technical Papers Deadline 为 2026-07-31**。[O27-COMSOC]
2. **[UNRESOLVED]** 官方页面没有给出该截止日期的具体时刻、时区，也没有说明 2026-07-31 是摘要注册、完整论文上传，还是两者的共同截止点。不能假设为“当地时间 23:59”或“Anywhere on Earth”。
3. **[CONFIRMED | INFOCOM 2027]** 会议将于 **2027-05-24 至 2027-05-27** 在美国夏威夷州檀香山线下举行。[O27-COMSOC]
4. **[CONFIRMED | INFOCOM 2027]** INFOCOM 面向 networking 及密切相关领域，覆盖理论和系统研究；官方列出主技术会议、workshop、keynote、panel、student poster、demo/poster 等组成。[O27-COMSOC]
5. **[UNRESOLVED]** INFOCOM 2027 的详细 CFP、Author Information、EDAS/投稿系统规则、页数、匿名方式、补充材料、rebuttal、camera-ready 和 PDF eXpress 细则，本次访问时尚未获得可读取的当届官方文本。年度官网已上线，但当前研究环境只能看到 JavaScript/WAF 提示。[O27-SITE]

## 1.2 RescueSched 的结论

**投稿方向判断：有明确 scope fit，但当前主会就绪度为“有条件且高风险”。**

RescueSched 的核心问题是：在 RSS 分片的多核 RPC server 中，某个排队请求在本地核上已预计无法满足 deadline，但迁移描述符到另一核后仍可按时完成时，是否应执行有界、付费的 descriptor migration。该问题直接涉及网络端系统、RPC 数据路径、RSS/per-core queue、SLO goodput 和尾延迟，最合理的身份是：

> **Networking systems / datacenter RPC systems 为主；RPC scheduling 为核心机制；resource scheduling 为次级方法标签。**

不要把论文包装为一个与网络无关的通用 CPU 调度器。引言、系统图和真机实验必须把 NIC/RSS、flow affinity、per-core receive/dispatch queues、RPC deadline 与 descriptor handoff 串成完整网络系统路径。

## 1.3 当前项目证据的强弱

**强项：**

- 当前分支明确把 `M1_RescueSched` 设为唯一论文主线；AQB/DQB 仅是历史实验，不能作为论文证据。[P-README]
- 论文合同已经把 novelty 收窄为“request-specific local-miss/remote-meet outcome-change selection”，并明确付出 handoff cost、使用非泄漏 EWMA、共享冻结 trace、报告负结果和边界。[P-CONTRACT]
- 修正版评估合同有强基线、公平控制、固定 seed、paired 95% interval 和 go/no-go gate。[P-EVAL]
- artifact provenance 已把 authoritative corrected experiment 与 legacy artifacts 分开，并要求 trace hash、命令、脚本、Git commit 和运行 manifest。[P-PROV]

**硬缺口：**

- 物理复现文档明确说明它是“计划而非证据”，并确认仓库尚无 physical trace replay loader；因此目前不能写“已在真实 RPC server/CloudLab 上验证”。[P-PHYS]
- 物理计划仍使用旧基线名 `L1_WorkStealing` 与 `M0_IntraHostProactive` 作为部分对齐标准，而最新评估合同规定强基线是 `L1_WorkStealingPolling` 与 `M0_AltoThreshold`。提交前必须统一。[P-EVAL][P-PHYS]
- 仓库中的 provenance 文档记录其写作时 worktree 为 dirty；提交证据必须迁移到 clean、immutable commit/tag 和只读归档。[P-PROV]

## 1.4 最重要的五个投稿风险

| 排名 | 风险                                                         |  严重性 | 立即措施                                                     |
| ---- | ------------------------------------------------------------ | ------: | ------------------------------------------------------------ |
| 1    | **没有真实 RPC/RSS 运行时证据，且 physical trace replay loader 尚不存在** | 致命/高 | 立即完成真机运行时、真实 descriptor handoff、强基线和同 trace replay；否则不得声称“系统已部署/验证”。 |
| 2    | **官方截止日期距调研日仅 16 天，且具体时刻/时区、摘要注册规则未发布或未读取** | 致命/高 | 把内部最终上传设为 2026-07-30 12:00 PDT；每天复查当届官网和投稿系统。 |
| 3    | **ALTOCUMULUS 已经是高度相近的 MICRO 2022 RPC scheduling 工作** |      高 | 完成逐机制 claim matrix；novelty 只能落在 request-specific deadline outcome change、remote feasibility、paid handoff 和目标侧安全，而不是“主动迁移 RPC”。 |
| 4    | **INFOCOM 2027 页数、模板、匿名、supplement、EDAS 和 desk-reject 细则未确认** |      高 | 使用未修改的 IEEE conference 模板准备；同时维护完全匿名稿和匿名 artifact；规则发布后立即做差异审计。 |
| 5    | **结果边界和仓库内部语义不一致可能造成过度声明或不可复现**   |      高 | 只使用 Step-21 corrected evidence；公开 W3 低负载失败、W2 tail regression；统一基线名、manifest、commit、图表与论文数字。 |

---

# 2. Source Status and Conference-Year Certainty

## 2.1 证据优先级与冲突解决

硬性规则按以下顺序裁决：

1. INFOCOM 2027 当届官网、CFP、Author Information、正式 FAQ；
2. INFOCOM 2027 官方投稿系统/EDAS 页面；
3. IEEE Communications Society 当届活动页；
4. IEEE Author Center、IEEE PSPB/IEEE Computer Society 的现行通用政策；
5. INFOCOM 2026 或最近一届的官方材料，只能标为 **PROVISIONAL**；
6. 社区经验、历史论文和本指南的系统论文判断，只能标为经验性判断。

当来源冲突时：

- 当届具体规则优先于 IEEE 通用默认；
- 更新日期更晚且发布机构更直接的来源优先；
- CFP/Author Information/投稿系统的具体字段优先于会议宣传页；
- 如果无法判断更新顺序，保留为 **UNRESOLVED**，不得自行折中。

## 2.2 INFOCOM 2027 来源可用性

| 信息项                                  | 状态           | 证据                                       | 结论                                                         |
| --------------------------------------- | -------------- | ------------------------------------------ | ------------------------------------------------------------ |
| 会议日期、地点、线下形式                | **CONFIRMED**  | IEEE ComSoc 2027 活动页 [O27-COMSOC]       | 2027-05-24 至 2027-05-27，Honolulu，线下。                   |
| Call for Technical Papers Deadline 日期 | **CONFIRMED**  | [O27-COMSOC]                               | 2026-07-31；时刻和时区未知。                                 |
| 年度官网是否存在                        | **CONFIRMED**  | [O27-COMSOC] 指向 [O27-SITE]               | 官网已上线。                                                 |
| 年度官网详细页面内容                    | **UNRESOLVED** | [O27-SITE]                                 | 本次环境只返回 JavaScript/WAF 提示，不能据此摘录格式规则。   |
| CFP/Author Information                  | **UNRESOLVED** | 未获得可读取当届原文                       | 不猜测。                                                     |
| EDAS/投稿系统页面                       | **UNRESOLVED** | 未找到可核验的 INFOCOM 2027 官方 EDAS 页面 | 不把 EDAS 当作已确认平台。                                   |
| 2026 详细 Author Information            | **UNRESOLVED** | 2026 年度站点同样受 JS/WAF 限制 [O26-SITE] | 仅能使用 ComSoc 2026 活动页作为历史背景，不能借此推定页数/匿名。 |

### 2.2.1 最近一届官方参考：INFOCOM 2026

由于 INFOCOM 2027 详细规则未能读取，本文按要求检查了最近一届官方来源。当前可审计地获得的 INFOCOM 2026 官方信息只有 IEEE ComSoc 活动页和年度官网的访问状态；因此下面仅作 **PROVISIONAL** 年际参考，不扩张为不存在的格式规则：

| INFOCOM 2026 官方信息                        | 历史值                                                       | 对 INFOCOM 2027 的用法                                       |
| -------------------------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| Call for Technical Papers Deadline           | 2025-07-31                                                   | **PROVISIONAL 历史背景**：说明 ComSoc 活动页可能只公布日期而不公布时刻；不能据此推定 2027 时区或 abstract/full-paper 语义。[O26-COMSOC] |
| Conference dates/location                    | 2026-05-18 至 2026-05-21，Tokyo                              | 仅用于确认该活动页的字段结构，不推算 2027 其他日期。[O26-COMSOC] |
| Scope                                        | networking 与 closely related areas；theoretical and systems research | 与 2027 当前页面一致，可作为范围稳定性的辅助证据，但 2027 结论仍直接依据当届页面。[O26-COMSOC][O27-COMSOC] |
| Program components                           | main technical program、workshops、keynote、panels、student poster、demo/poster | 只说明这些组成在相邻两届都被 ComSoc 列出；各 track 的 2027 资格、页数和截止仍未发布。[O26-COMSOC][O27-COMSOC] |
| 页数、模板、匿名、supplement、rebuttal、EDAS | 未从可读取的 2026 官方原文获得                               | **UNRESOLVED**；本文拒绝从博客、实验室网页或搜索摘要补齐。[O26-SITE] |

这意味着“参考最近一届”不等于“把最近一届常见做法写成规则”。在可读取的 2026 官方 Author Information/CFP 原文出现前，本文不会填写任何历史页数、匿名方式或截止时区。

## 2.3 项目材料读取状态

| 用户指定材料                                     | 分支/路径                     | 读取状态         | 在本指南中的处理                                             |
| ------------------------------------------------ | ----------------------------- | ---------------- | ------------------------------------------------------------ |
| `README.md`                                      | `codex/rescuesched-baselines` | 已读取           | 权威确认 RescueSched 是唯一论文主线，AQB/DQB 为历史材料。[P-README] |
| `新实验思想指导.md`                              | `main`                        | 已读取           | 仅用于算法思想、边界与指标设计；因分支与当前论文分支不同，不覆盖当前合同。[P-IDEA] |
| `docs/PAPER_CONTRACT_INFOCOM2027.md`             | `codex/rescuesched-baselines` | 已读取           | 当前 claim boundary 的主要项目依据。[P-CONTRACT]             |
| `docs/RESCUESCHED_EVALUATION_CONTRACT_V2.md`     | 同上                          | 已读取           | 当前强基线、公平性和 go/no-go gate 的主要依据。[P-EVAL]      |
| `docs/INFOCOM_READINESS.md`                      | `main` 与当前分支均测试       | **404 / 未找到** | 未使用，不推测其内容。[P-MISSING-READINESS]                  |
| `docs/ARTIFACT_PROVENANCE.md`                    | 当前分支                      | 已读取           | 用于证据溯源、legacy 隔离和 manifest 设计。[P-PROV]          |
| `docs/RESCUESCHED_PHYSICAL_REPRODUCTION_PLAN.md` | 当前分支                      | 已读取           | 明确是真机计划，不是完成证据。[P-PHYS]                       |
| `docs/PHYSICAL_MACHINE_RUNBOOK.md`               | `main` 与当前分支均测试       | **404 / 未找到** | 未使用，不推测其内容。[P-MISSING-RUNBOOK]                    |

## 2.4 项目主线裁决

**权威项目裁决：RescueSched 是唯一有效投稿主线。**

- `M1_RescueSched` 是 main method；
- 当前强基线是 `L0_RandomCore`、`L1_WorkStealingPolling`、`M0_AltoThreshold`；
- `B0_IdealCFCFS` 只能作为明确标注的非部署 upper bound；
- AQB、DQB、历史 one-shot work stealing 和 `M0_IntraHostProactive` 只能用于历史/诊断，不可写入 contribution 或主结果；
- 如果旧图、旧 CSV 或旧 README 与 Step-21 corrected evidence 冲突，以当前 paper/evaluation contract 与 corrected artifact 为准。[P-README][P-EVAL][P-PROV]

---

# 3. Important Dates

## 3.1 官方日期表

| 事项                               | 绝对日期/时刻                | 时区               | 状态                    | 依据与说明                                                   |
| ---------------------------------- | ---------------------------- | ------------------ | ----------------------- | ------------------------------------------------------------ |
| Abstract registration              | **未发布**                   | 未发布             | **UNRESOLVED**          | 未找到 INFOCOM 2027 当届官方规则。                           |
| Call for Technical Papers Deadline | **2026-07-31**               | **未发布**         | **CONFIRMED（仅日期）** | IEEE ComSoc 官方活动页。[O27-COMSOC]                         |
| Full-paper PDF upload deadline     | **未能与上项区分**           | 未发布             | **UNRESOLVED**          | 不能假设 2026-07-31 一定是 PDF 最终上传时刻。                |
| Supplementary material deadline    | **未发布**                   | 未发布             | **UNRESOLVED**          | 是否允许 supplement 也未确认。                               |
| Rebuttal / author response         | **未发布**                   | 未发布             | **UNRESOLVED**          | 不得按往届经验推算。                                         |
| Notification                       | **未发布**                   | 未发布             | **UNRESOLVED**          | 不得推算。                                                   |
| Camera-ready                       | **未发布**                   | 未发布             | **UNRESOLVED**          | 不得推算。                                                   |
| Copyright/eCF deadline             | **未发布**                   | 未发布             | **UNRESOLVED**          | IEEE 通常在接收后发 eCF，但 INFOCOM 2027 日期未知。[IEEE-COPYRIGHT] |
| Conference dates                   | **2027-05-24 至 2027-05-27** | 官方未在页面标时区 | **CONFIRMED**           | Honolulu，线下。[O27-COMSOC]                                 |

## 3.2 截稿时间解释

- **[UNRESOLVED]** 官方没有给出 `23:59`、AoE、UTC、EDAS server time、Hawaii time 或其他时区定义。
- 任何“INFOCOM 一贯按某时区”的说法在当届规则出现前都不能用于冒险上传。
- 项目内部应采用更早的硬截止：**2026-07-30 12:00 PDT** 完成最终上传和下载回验。该时间是项目风险控制，不是官方截止。
- 2026-07-31 只能作为最后应急窗口，不能安排主实验、作者确认或首次 PDF 检查。

## 3.3 需要重新检查的时间点与页面

| 检查日期（PDT） | 必查页面                 | 重点                                                   |
| --------------- | ------------------------ | ------------------------------------------------------ |
| 2026-07-15      | [O27-SITE]、[O27-COMSOC] | CFP/Author Information 是否发布；截止时刻/时区。       |
| 2026-07-17      | 同上；搜索官方投稿系统   | 摘要注册是否早于论文截止；EDAS/HotCRP/其他平台链接。   |
| 2026-07-20      | 同上                     | 页数、匿名、模板、supplement、COI。                    |
| 2026-07-24      | 同上                     | author response、伦理、artifact、必填字段。            |
| 2026-07-27      | 同上                     | 最终规则差异审计；必要时通过官网正式联系人询问尚缺项。 |
| 2026-07-29      | 投稿系统实际页面         | 字段、作者顺序、冲突、文件大小、checksum、提交状态。   |
| 2026-07-30      | 投稿系统实际页面         | 最终上传、下载回验、收据/确认邮件。                    |
| 2026-07-31      | 仅应急检查               | 不依赖未公布的“当天结束”解释。                         |

---

# 4. Scope and Track Fit

## 4.1 官方定位

**[CONFIRMED | INFOCOM 2027]** INFOCOM 被 IEEE ComSoc 描述为 networking 研究领域的顶级会议，接收 networking 及密切相关领域的重要、创新贡献，覆盖理论和系统研究。[O27-COMSOC]

该官方描述足以确认：

- networking systems 属于核心范围；
- datacenter networking / RPC systems 只要问题与网络数据路径、通信和端系统行为紧密相连，原则上在 scope 内；
- resource scheduling 若只是通用计算资源分配，scope fit 较弱；若调度对象是 RSS-sharded RPC、网络队列和 deadline/SLO，则可作为 networking systems 机制。

## 4.2 RescueSched 的最合适定位

| 维度     | 推荐定位                                    | 理由                                                         |
| -------- | ------------------------------------------- | ------------------------------------------------------------ |
| 一级领域 | **Networking systems**                      | 系统问题源于 RSS 分片、RPC receive/dispatch 路径和 per-core queue imbalance。 |
| 二级领域 | **Datacenter RPC systems**                  | 目标指标是微秒级/短 RPC 的 deadline violation、SLO goodput 与 tail behavior。 |
| 核心技术 | **SLO-aware RPC scheduling / queue repair** | RescueSched 根据 request-specific local miss 和 remote meet 决定 descriptor migration。 |
| 方法标签 | **Resource scheduling**                     | 可作为方法词，但不能成为唯一叙事，否则容易被视为通用 OS scheduler。 |
| 不宜主打 | 通用 multicore load balancing               | 会削弱 INFOCOM 的网络问题动机。                              |

建议题目和摘要持续保留 `RSS-Sharded RPC Servers`，并在第一页回答三件事：

1. NIC/RSS 为什么造成 per-core queue 的短时、不可由平均负载解释的不平衡；
2. 为什么通用 work stealing/threshold migration 对 deadline outcome 不够精确；
3. 为什么 descriptor migration 的网络系统成本、缓存/NUMA/控制周期不能忽略。

## 4.3 主会、workshop、demo、poster、artifact 的区别

| 形式                      | 官方已确认的存在性 | 可确认的定位                                                 | 2027 具体规则                                     |
| ------------------------- | ------------------ | ------------------------------------------------------------ | ------------------------------------------------- |
| Main technical program    | **CONFIRMED**      | 完整、重要、创新的研究贡献；RescueSched 的首选目标。[O27-COMSOC] | 页数、评审、匿名等 **UNRESOLVED**。               |
| Workshops                 | **CONFIRMED**      | 通常主题更聚焦，适合较早期、专项或边界研究；这是经验性描述。 | workshop 名称、CFP、出版与截止均 **UNRESOLVED**。 |
| Demo/poster sessions      | **CONFIRMED**      | Demo 强调可运行展示；poster 更适合精炼结果或在研工作；这是经验性描述。 | 2027 资格、页数、出版方式均 **UNRESOLVED**。      |
| Student poster            | **CONFIRMED**      | 官方确认有该 session。[O27-COMSOC]                           | 学生资格、材料与截止 **UNRESOLVED**。             |
| Artifact track/evaluation | **未确认**         | 本次未找到 INFOCOM 2027 独立 artifact track 规则。           | **UNRESOLVED**；不得声称主会强制 artifact。       |

## 4.4 Track 建议

- **首选：Main technical program**，前提是真机系统证据在提交前完成，并把 novelty 与 ALTOCUMULUS 切清。
- 若只能提供仿真、算法和边界分析，主会 systems claim 风险很高；此时 workshop 或 poster 可能更符合成熟度，但需等 2027 官方 CFP。
- 若完成了可运行的 RSS/RPC prototype，却论文证据尚不够完整，可同时评估 demo；不得假设 demo 与主会可同时投稿或共享稿件，须等官方规则。

---

# 5. Paper Format and Page Limits

## 5.1 当届格式状态

| 项目                     | INFOCOM 2027 状态                                       | 当前可执行要求                                               |
| ------------------------ | ------------------------------------------------------- | ------------------------------------------------------------ |
| 官方 LaTeX/Word 模板     | **UNRESOLVED（当届链接）**；IEEE 通用模板 **CONFIRMED** | 使用 IEEE 官方 conference template 起稿，不使用实验室自制模板。[IEEE-TEMPLATE] |
| `IEEEtran` 具体模式      | **UNRESOLVED**                                          | 不把任何 `documentclass` 参数写成 INFOCOM 2027 已确认规则。  |
| 双栏                     | **UNRESOLVED（INFOCOM 2027）**                          | 通用 IEEE conference template 通常提供会议版式；最终以当届规则为准。 |
| 字号                     | **UNRESOLVED**                                          | 不猜测 9pt/10pt。                                            |
| 纸张尺寸（US Letter/A4） | **UNRESOLVED**                                          | 同时避免依赖纸张边缘的图表；规则发布后统一。                 |
| 正文页数上限             | **UNRESOLVED**                                          | 不引用往届数字。                                             |
| References 是否计入页数  | **UNRESOLVED**                                          | 维护可独立压缩的 related work 与 appendix 素材。             |
| Appendix 是否允许        | **UNRESOLVED**                                          | 不把核心证明/实验放在 appendix 才能成立。                    |
| Supplementary material   | **UNRESOLVED**                                          | 在确认允许前，主张必须在主 PDF 自洽。                        |
| 文件大小/文件数量        | **UNRESOLVED**                                          | 等投稿系统页面。                                             |
| 页边距、栏宽、行距       | **UNRESOLVED（当届具体值）**                            | 保持官方模板默认，不手动修改。                               |
| 缩小字号/压行距/改模板   | **UNRESOLVED（desk-reject 措辞）**                      | 项目内部禁止；属于高风险版式规避。                           |

## 5.2 IEEE 通用模板与 PDF 合规

**[CONFIRMED | IEEE-wide]** IEEE Author Center 提供官方 conference Word/LaTeX 模板，并建议使用 IEEE 的 LaTeX/参考文献校验工具。[IEEE-TEMPLATE]

**[CONFIRMED | IEEE-wide, conditional on conference use]** IEEE PDF eXpress/Xplore 检查包括：

- PDF 版本要求；
- 非扫描 PDF；
- 字体全部嵌入或 subset；
- 无密码/安全限制；
- 无 bookmarks/links、crop marks、日期戳、附件、PDF package 或 merged PDF；
- 会议若不使用 PDF eXpress，可使用 IEEE PDF Checker。[IEEE-XPLORE]

注意：**INFOCOM 2027 是否在初稿或 camera-ready 使用 PDF eXpress 尚未确认。** PDF eXpress 通常是出版阶段工具，不能用它代替初稿页数、匿名或模板检查。

## 5.3 项目内部版式闸门

在当届规则发布前，采用以下保守策略：

- 仅使用未修改的 IEEE conference template；
- 不修改 `\textwidth`、`\textheight`、`\columnsep`、字号、caption 字号、bibliography spacing 或行距；
- 不用负 `\vspace` 堆叠版面；
- 所有关键图表在灰度、双栏宽和单栏宽下均可读；
- 主文自洽，不依赖未确认的 supplement；
- 保持一个“核心版”和一个“可删除扩展块”，等页数公布后快速收敛；
- 规则发布当天执行 diff：模板、页数、reference、appendix、supplement、PDF size、匿名页眉页脚。

---

# 6. Double-Blind and Anonymity Rules

## 6.1 官方状态

- **[UNRESOLVED]** 本次未获得 INFOCOM 2027 当届页面对 single-anonymous 或 double-anonymous 的明确说明。
- IEEE Author Center 仅定义两种模式，并说明大多数 IEEE publications 使用 single-anonymous；这不能用来推断 INFOCOM 2027。[IEEE-PEER]
- 因此，作者名、单位、致谢、基金号、自引、GitHub、artifact 和 supplement 的具体匿名规则均为 **UNRESOLVED**。

## 6.2 保守准备方案：按双盲标准准备

以下是**项目风险控制**，不是已经确认的 INFOCOM 2027 规则：

### 作者与机构

- 初稿版删除姓名、单位、邮箱、ORCID、个人主页；
- PDF metadata、文件名、LaTeX comments、embedded attachment、Git history export 中不得保留身份；
- title/abstract 中避免出现内部项目代号、实验室特有平台名或已公开演讲的唯一措辞。

### 致谢与项目编号

- 初稿版移除可识别资助号、实验室、CloudLab project 名、导师/同事姓名；
- 单独维护 camera-ready acknowledgments；
- AI 使用披露必须保留一份合规文本；若双盲规则要求移除致谢，应查看投稿系统是否有匿名 disclosure 字段，或询问 chairs，不能静默省略 IEEE 要求的披露。

### 自引

保守写法：

- 用第三人称正常引用，例如“Prior work [n] showed …”，不要写“in our previous work”；
- 不删除必要引用，也不要用“匿名引用”占位而损害 related work；
- 不在正文透露“我们开源的系统”“本实验室先前版本”等身份线索。

### GitHub、数据集、代码与 artifact

如果当届要求双盲：

- 不在稿件中链接当前公开的 `Ricardo3319/Micro_banch`；
- 制作一个脱敏、无 Git history、无账号名、无作者邮箱、无组织名的匿名 snapshot；
- 使用随机 artifact 标识，不使用论文标题、项目主页、CloudLab project 名；
- README 只描述复现步骤，不含引用作者自己身份的语句；
- 清理日志中的用户名、home path、hostname、SSH key path、云账户、绝对路径；
- supplement 和 artifact 均按主稿同等匿名标准处理。

### arXiv、技术报告、演讲和既有代码

**[CONFIRMED | IEEE-wide]** IEEE 允许作者在 arXiv、TechRxiv、批准的非营利预印本服务器、个人/单位网站或机构仓储发布 preprint，并不将其视为 prior publication，但要求相应 notice 和接收后的更新。[IEEE-POSTING]

**[UNRESOLVED | INFOCOM 2027]** 这不等于预印本不会影响 INFOCOM 的双盲要求。保守做法：

- 审稿期不要主动推广与投稿标题完全一致的新版本；
- 不在匿名稿中链接可识别的 preprint/repo；
- 不删除必要 prior-work disclosure；
- 记录公开时间、版本、URL，若投稿系统询问 prior dissemination，准确填写。

## 6.3 匿名违规的风险

是否“自动 desk reject”必须等 INFOCOM 2027 官方措辞。即便未明确自动拒稿，以下仍是高风险：

- PDF 首页出现作者/机构；
- acknowledgments 暴露基金或实验室；
- artifact URL 直接显示 GitHub 用户名；
- supplement 与主稿匿名级别不一致；
- 自引措辞直接承认作者身份；
- PDF metadata 包含作者名；
- 论文唯一术语与公开仓库/演讲完全一致并被正文链接。

---

# 7. Submission and Prior-Publication Policy

## 7.1 Simultaneous submission

**[CONFIRMED | IEEE-wide]** IEEE 要求提交原创工作，且不得同时处于另一 refereed publication 的评审中；作者须披露相关既有发表和当前相似投稿。[IEEE-SUBMISSION]

对 RescueSched 的操作要求：

- 不得同时把同一实质内容投往其他会议/期刊；
- 不得将改标题、换图或换少量实验视为不同论文；
- 若有相关 workshop/technical report/课程报告，建立 overlap table，逐段标明复用、扩展与新增；
- 所有作者在提交前书面确认“无并行评审”。

## 7.2 Prior publication、workshop 与扩展版本

**[CONFIRMED | IEEE-wide]** IEEE 承认技术工作的演化式发表，例如从早期 workshop 到完整会议再到期刊，但要求完全引用，并清楚说明新稿与既有工作的区别。[IEEE-SUBMISSION]

**[UNRESOLVED | INFOCOM 2027]** INFOCOM 2027 是否有更严格的 overlap 百分比、workshop 非归档条件或具体 disclosure 字段尚未确认。

项目应准备：

- 既有公开材料清单；
- 每份材料的发表/公开性质：refereed archival、non-archival workshop、technical report、preprint、code only、talk slides；
- 与 RescueSched 的文字、算法、数据和图表重合范围；
- 新增内容矩阵；
- 必须引用的自己的旧工作。

## 7.3 arXiv 与技术报告

- **[CONFIRMED | IEEE-wide]** IEEE 不把合规 preprint 视为 prior publication。[IEEE-POSTING]
- **[UNRESOLVED | INFOCOM 2027]** 当届是否限制审稿期更新或如何处理匿名，需等 CFP。
- 技术报告若包含论文全部核心结果，虽未必构成 IEEE prior publication，仍可能削弱匿名和 novelty perception；必须如实披露。

## 7.4 Plagiarism 与 self-plagiarism

**[CONFIRMED | IEEE-wide]**

- 引用他人或自己的已发表文字、观点、流程、结果、图表和数据均需适当引用；
- plagiarism 不可接受；
- 自己的旧文字、图、表也不能无标注复制；
- 若以旧工作为基础，必须明确说明差异；
- 作者须在提交前取得受限材料的许可。[IEEE-ETHICS][IEEE-SUBMISSION]

项目执行：

- 对主稿运行文字重合审计；
- 禁止从废案 AQB/DQB 文档直接复制仍暗示其是贡献的段落；
- 所有 reused diagram 必须重画或获得许可并引用；
- related work 中不能只引用二手描述，尤其是 ALTOCUMULUS。

## 7.5 Author list 与提交后修改

- **[UNRESOLVED]** INFOCOM 2027 的作者注册截止、提交后加删作者、顺序修改和 chair approval 规则尚未获得。
- **[CONFIRMED | IEEE-wide]** 作者必须同时满足显著智力贡献、参与撰写/审阅、批准最终版本三项条件；其他贡献者放在 acknowledgments。[IEEE-ETHICS]

保守要求：

- 在建立投稿记录前冻结作者候选名单；
- 记录每位作者贡献与最终批准；
- title、author order、邮箱、单位、ORCID 在投稿系统和最终 PDF 保持一致；
- 不把提供机器、经费或一般管理本身当成作者资格。

## 7.6 Conflicts of interest

**[CONFIRMED | IEEE-wide]** IEEE 要求避免实际、感知或潜在 COI；经常合作关系和财务利益是典型例子。[IEEE-SUBMISSION]

**[UNRESOLVED | INFOCOM 2027]** COI 年限、同机构规则、导师学生关系年限、域名冲突和提交后补报流程尚未确认。

项目现在应建立完整 COI 表，至少覆盖：

- 当前和近期合作者；
- 同单位人员；
- 导师/学生与密切个人关系；
- 共同项目、共同基金和财务利益；
- 与竞争系统作者的潜在关系；
- 每位作者确认和更新时间。

---

# 8. Generative-AI and Research-Ethics Policy

## 8.1 生成式 AI 内容披露

**[CONFIRMED | IEEE-wide]** IEEE 要求：文章中使用 AI 生成的内容，包括文字、图、图像和代码，应在 acknowledgments 中披露；应标识 AI 系统、受影响的具体章节和使用程度。仅用于编辑和语法增强通常不在强制披露意图内，但 IEEE 建议披露。[IEEE-SUBMISSION]

### RescueSched 的执行方案

维护 `AI_USE_DISCLOSURE.md`，记录：

- 工具和版本/访问日期；
- 用于哪些章节、代码、分析脚本或图表；
- 生成、改写、总结、校对或调试的程度；
- 人类作者如何验证事实、代码和引用；
- 最终 acknowledgments 文本。

示例结构，不应机械照抄：

> “The authors used [system] to assist with [specific sections/tasks]. All technical claims, code, experiments, citations, and final wording were independently reviewed and approved by the human authors.”

若 INFOCOM 2027 为双盲，AI disclosure 与匿名 acknowledgments 的冲突处理仍是 **UNRESOLVED**。不能因匿名而永久省略 IEEE 要求；应使用当届系统字段或向 chairs 请求书面指引。

## 8.2 AI 能否列为作者

**[CONFIRMED | IEEE-wide authorship rule；AI 结论为明示推论]** IEEE 的作者定义要求作者是能做出智力贡献、参与撰写/修订并批准最终版本的“individuals”，且承担最终责任。[IEEE-ETHICS] 生成式 AI 系统不能满足作者责任和最终批准要求，因此**不得列为作者**。这是基于官方作者资格定义的直接推论；本次找到的页面未用一句单独的“AI cannot be an author”措辞。

## 8.3 Human-subject 与数据伦理

**[CONFIRMED | IEEE-wide]** 涉及 human subjects/animals 的研究须说明 IRB/等效机构监督，或解释为何未审查；涉及人类受试者还须说明 consent 或解释为何未获得。[IEEE-SUBMISSION]

RescueSched 的分类：

- 纯 synthetic workload、公开 benchmark 和 CloudLab 系统测量通常不涉及 human subjects；
- 若使用生产 RPC trace、用户标识、tenant/flow 信息、真实请求内容或可关联个体行为的数据，必须重新进行隐私/伦理判断；
- 即使不构成人体研究，也要有数据使用授权、去标识化、最小化收集、保留期限和访问控制；
- 不公开 IP、租户 ID、账号、secret、CloudLab credential 或可逆映射。

## 8.4 网络测量伦理

本次未找到 INFOCOM 2027 特定网络测量伦理清单，因此状态为 **UNRESOLVED**。项目内部最低标准：

- 只测量有授权的机器、链路和服务；
- 避免对第三方网络造成负载；
- 记录 rate limit、实验时间窗和停止条件；
- 对公开 trace 核对 license 与再分发条款；
- 论文中报告丢包、超时、失败请求和过滤规则，不能只报告成功样本。

---

# 9. Review Criteria

## 9.1 IEEE 官方通用评审标准

**[CONFIRMED | IEEE-wide]** IEEE Author Center 列出的 reviewer 关注项如下：[IEEE-PEER]

| 官方标准    | 官方含义                     | RescueSched 对应问题                                         |
| ----------- | ---------------------------- | ------------------------------------------------------------ |
| Scope       | 是否适合该会议范围           | 是否被清楚呈现为 RSS/RPC networking system，而非通用 CPU scheduler？ |
| Novelty     | 是否与既有发表有实质区别     | 与 ALTOCUMULUS、work stealing、threshold migration 的决策条件有何不可替代差异？ |
| Validity    | 研究是否设计和执行正确       | 相同 trace、相同 handoff cost、非泄漏 estimator、无数据挑选、真实运行时。 |
| Data        | 数据是否正确报告、分析和解释 | paired seeds、CI、原始 CSV、tail 样本数、失败/丢弃处理。     |
| Clarity     | 表达是否清楚、简洁、逻辑一致 | 问题定义、算法、成本模型、claim boundary 和负结果是否一致。  |
| Compliance  | 是否满足伦理和出版要求       | 匿名、AI disclosure、COI、prior work、权限、格式。           |
| Advancement | 是否为领域带来显著贡献       | deadline goodput 改善是否足够大、适用范围是否重要、成本是否可接受。 |

INFOCOM 2027 是否公布更细的评分 rubric 仍为 **UNRESOLVED**。

## 9.2 网络系统论文的经验性评审预期

以下是**经验性判断，不是官方硬规则**：

- **Novelty**：不能只把两个已知条件相加；需要说明 request-specific feasibility 为什么改变错误迁移集合，且带来可观测、可复现的 SLO outcome。
- **Technical correctness**：算法估计不能使用当前请求隐藏 service time；目标队列 reservation、并发决策和 handoff delay 必须在实现中真实发生。
- **Significance**：只在一个人工点获胜通常不够；至少展示主区间、边界、负结果和 overhead/benefit tradeoff。
- **Experimental rigor**：强基线、同 trace、同成本、固定 holdout、paired CI、足够 tail 样本、配置预注册。
- **Reproducibility**：manifest、seed、raw CSV、脚本、commit、hardware 和失败日志。
- **Clarity**：清楚区分 simulator claim、physical claim、oracle upper bound、deployable estimator 和 limitation。
- **Related work**：不能把 ALTOCUMULUS 简化为一个弱“阈值法” strawman；必须按论文原机制实现或解释近似差异。
- **Negative results/limitations**：负结果本身不是缺陷；隐藏 W3 低负载失败或 W2 tail regression 才会破坏可信度。

## 9.3 真实系统证据的最低期望

对 RescueSched，主会 systems 叙事至少应具备：

1. 真实 NIC/RSS 或等价可核验的 flow-to-core receive steering；
2. 多核 pinned workers 与真实 per-core queue；
3. 实际 descriptor handoff，而不是只在 simulator 中扣分；
4. 同一运行时中的 `RandomCore/RSS-local`、polling work stealing、ALTO-style threshold 和 RescueSched；
5. 独立 load generator，最好是第二台 CloudLab 节点，经真实网络链路发送 RPC；
6. end-to-end latency、deadline violation/SLO goodput、throughput、CPU overhead、migration count；
7. local/cross-core/cross-NUMA handoff distribution；
8. W3 主结果、W1 sanity、W2 boundary、overload failure case；
9. 重复运行与置信区间；
10. simulator 与 physical replay 的同 trace 对齐。

---

# 10. Artifact and Reproducibility Expectations

## 10.1 官方状态

- **[CONFIRMED | IEEE official recommendation]** IEEE 强烈支持开放科学，并鼓励共享方法、数据、代码和其他研究输出。[IEEE-REPRO]
- IEEE Author Center 举例的 data repositories 包括 figshare、Zenodo、Dryad；代码平台示例包括 Code Ocean。[IEEE-REPRO]
- **[UNRESOLVED | INFOCOM 2027]** 是否强制 artifact、是否有 badge、是否参与主会评审、匿名上传方式、artifact 截止时间均未确认。
- 本次未找到 INFOCOM 2027 对 IEEE DataPort、Zenodo 或 GitHub 的指定优先级。不得声称某平台是当届“官方推荐仓库”。

## 10.2 RescueSched 当前 artifact 优势

项目已有以下良好基础：[P-PROV][P-EVAL]

- `rescuesched-v2` schema 与 trace SHA-256；
- legacy artifact 与 corrected evidence 隔离；
- authoritative Step-21 corrected full 目录；
- `manifest.md`、输入 `w1.csv/w2.csv/w3.csv`、`summary.csv`、`paired_comparisons.csv`、`go_no_go.md`；
- 固定开发和 holdout seeds；
- 相同 immutable trace；
- 明确生成、分析和绘图命令；
- paired 95% interval gate。

## 10.3 提交前必须补齐的 artifact 结构

建议形成如下只读包：

```text
artifact/
  README.md
  LICENSES.md
  MANIFEST.json
  CLAIMS_TO_FILES.md
  environment/
    software.txt
    hardware.txt
    cloudlab_profile.txt
  configs/
  traces/
    README.md
    hashes.sha256
  raw/
  derived/
  scripts/
  src/
  tests/
  logs/
  checksums.sha256
```

`MANIFEST.json` 至少记录：

- clean Git commit/tag；
- `git status --short` 应为空；
- compiler、CMake、Python、OS/kernel；
- CPU model、core/thread count、NUMA topology、frequency governor、NIC/driver/firmware、RSS configuration；
- CloudLab profile、node type、site、reservation、image、experiment ID 的匿名/公开版本；
- workload、rho、seed、trace hash、SLO、warmup、measurement interval；
- 每个 raw file 的命令、起止时间、exit code、checksum；
- 分析脚本版本和生成图表；
- 已知失败与排除规则。

## 10.4 Raw data、seed 与统计

- 保留每次 run 的逐请求或足够重建指标的日志；
- 不只发布汇总均值；
- seed 列表与 development/holdout 分工固定；
- tail 指标报告样本数和 quantile 方法；
- paired comparison 必须基于相同 workload/rho/seed/trace；
- 失败 run 不能静默删除，必须在 manifest 中说明；
- BMR/UMR 若依赖 counterfactual，必须标记为诊断而非直接可测事实。[P-EVAL][P-PHYS]

## 10.5 匿名 artifact

若 2027 要求双盲：

- 从 clean export 创建，不包含 GitHub history；
- 替换用户名、邮箱、组织、CloudLab project、主机名；
- README 不链接公开仓库；
- DOI/仓库名使用匿名占位或 conference-approved anonymous service；
- 检查压缩包 metadata 和绝对路径；
- supplement/artifact 中的 acknowledgments、license copyright holder 需按当届规则处理。

## 10.6 Camera-ready 后公开

- **[UNRESOLVED | INFOCOM 2027]** 何时允许/要求公开 artifact 未发布。
- 保守流程：评审期间保持匿名 snapshot；接收后再将 clean tagged release 推到公开 GitHub/Zenodo，并按 IEEE posting/copyright 规则更新论文版本。[IEEE-POSTING]
- Zenodo 可用于不可变 DOI 归档；GitHub 适合开发；Code Ocean 是 IEEE 明确举例的可运行代码平台。[IEEE-REPRO]

---

# 11. Camera-Ready and Publication Requirements

## 11.1 已确认的 IEEE-wide 要求

| 项目                | 状态            | 要求                                                         |
| ------------------- | --------------- | ------------------------------------------------------------ |
| 最终标题与作者名    | **CONFIRMED     | IEEE-wide**                                                  |
| IEEE Copyright Form | **CONFIRMED     | IEEE-wide**                                                  |
| ORCID               | **CONFIRMED     | IEEE-wide**                                                  |
| PDF eXpress         | **CONDITIONAL** | 仅在 conference 指示时使用；INFOCOM 2027 是否使用尚未确认。[IEEE-XPLORE][IEEE-FINAL] |
| 出版后修改          | **CONFIRMED     | IEEE-wide**                                                  |
| 第三方材料          | **CONFIRMED     | IEEE-wide**                                                  |

## 11.2 尚未确认的 INFOCOM 2027 出版细节

以下全部 **UNRESOLVED**：

- camera-ready 日期；
- 最终页数与超页费；
- PDF eXpress conference ID；
- copyright notice 位置；
- open access 选项、费用与截止；
- author registration 和 no-show/presentation policy；
- presentation 时长与格式；
- 最终 artifact/public repository deadline；
- 最终 affiliation、funding statement 和 grant number 的具体版式。

## 11.3 图片、表格和第三方材料

- 自制图必须能追溯到原始数据和脚本；
- 复用或改编他人图时，引用不等于版权许可；
- 若只需要比较机制，优先重画自己的概念图并引用原论文；
- logo、产品截图、CloudLab 页面截图、厂商图也可能受版权/商标约束；
- camera-ready 前完成 RightsLink/许可文件归档。

## 11.4 引用与 bibliography

- **[CONFIRMED | IEEE-wide]** 所有直接引用、转述、数据、结果、图表以及自己的既有工作都需要引用。[IEEE-ETHICS]
- IEEE 官方 conference template 应作为 bibliography 样式起点。[IEEE-TEMPLATE]
- **[UNRESOLVED | INFOCOM 2027]** references 是否计页、允许的 appendix、脚注/URL 细则和 bibliography 上限。
- 项目内部要求每条 related-work claim 直接对应原论文；不以博客或二手综述替代原始机制描述。

---

# 12. Desk-Reject Checklist

INFOCOM 2027 的正式 desk-reject 清单尚未读取。下表区分“已知 IEEE 级违规”和“高概率会议级风险”。

| 问题                           | 规则状态                                  | 风险处理                                                     |
| ------------------------------ | ----------------------------------------- | ------------------------------------------------------------ |
| 超过当届页数                   | **UNRESOLVED（具体数值）**                | 规则发布后自动检查；禁止靠缩字/改边距规避。                  |
| 非匿名或暴露身份               | **UNRESOLVED（当届审稿模式）**            | 先按双盲标准准备；扫描 PDF metadata、repo、supplement。      |
| 错误模板/字号/纸张             | **UNRESOLVED**                            | 使用官方模板，发布后做逐项 diff。                            |
| 同时投稿                       | **CONFIRMED 禁止（IEEE-wide）**           | 全体作者书面确认无并行评审。[IEEE-SUBMISSION]                |
| Prior work 未披露/未引用       | **CONFIRMED 风险（IEEE-wide）**           | 提交 overlap statement 和完整引用。[IEEE-SUBMISSION]         |
| Plagiarism/self-plagiarism     | **CONFIRMED 违规（IEEE-wide）**           | 文本、图表和代码 provenance 审计。[IEEE-ETHICS]              |
| Scope 不符                     | **CONFIRMED 评审标准（IEEE-wide）**       | 强化 RSS/RPC/networking systems 叙事。[IEEE-PEER]            |
| PDF 字体未嵌入/有安全限制      | **CONFIRMED Xplore 合规项**               | 本地 `pdffonts`、PDF Checker/PDF eXpress 检查。[IEEE-XPLORE] |
| 缺少必填 metadata              | **UNRESOLVED（系统字段）**                | 投稿系统开放后建字段表，逐项双人核验。                       |
| 作者顺序/PDF/系统不一致        | **高风险**                                | 冻结 author manifest；最终下载回验。                         |
| COI 漏报                       | **CONFIRMED IEEE 伦理要求；当届细则未决** | 全作者 COI 合并、去重、签字确认。[IEEE-SUBMISSION]           |
| AI 使用未披露                  | **CONFIRMED IEEE 要求**                   | 建 AI log 和最终 disclosure。[IEEE-SUBMISSION]               |
| Human-subject 声明缺失         | **CONFIRMED IEEE 要求（如适用）**         | IRB/consent 或不适用说明。[IEEE-SUBMISSION]                  |
| Supplement 违规或不匿名        | **UNRESOLVED**                            | 在允许之前不依赖 supplement；同等匿名检查。                  |
| 使用废案结果支撑 RescueSched   | **项目级禁止**                            | AQB/DQB/legacy CSV 全部排除。[P-README][P-PROV]              |
| 仿真结果写成真机结论           | **技术诚信风险**                          | 明确 simulator-only；CloudLab 完成前不写 deployment claim。[P-PHYS] |
| Artifact commit dirty/不可重建 | **可复现性高风险**                        | clean tag、checksum、manifest、fresh rerun。[P-PROV]         |

---

# 13. RescueSched-Specific Compliance Analysis

## 13.1 唯一贡献主线

本文严格遵循用户和当前 README：

> RescueSched 是唯一有效论文主线；此前因与既有工作过度重合而放弃的 AQB/DQB 或其他旧方案，仅是历史材料，不能作为 contribution、novelty、实验结果或“额外系统模块”。[P-README]

论文中应完全移除：

- “我们还提出 AQB/DQB”之类并列贡献；
- 旧方案图、旧 CLI 结果和旧统计；
- 从旧方案继承但未在 RescueSched 当前合同中验证的因果表述；
- 把废案失败包装成 RescueSched ablation 的叙述。

## 13.2 RescueSched 的可辩护 novelty

当前 paper contract 给出的最强、也最窄的 novelty 是：[P-CONTRACT]

1. **Request-specific rescue window**：候选必须 predicted local miss；
2. **Remote deadline feasibility**：迁移后 predicted remote meet，而不只是目标较空；
3. **Paid handoff**：descriptor migration 有真实时间/资源成本，不只是 score penalty；
4. **Target reservation and bounded control**：在并发决策中保留目标侧工作量；
5. **Non-leaking deployable estimator**：使用 method-keyed EWMA，不观察当前请求隐藏 service time；
6. **Outcome-oriented evaluation**：以 deadline violation/SLO goodput 为主，而非只看平均负载或迁移次数。

最安全的核心表述是：

> RescueSched is not a new claim that RPC work can be migrated. It is a request-specific decision rule for migrating a queued descriptor only when the predicted deadline outcome changes from a local miss to a remote meet after accounting for handoff and target reservations.

## 13.3 与 ALTOCUMULUS 的定位

书目已核实：

- Jiechen Zhao, Iris Uwizeyimana, Karthik Ganesan, Mark C. Jeffrey, Natalie Enright Jerger, **“ALTOCUMULUS: Scalable Scheduling for Nanosecond-Scale Remote Procedure Calls,”** MICRO 2022, pp. 423–440, DOI `10.1109/MICRO56248.2022.00040`。[L-ALTO]

项目合同把它称为最接近工作，并认为它已经覆盖 proactive SLO-aware queued-RPC descriptor migration。[P-CONTRACT]

本次调研核实了书目信息，但未获得该论文全文的可审计官方页面内容。因此，以下机制比较必须在最终 related work 前由作者直接阅读全文核验：

| 对比问题 | ALTOCUMULUS                                   | RescueSched 必须证明                                         |
| -------- | --------------------------------------------- | ------------------------------------------------------------ |
| 触发依据 | 不得只写“queue threshold”；需按原论文精确描述 | request-specific predicted local miss。                      |
| 目标选择 | 需核对是否已有 deadline/SLO feasibility       | 明确 remote meet after handoff，而非“更轻载”。               |
| 迁移对象 | 需核对 queued RPC/descriptor 语义             | 只迁移 queued descriptor，不迁 payload、不抢占 running RPC。 |
| 成本模型 | 需核对硬件/软件成本如何处理                   | 真机 measured handoff distribution、reservation 和 scheduler cycles。 |
| SLO 模型 | 需核对 per-request/per-class deadline         | 清楚说明 deadline 来源、估计和错误。                         |
| 系统路径 | 需核对硬件设计或 runtime                      | commodity multicore/RSS server 的适用边界。                  |
| 结果     | 需核对 workload、指标和 baseline              | 不用不同 workload 或弱实现制造虚假优势。                     |

**严禁的 novelty 说法：**

- “首次把 RPC 从一个核迁移到另一个核”；
- “首次进行 SLO-aware RPC scheduling”；
- “首次在队列不平衡时主动迁移”；
- “ALTOCUMULUS 只是简单阈值法”，除非原论文全文和实现确实支持该概括。

## 13.4 与 work stealing 的定位

最新评估合同的强基线是 `L1_WorkStealingPolling`：[P-EVAL]

- idle core 周期性 pull；
- 成功 steal 支付同样 descriptor-handoff delay；
- target reservation 可见；
- 报告 polling attempts、success、moved work 和 frequency。

RescueSched 与其差异应被写成：

- work stealing 是**空闲触发、reactive pull**；
- RescueSched 是**deadline outcome 触发、request-specific bounded repair**；
- 两者都必须支付实际 handoff；
- RescueSched 不能靠更高迁移量或更高控制频率获胜；
- W3 `rho=0.70` 输给 polling work stealing 的结果必须报告。[P-CONTRACT]

## 13.5 与广义 SLO-aware RPC scheduling 的定位

Related work 至少拆分以下类别：

1. queue-length/load threshold migration；
2. work stealing/work shedding；
3. EDF、priority、SLO-aware local scheduling；
4. admission control/load shedding；
5. request cloning/hedging；
6. in-network/RPC core selection；
7. hardware-accelerated nanosecond RPC schedulers；
8. descriptor migration/queue repair。

RescueSched 不应声称替代所有类别。它解决的是：

> 在已有 per-core RSS queue 和不可抢占的 queued RPC 条件下，利用可部署估计器判断一次 descriptor handoff 是否能把该请求从 deadline miss 改为 meet。

## 13.6 当前最可能的审稿质疑

### Novelty

- local-miss + remote-meet 是否只是明显 heuristic？
- ALTOCUMULUS 是否已经隐式或显式做了同样 feasibility test？
- target safety/reservation 是否已有成熟设计？
- estimator 和 cost accounting 是实现细节还是科研贡献？

**回答所需证据：** formal decision boundary、错误迁移分类、与 ALTO 原机制的 exact ablation、在相同迁移预算下的 outcome change。

### 系统真实性

- simulator 是否忠实表现 NIC/RSS、queue concurrency、cache/NUMA、handoff 和 OS noise？
- descriptor 到底是什么，如何安全转移 ownership？
- flow affinity、transport state、payload buffer、completion path 是否允许迁移？
- 1 μs control period 在真实 runtime 是否可承受？

**回答所需证据：** 真实 runtime、并发安全、measured costs、CPU cycles、RSS config 和 end-to-end RPC。

### 实验充分性

- 是否只有 synthetic W3；
- 是否只在 `rho=0.85/0.90` 的少数点获胜；
- W2 P99/P999 严重退化是否意味着方法伤害尾延迟；
- baseline 是否忠实且充分调参；
- 十个 seed 是否足够支撑 P99.9；
- physical 与 simulator 是否同 trace、同 completion set。

## 13.7 可以写入论文的仿真结论

前提是每个数字映射到 authoritative Step-21 CSV、命令、配置、trace hash 和 commit：[P-CONTRACT][P-PROV]

可以写：

- “在定义的离散事件 simulator、冻结 trace、相同 handoff cost 和 EWMA estimator 下……”；
- W3 在 `rho=0.85` 和 `rho=0.90` 通过预注册 deadline-violation gate；
- W3 在 `rho=0.70` 输给 polling work stealing；
- W2 即使 deadline misses 改善，也可能出现严重 P99/P999 regression；
- identical trace、paid handoff、target reservation、paired holdout CI 的方法学；
- estimator error、migration cost、budget、period、scan bound 的 sensitivity；
- global overload 下不声称创造不存在的 slack；
- oracle 仅是 non-deployable upper bound。

必须用限定词：`in simulation`、`under the evaluated workload/model`、`for the tested load points`。

## 13.8 必须等待 CloudLab/真机结果的表述

在完成物理实验前，以下表述不得出现为已证实事实：

- “RescueSched is deployable on commodity RPC servers”；
- “descriptor migration costs only X μs”——除非 X 来自真实运行路径；
- “negligible CPU overhead”；
- “preserves cache locality/NUMA locality”；
- “improves end-to-end RPC P99/P99.9”；
- “sustains line rate / production throughput”；
- “works with Linux RSS/DPDK/eRPC/真实 transport”；
- “robust to OS jitter, IRQ placement and NIC queues”；
- “simulator accurately predicts physical performance”；
- “beneficial migration ratio is causally measured”——若依赖 counterfactual，只能称 bounded/diagnostic estimate。

## 13.9 只能作为 limitation，不能作为 contribution 的内容

- single-host 或单服务范围；
- same-NUMA-only migration；
- 只迁 queued descriptor，不迁 payload；
- 不抢占 running request；
- append-to-tail 的简化；
- synthetic W1/W2/W3 workload；
- 未覆盖 global overload；
- 对 EWMA/method label 质量的依赖；
- W3 低负载不优于 work stealing；
- W2 P99/P999 退化；
- 没有 production deployment；
- BMR/UMR 的 counterfactual、非因果性质；
- 当前物理计划和 loader 未完成。

这些边界可以提升可信度，但不能包装成“贡献”。

---

# 14. RescueSched Evidence Required Before Submission

## 14.1 真机实验最低集

### A. 必须完成：主会 systems claim 的最低门槛

1. **真实 RPC server path**
   - 至少一台 server 节点和一台独立 load-generator 节点；
   - server 端开启并记录 NIC RSS queues、IRQ/core affinity、worker pinning；
   - per-core FIFO/dispatch queue 可观测。

2. **同一 runtime 中实现全部强基线**
   - `L0_RandomCore`/RSS-local；
   - `L1_WorkStealingPolling`；
   - `M0_AltoThreshold`；
   - `M1_RescueSched`；
   - 所有迁移支付相同真实 handoff path。

3. **真实 descriptor ownership transfer**
   - 说明 transport state、buffer reference、completion callback、cache ownership 和并发安全；
   - 记录迁移开始/结束、source/target、request ID、deadline、估计值和结果。

4. **Migration microbenchmark**
   - local queue push/pop；
   - cross-core；
   - cross-NUMA（即使主设计限制 same-NUMA，也要作为边界）；
   - 分布而非单个均值：median、P95、P99、max；
   - CPU governor、frequency、NUMA、NIC、OS noise。

5. **End-to-end 主矩阵**
   - W3 `rho={0.70,0.85,0.90}`；
   - W1 sanity、W2 boundary；
   - 相同 trace replay；
   - deadline violation/SLO goodput 为 primary；
   - P99/P999、throughput、CPU 和 migration overhead 为 secondary。

6. **重复性和统计**
   - 与 simulator 相同 seeds/trace；
   - 多次独立 run；
   - paired CI；
   - 失败 run 和 timeout/dropped request 规则公开。

7. **Overhead**
   - scheduler cycles/request；
   - checks/candidates/completed request；
   - migrations/requests 与 migrated work；
   - CPU utilization；
   - throughput loss；
   - 可行时报告 cache miss/remote memory。

8. **Simulator–physical alignment**
   - 同输入 trace、SLO、warmup、完成集合和 timeout；
   - 实测 handoff cost 回填 simulator；
   - 对方向不一致给出原因，不能只保留一致点。

### B. 强烈建议

- 一种真实应用服务或公开 RPC benchmark，而不只是 sleep/busy-loop service；
- 不同 service-time estimation error；
- 不同 RSS queue 数和 core 数；
- IRQ/worker mapping 敏感性；
- 真实或公开 trace（许可允许时）；
- failure injection：目标核瞬时拥塞、handoff stall、estimator drift。

## 14.2 CloudLab 必须记录的信息

| 类别     | 字段                                                         |
| -------- | ------------------------------------------------------------ |
| 实验身份 | profile 名、site、reservation、experiment UUID、开始/结束时间；匿名版需脱敏。 |
| 硬件     | node type、CPU model/stepping、cores/threads、NUMA、memory、NIC model、link speed。 |
| 软件     | OS image、kernel、compiler、CMake、runtime/transport、NIC driver/firmware。 |
| CPU      | governor、turbo、C-state、frequency、SMT、isolated cores、IRQ affinity。 |
| 网络     | MTU、RSS hash/key/indirection table、queue count、offloads、link topology、loss。 |
| 构建     | clean commit/tag、submodules、build flags、dependencies。    |
| workload | trace hash、arrival process、service distribution、SLO、rho、seed、duration。 |
| policy   | period、budget、scan K、targets H、threshold、estimator、handoff mode。 |
| 输出     | raw log、stderr/stdout、exit code、checksums、analysis command。 |

## 14.3 项目证据到评审标准映射表

| 评审标准        | 当前证据                                                     | 当前评价  | 提交前缺口                     | 通过条件                                                     |
| --------------- | ------------------------------------------------------------ | --------- | ------------------------------ | ------------------------------------------------------------ |
| Scope           | RSS-sharded RPC queue-repair 问题定义 [P-README][P-CONTRACT] | 强        | 真机网络路径叙事               | 系统图和实验展示 NIC→RSS→queue→worker→handoff。              |
| Novelty         | local-miss/remote-meet + paid handoff [P-CONTRACT]           | 中/高风险 | ALTOCUMULUS 全文 claim matrix  | 证明其未做相同 request-specific outcome-change test，或明确更窄增量。 |
| Validity        | frozen trace、EWMA、same cost、reservation [P-EVAL]          | 仿真强    | 真实并发与成本                 | 物理实现与 simulator 对齐，无 hidden service leakage。       |
| Data            | 10 seeds、paired 95% CI、Step-21 [P-CONTRACT][P-PROV]        | 中强      | 真机重复与 tail sample size    | raw data、运行失败、quantile 方法全部可审计。                |
| Significance    | W3 0.85/0.90 positive；0.70/W2 negative [P-CONTRACT]         | 有边界    | 真实 effect size 与 throughput | 物理 primary metric 有稳定改善且成本可接受。                 |
| Reproducibility | schema、trace hash、manifest [P-PROV]                        | 强基础    | dirty→clean，physical package  | clean tag、checksums、one-command reproduction。             |
| Systems realism | Physical plan [P-PHYS]                                       | 当前弱    | loader/runtime/real RSS        | 完成真机主矩阵和 handoff instrumentation。                   |
| Clarity         | paper contract 的 claim boundary [P-CONTRACT]                | 强        | 统一旧文档和基线名             | 论文、代码、图、表、artifact 同一术语。                      |
| Compliance      | 本指南、AI/COI/prior-work 清单                               | 未完成    | 当届匿名/格式/系统字段         | 官方规则发布后零差异，双人审计。                             |

## 14.4 实验 Go/No-Go

### GO：可以维持 INFOCOM main technical paper 叙事

- physical loader/runtime 完成；
- 三个强基线和 RescueSched 在同一实现中公平运行；
- W3 至少一个中高负载点的物理 paired interval 支持 primary claim；
- 未依赖更多 migrated work 取胜；
- W2/低负载负结果如实报告；
- ALTOCUMULUS claim matrix 通过；
- clean artifact 可重建。

### NO-GO 或必须缩窄

- 截止前只有 simulator；
- physical 只测 microbenchmark，没有 end-to-end RPC；
- baseline 是弱化 one-shot 版本；
- 物理方向与 simulator 相反且无解释；
- 主要收益只存在于 oracle；
- P99/P999 或 throughput 损失抵消 deadline goodput；
- novelty 与 ALTOCUMULUS 无法区分。

若触发 NO-GO，应把论文降格为严格的 simulation/boundary study，或重新评估 workshop/poster；不能用语言包装替代证据。

---

# 15. Submission Timeline and Milestones

> 官方只确认 2026-07-31 这一日期，未确认具体时刻/时区。下表是从 2026-07-15 起的项目内部倒排计划。

| 日期（PDT）      | D−日 | 必须完成                                                     | 交付物/闸门                                       |
| ---------------- | ---: | ------------------------------------------------------------ | ------------------------------------------------- |
| 2026-07-15       | D−16 | 冻结唯一主线；官方规则初查；冻结 claim boundary              | 本指南；废案排除清单；source log。                |
| 2026-07-16       | D−15 | 完成 ALTOCUMULUS 全文阅读和 mechanism matrix                 | `RELATED_WORK_CLAIM_MATRIX.md`；DOI/BibTeX 核验。 |
| 2026-07-17       | D−14 | 冻结 author/COI/prior-publication/AI log                     | author manifest；COI 表；AI disclosure draft。    |
| 2026-07-18       | D−13 | physical trace loader 最小可运行；server/load generator 连通 | loader tests；一条端到端 RPC trace。              |
| 2026-07-19       | D−12 | 真实 descriptor handoff 和 instrumentation                   | local/cross-core microbench raw data。            |
| 2026-07-20       | D−11 | 四策略同 runtime smoke test；复查官方规则                    | fairness checklist；官方规则 diff。               |
| 2026-07-21       | D−10 | W3 物理 pilot；校准 handoff cost                             | pilot manifest；go/no-go 初判。                   |
| 2026-07-22       |  D−9 | W3 全矩阵开始；W1/W2 pilot                                   | raw logs；paired run map。                        |
| 2026-07-23       |  D−8 | 主结果数据冻结候选；主会 readiness 决策                      | **关键 Go/No-Go：无可信物理证据则缩窄或转轨。**   |
| 2026-07-24       |  D−7 | 完成统计、tail sample 审计、负结果                           | summary/CI；W2/低负载边界表。                     |
| 2026-07-25       |  D−6 | 全文技术冻结；所有数字映射到 artifact                        | claim-to-file table；figure manifest。            |
| 2026-07-26       |  D−5 | related work、limitations、ethics、AI、artifact 段落冻结     | 完整匿名稿 v1。                                   |
| 2026-07-27       |  D−4 | 内部 reviewer round；官方规则终审                            | reviewer issue list；规则差异修复。               |
| 2026-07-28       |  D−3 | 实验最终锁定；clean rerun/tag/archive                        | immutable release candidate；checksums。          |
| 2026-07-29       |  D−2 | PDF/metadata/anonymity/COI/author 双人审计                   | final candidate PDF；提交系统草稿。               |
| 2026-07-30 12:00 |  D−1 | **内部硬截止：上传并下载回验**                               | 提交确认、PDF checksum、系统截图/收据。           |
| 2026-07-31       |    D | 官方仅确认日期；只处理不可预见应急                           | 不安排新实验、不首次上传。                        |

## 15.1 规则发布后的即时差异审计

在 CFP/Author Information/投稿系统可读后的 2 小时内，检查：

1. abstract 与 paper 是否两个截止；
2. 精确时区和时刻；
3. 页数及 references/appendix；
4. 模板、字号、paper size；
5. double-blind/self-citation/arXiv；
6. supplement/artifact；
7. author list 和 COI；
8. rebuttal；
9. ethics/AI；
10. desk reject 和 PDF 限制。

任何差异以官方新规则为准，并在本指南顶部增加 dated changelog。

---

# 16. Final Author Checklist

## 16.1 一页投稿检查表

> 打印或复制到 issue tracker；每项由至少两人独立核验。

### Conference rule

- [ ] INFOCOM 2027 CFP、Author Information 和投稿系统页面已保存为 PDF/HTML，记录访问日期。
- [ ] 2026-07-31 的精确截止时刻与时区已确认；未依赖猜测。
- [ ] 摘要注册与完整论文截止已区分。
- [ ] 页数、references、appendix、supplement、文件大小已确认。
- [ ] 审稿模式和全部匿名规则已确认。

### Paper identity and scope

- [ ] RescueSched 是唯一贡献主线；AQB/DQB/废案未作为证据。
- [ ] Title/abstract 第一页明确 RSS、RPC、networking systems 问题。
- [ ] Contribution 不声称“首次 RPC migration”或“通用 tail-latency 改善”。
- [ ] ALTOCUMULUS 全文已读，逐机制差异有原文依据。
- [ ] Work stealing 与 threshold baseline 均为强、同成本实现。

### Evidence

- [ ] 所有数字映射到 authoritative CSV、命令、trace hash、script、commit。
- [ ] physical runtime、trace loader、真实 descriptor handoff 已完成。
- [ ] W3 `rho=0.70/0.85/0.90`、W1、W2 和 overload 边界均报告。
- [ ] W3 低负载失败和 W2 P99/P999 regression 未隐藏。
- [ ] Oracle、BMR/UMR 和 simulator-only 结论均正确限定。
- [ ] Tail sample count、quantile 方法、失败 run、drop/timeout 规则已记录。

### Artifact

- [ ] Git worktree clean；tag/commit immutable；checksums 完整。
- [ ] manifest 含 compiler、OS/kernel、CPU/NUMA/NIC/RSS/governor、CloudLab profile。
- [ ] 匿名 artifact 无姓名、邮箱、用户名、host/path、公开 repo 链接。
- [ ] fresh machine 从 README 可复现核心表/图。

### Ethics and policy

- [ ] 全体作者确认无 simultaneous submission。
- [ ] Prior publication/preprint/technical report 均披露并正确引用。
- [ ] 文本、图表、数据、代码无 plagiarism/self-plagiarism；第三方权限已取得。
- [ ] Author list 满足 IEEE authorship；顺序、邮箱、单位、ORCID 已确认。
- [ ] COI 按当届规则完整申报。
- [ ] AI 使用有日志和 IEEE-compliant disclosure。
- [ ] 若使用真实用户/租户 trace，IRB/consent/隐私和数据授权已处理。

### PDF and submission

- [ ] 官方模板未被缩字、改边距、压行距或负间距规避。
- [ ] PDF 字体嵌入，无密码、附件和异常 metadata。
- [ ] 作者/机构/致谢/基金/仓库匿名处理符合当届规则。
- [ ] 投稿系统 title、abstract、keywords、authors、COI 与 PDF 一致。
- [ ] 最终 PDF 已从系统下载回验，页数、checksum 和提交状态正确。
- [ ] 提交确认邮件/收据和页面保存到只读审计目录。

## 16.2 接收后的清单

- [ ] 根据通知完成 camera-ready 修改，未引入未经评审的新核心结果。
- [ ] 恢复作者、单位、致谢、基金和 AI disclosure。
- [ ] 全体作者 ORCID 可用且 metadata 一致。[IEEE-ORCID]
- [ ] 完成 eCF/copyright。[IEEE-COPYRIGHT]
- [ ] 按 conference 指示使用 PDF eXpress/PDF Checker。[IEEE-XPLORE]
- [ ] 公开 clean GitHub release 和不可变 Zenodo/其他归档（如政策允许）。
- [ ] 更新 preprint 的 DOI/copyright notice。[IEEE-POSTING]
- [ ] 完成 author registration、presentation 和可能的 no-show 要求。

---

# 17. Official Sources

## 17.1 INFOCOM 当届与上一届官方来源

| ID         | 页面标题                                                     | 发布机构                    | URL                                                          | 访问日期   | 适用年份     | 证据等级                             | 用途                                                         |
| ---------- | ------------------------------------------------------------ | --------------------------- | ------------------------------------------------------------ | ---------- | ------------ | ------------------------------------ | ------------------------------------------------------------ |
| O27-COMSOC | IEEE INFOCOM 2027: IEEE International Conference on Computer Communications 2027 | IEEE Communications Society | https://www.comsoc.org/conferences-events/ieee-international-conference-computer-communications-2027 | 2026-07-15 | INFOCOM 2027 | 官方会议信息                         | 截止日期、会议日期/地点、范围、会议组成。                    |
| O27-SITE   | IEEE INFOCOM 2027 annual website                             | IEEE INFOCOM / IEEE ComSoc  | https://infocom2027.ieee-infocom.org/                        | 2026-07-15 | INFOCOM 2027 | 官方页面；当前访问状态               | 官网存在，但本次环境仅返回 JavaScript/WAF 提示，未据此推断规则。 |
| O26-COMSOC | IEEE INFOCOM 2026: IEEE International Conference on Computer Communications 2026 | IEEE Communications Society | https://www.comsoc.org/conferences-events/ieee-international-conference-computer-communications-2026 | 2026-07-15 | INFOCOM 2026 | 官方历史会议信息 / PROVISIONAL       | 仅用于核对会议范围和历史活动结构；不用于推定 2027 格式。     |
| O26-SITE   | IEEE INFOCOM 2026 annual website                             | IEEE INFOCOM / IEEE ComSoc  | https://infocom2026.ieee-infocom.org/                        | 2026-07-15 | INFOCOM 2026 | 官方页面；当前访问状态 / PROVISIONAL | 年度官网存在，但本次环境仅返回 JavaScript/WAF 提示；未据此推断任何详细规则。 |

## 17.2 IEEE 官方政策与作者资源

| ID              | 页面标题                             | 发布机构                         | URL                                                          | 访问日期   | 适用年份                       | 证据等级            | 用途                                                         |
| --------------- | ------------------------------------ | -------------------------------- | ------------------------------------------------------------ | ---------- | ------------------------------ | ------------------- | ------------------------------------------------------------ |
| IEEE-ORCID      | ORCID: Get Credit for Your Work      | IEEE Author Center — Conferences | https://conferences.ieeeauthorcenter.ieee.org/become-an-ieee-conference-author/orcid-get-credit-for-your-work/ | 2026-07-15 | 现行 IEEE conference policy    | 官方规则            | 所有 conference authors 需要 ORCID。                         |
| IEEE-ETHICS     | Ethical Requirements                 | IEEE Author Center — Conferences | https://conferences.ieeeauthorcenter.ieee.org/author-ethics/ethical-requirements/ | 2026-07-15 | 现行 IEEE policy               | 官方规则            | authorship、引用、plagiarism、原创性。                       |
| IEEE-SUBMISSION | Submission Policies                  | IEEE Author Center — Conferences | https://conferences.ieeeauthorcenter.ieee.org/author-ethics/guidelines-and-policies/submission-policies/ | 2026-07-15 | 现行 IEEE policy               | 官方规则            | simultaneous submission、prior publication、preprint notice、human subjects、AI、text recycling、COI。 |
| IEEE-TEMPLATE   | Authoring Tools and Templates        | IEEE Author Center — Conferences | https://conferences.ieeeauthorcenter.ieee.org/write-your-paper/authoring-tools-and-templates/ | 2026-07-15 | 现行 IEEE guidance             | 官方建议            | 官方 Word/LaTeX conference template、LaTeX/reference tools。 |
| IEEE-XPLORE     | Meet IEEE Xplore Requirements        | IEEE Author Center — Conferences | https://conferences.ieeeauthorcenter.ieee.org/write-your-paper/meet-ieee-xplore-requirements/ | 2026-07-15 | 现行 IEEE publication guidance | 官方规则/条件性要求 | PDF eXpress、字体嵌入和 PDF 合规；是否由 INFOCOM 使用待确认。 |
| IEEE-REPRO      | Research Reproducibility             | IEEE Author Center — Conferences | https://conferences.ieeeauthorcenter.ieee.org/write-your-paper/research-reproducibility/ | 2026-07-15 | 现行 IEEE guidance             | 官方建议            | 方法、数据、代码共享；Zenodo/figshare/Dryad/Code Ocean 示例。 |
| IEEE-PEER       | Understand Peer Review               | IEEE Author Center — Conferences | https://conferences.ieeeauthorcenter.ieee.org/understand-peer-review/ | 2026-07-15 | 现行 IEEE guidance             | 官方规则/说明       | scope、novelty、validity、data、clarity、compliance、advancement；匿名模式定义。 |
| IEEE-FINAL      | Finalize Your Paper                  | IEEE Author Center — Conferences | https://conferences.ieeeauthorcenter.ieee.org/get-published/finalize-your-paper/ | 2026-07-15 | 现行 IEEE guidance             | 官方规则/说明       | 最终标题/作者检查、模板文本、条件性 PDF eXpress、Xplore 后不可修改。 |
| IEEE-COPYRIGHT  | About Transferring Copyright to IEEE | IEEE Author Center — Conferences | https://conferences.ieeeauthorcenter.ieee.org/get-published/about-transferring-copyright-to-ieee/ | 2026-07-15 | 现行 IEEE policy               | 官方规则            | eCF/copyright、复用材料和权限。                              |
| IEEE-POSTING    | Post Your Paper                      | IEEE Author Center — Conferences | https://conferences.ieeeauthorcenter.ieee.org/get-published/post-your-paper/ | 2026-07-15 | 现行 IEEE policy               | 官方规则            | arXiv/TechRxiv/preprint、accepted manuscript 和最终 PDF posting。 |

## 17.3 项目证据来源

这些来源用于评估 RescueSched，不是 INFOCOM 官方规则。

| ID                  | 页面标题                                     | 发布者                  | URL                                                          | 访问日期   | 适用版本                      | 证据等级                         |
| ------------------- | -------------------------------------------- | ----------------------- | ------------------------------------------------------------ | ---------- | ----------------------------- | -------------------------------- |
| P-README            | RescueSched Simulator — README.md            | Ricardo3319/Micro_banch | https://github.com/Ricardo3319/Micro_banch/blob/codex/rescuesched-baselines/README.md | 2026-07-15 | `codex/rescuesched-baselines` | 项目证据                         |
| P-CONTRACT          | RescueSched INFOCOM 2027 Paper Contract      | Ricardo3319/Micro_banch | https://github.com/Ricardo3319/Micro_banch/blob/codex/rescuesched-baselines/docs/PAPER_CONTRACT_INFOCOM2027.md | 2026-07-15 | 当前论文分支                  | 项目证据                         |
| P-EVAL              | RescueSched Corrected Evaluation Contract v2 | Ricardo3319/Micro_banch | https://github.com/Ricardo3319/Micro_banch/blob/codex/rescuesched-baselines/docs/RESCUESCHED_EVALUATION_CONTRACT_V2.md | 2026-07-15 | 2026-07-12 contract           | 项目证据                         |
| P-PROV              | Artifact and Figure Provenance               | Ricardo3319/Micro_banch | https://github.com/Ricardo3319/Micro_banch/blob/codex/rescuesched-baselines/docs/ARTIFACT_PROVENANCE.md | 2026-07-15 | 当前论文分支                  | 项目证据                         |
| P-PHYS              | RescueSched Physical Reproduction Plan       | Ricardo3319/Micro_banch | https://github.com/Ricardo3319/Micro_banch/blob/codex/rescuesched-baselines/docs/RESCUESCHED_PHYSICAL_REPRODUCTION_PLAN.md | 2026-07-15 | 当前论文分支                  | 项目证据；明确是计划而非完成证据 |
| P-IDEA              | 新实验思想指导.md                            | Ricardo3319/Micro_banch | https://github.com/Ricardo3319/Micro_banch/blob/main/%E6%96%B0%E5%AE%9E%E9%AA%8C%E6%80%9D%E6%83%B3%E6%8C%87%E5%AF%BC.md | 2026-07-15 | `main`；非当前论文分支        | 项目概念材料                     |
| P-MISSING-READINESS | docs/INFOCOM_READINESS.md                    | Ricardo3319/Micro_banch | https://github.com/Ricardo3319/Micro_banch/blob/codex/rescuesched-baselines/docs/INFOCOM_READINESS.md | 2026-07-15 | 当前分支及 main 均测试        | 访问状态：404，未使用            |
| P-MISSING-RUNBOOK   | docs/PHYSICAL_MACHINE_RUNBOOK.md             | Ricardo3319/Micro_banch | https://github.com/Ricardo3319/Micro_banch/blob/codex/rescuesched-baselines/docs/PHYSICAL_MACHINE_RUNBOOK.md | 2026-07-15 | 当前分支及 main 均测试        | 访问状态：404，未使用            |

## 17.4 Related-work 书目核验来源

| ID     | 页面标题                                           | 发布机构 | URL                                           | 访问日期   | 适用年份   | 证据等级             |
| ------ | -------------------------------------------------- | -------- | --------------------------------------------- | ---------- | ---------- | -------------------- |
| L-ALTO | dblp: MICRO 2022 proceedings entry for ALTOCUMULUS | DBLP     | https://dblp.org/db/conf/micro/micro2022.html | 2026-07-15 | MICRO 2022 | 学术索引；非投稿规则 |

---

# Appendix A. Unresolved Rules Register

以下项目在 2026-07-15 仍需 INFOCOM 2027 官方确认：

1. 摘要注册是否单独、日期和冻结字段；
2. 完整论文截止的具体时刻与时区；
3. 投稿平台和 EDAS conference ID；
4. LaTeX/Word 当届模板链接与 `IEEEtran` 模式；
5. 双栏、字号、纸张；
6. 页数和 references 是否计页；
7. appendix 和 supplementary material；
8. PDF 文件大小/数量；
9. single- 或 double-anonymous；
10. 自引、arXiv、公开代码和匿名 artifact；
11. author list 修改；
12. COI 年限和域名规则；
13. rebuttal/author response；
14. notification 和 camera-ready；
15. artifact track/badge；
16. Open Access 选项/APC；
17. PDF eXpress ID 与阶段；
18. author registration、presentation/no-show；
19. demo/poster/workshop 的交叉投稿规则；
20. 正式 desk-reject 清单。

---

# Appendix B. Audit Changelog

| 日期       | 变更                                                         |
| ---------- | ------------------------------------------------------------ |
| 2026-07-15 | 初版并完成结构校验。确认 2026-07-31 Call for Technical Papers Deadline 和 2027-05-24 至 2027-05-27 会期；详细 Author Information/EDAS 仍未确认。补充 INFOCOM 2026 官方活动页的 PROVISIONAL 年际参考，并明确其年度站点不可读时不补猜页数/匿名。明确 RescueSched 为唯一主线；AQB/DQB 为历史材料。核实 ALTOCUMULUS 的 MICRO 2022 书目信息。 |

