# CloudLab 实测入口

## 状态与选择

用户可申请 CloudLab 物理机；目前尚无已分配节点和登录信息。已经提供物理节点 profile、Linux UDP 测量工具和本机协议自检；**尚未部署真实多机 QBR 系统**。

根据用户的资源限制，当前推荐申请 Utah 的 **3 台 c6525-25g**：1 台合并负载发生器与入口调度器，2 台执行主机。client 上两个功能绑定到不同物理核心，并记录共享 CPU/NIC 的干扰。该配置支持双执行主机迁移和基本策略对比；多源向第三台目标迁移与规模实验留待扩容。配置见 [申请说明](../cloudlab/README.md)。

CloudLab 区分管理网和实验网；实验流量须使用 profile 配置的实验网地址。节点型号、链路和可用资源随站点而异，以申请页面为准。[官方硬件说明](https://docs.cloudlab.us/hardware.html)、[控制网与实验网说明](https://docs.cloudlab.us/control-net.html)。

## Profile

直接导入 `cloudlab/qbr-3node-utah.rspec`，或上传默认等价的 `cloudlab/profile.py`。型号和 Ubuntu 22.04 镜像沿用用户原配置，每个实验 LAN 接口请求 25 Gbps。地址为：

- client（含 dispatcher）：`10.10.1.1`
- worker1–worker2：`10.10.1.11`、`10.10.1.12`

该文件按官方 `RawPC`、`LAN`、接口与 IPv4 地址 API 编写，当前环境未安装 geni-lib，未在 CloudLab 门户实例化验证。[geni-lib 官方说明](https://docs.cloudlab.us/geni-lib.html)。

## 编译与最小测量

将项目复制到两台节点，然后分别执行：

```bash
bash scripts/build_probe.sh
```

worker1，先启动回声端：

```bash
./build-qbr/net_probe server --bind 10.10.1.11 --port 19000 --max-packets 11000
```

client，发送端：

```bash
./build-qbr/net_probe client --bind 10.10.1.1 --peer 10.10.1.11 --port 19000 --bytes 128 --train 1 --warmup 1000 --iterations 10000 > rtt-128-train1.json
```

`--cpu N` 可将进程固定到已确认的空闲 CPU。先检查 CPU/NUMA 拓扑，客户端、服务线程和网卡中断的布局要记录；不要把固定 CPU 编号直接套到所有机器。

批量发送的成本测量将 `--train` 改为 8，服务端 `--max-packets` 相应改成 88000。payload 可取 64、128、512、1200 字节。工具限制 payload≤1400 字节以避免默认 Ethernet MTU 下的 IPv4 分片；较大批次是多个独立数据报，未伪装成单个免费大包。

服务端 `--service-us 5` 或 `100` 可加入人工服务时长。它是单线程忙等负载，用于链路/服务叠加诊断，不能替代多核 RPC 执行器或应用实测。

输出包含 RTT P50/P99/P99.9、整组完成时间、丢失包数和闭环发送速率。**闭环 ping/pong 速率不是开放负载系统吞吐；回环自检数字也不是跨主机 RTT。** 丢包不能只从延迟样本中删掉，需要同时报告丢失比例。

## 测量如何进入模型

1. 记录 CPU 型号、内核、网卡、驱动、实验网接口、MTU、链路速率、CPU 绑定和网卡中断合并配置。保持配置一致后再比较策略。
2. 对每个 payload/train 组合运行多次，保留原始 JSON；确认实验 IP 的路由经过实验网。
3. RTT 包含双方软件处理、两程网络和链路成本，不能不加说明地把 RTT/2 当成单程传播延迟，再额外叠加同一组已包含的软件开销。优先用测量分布校准整条消息路径，必要时分别测量序列化与 handler 成本。
4. 在测量所得范围内重新扫描仿真参数；若实际往返已接近短任务 SLO，缩小或调整目标服务时间，而非沿用虚构的 3.15 μs 单程值。
5. Linux UDP 是第一阶段测量路径。若后续改用 RDMA/DPDK，必须重新测量并为所有基线提供一致路径。

## 真实系统论文仍需完成的部分

- 多 worker 软件共享 FIFO 与可见服务能力估计。
- 使用同一网络框架的入口 JSW/JBSQ、批量窃取和 QBR。
- 带请求 ID 的真实 token、commit、expire、退回与去重状态机。
- 开放环负载发生器、跨机器总请求数对账、排空及不依赖时钟同步的端到端响应测量。
- 服务应用和容量扰动，多个独立运行的置信区间。

收到已分配节点后才可执行这些部署和测量；当前没有为用户创建、预订或占用 CloudLab 资源。
