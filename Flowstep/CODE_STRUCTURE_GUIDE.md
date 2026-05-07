# Code Structure Guide (High Cohesion)

## 1. 模块职责边界
- `model`: 纯数据对象（Task/Event/Node/Core），不放调度决策。
- `core`: 仿真时钟、事件队列、执行推进，负责事件因果顺序。
- `algorithms`: 仅实现 B0/B1/B2/M0 决策逻辑，不操作日志落盘细节。
- `workloads`: W1/W2/W3 到达与服务时间生成。
- `metrics`: 统计聚合、分位数、CI 计算、日志写出。
- `app`: 命令行入口、配置加载、运行编排。

## 2. 关键接口（建议）
- `IScheduler::on_task_arrive(...)`
- `IScheduler::on_task_finish(...)`
- `IScheduler::on_periodic_check(...)`
- `IWorkloadGenerator::next_arrival(...)`
- `IMetricsSink::record_task_finish(...)`

## 3. 强约束
- 时间单位统一 `us`，变量名带 `_us` 后缀。
- 同时间戳事件顺序必须固定：`TASK_FINISH > TASK_ARRIVE > TASK_GENERATE`。
- B0/B1/B2/M0 只能读取相同的陈旧全局视图，不读取跨主机实时信息。

## 4. 文件拆分顺序（建议）
1) 拆 `model` 与 `core`。
2) 拆 `workloads` 与 `metrics`。
3) 再拆 `algorithms`（B1 -> B2 -> M0）。
4) 最后接入 `app/main` 与配置脚本。

## 5. 验证优先级
- 先验证可重放与日志完整性。
- 再验证 B1/B2/M0 行为差异。
- 最后做性能和统计封板。
