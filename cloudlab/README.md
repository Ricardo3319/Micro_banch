# 本轮 CloudLab 申请配置

当前优先申请 **3 台**，直接导入 **[qbr-3node-utah.rspec](qbr-3node-utah.rspec)**。使用 Python profile 的话，上传 [profile.py](profile.py)，默认也是 3 台：合并的 client/dispatcher，加两个 worker。

此前的 [五节点配置](qbr-5node-utah.rspec) 保留作后续扩容；暂时不用申请。Python 中设置 `workers=3`、`separate_dispatcher=true` 可恢复五节点拓扑。

## 申请资源

- 站点：CloudLab Utah；3 台独占裸机，统一 `c6525-25g`。
- 镜像：沿用用户原配置的 `UBUNTU22-64-STD`。
- 网络：单个实验 LAN，每节点接口请求 25 Gbps；没有额外时延或丢包注入。
- CPU/内存/磁盘由整机型号决定，不使用虚拟机资源切分。

官方列出的该型号为 AMD EPYC 7302P、16 个物理核心、128 GB 内存及 ConnectX-5 25 GbE。当前库存和镜像可用性仍以门户实例化结果为准。[硬件来源](https://docs.cloudlab.us/hardware.html)。

| 名称 | 实验 IP | 角色 |
| --- | --- | --- |
| client | 10.10.1.1 | 负载发生、入口调度、端到端时延记录和运行后日志收集 |
| worker1 | 10.10.1.11 | 共享队列、RPC 执行、本地调度与迁移 |
| worker2 | 10.10.1.12 | 同上 |

两个执行主机可以校准跨机通信，验证迁移、额度、超时退回，并比较入口调度与纠偏机制。两个独立源端同时向第三个目标迁移、多个目标选择与规模扩展暂不纳入该拓扑的实测结论。

这是一套小规模实现验证配置。需在模拟器中使用 `hosts=2` 重建输入轨迹，并校准 client/dispatcher 本地交接路径；不能直接对照此前 16 主机结果。两个 worker 只有一个远端候选，无法展示多目标选择的价值。

## CPU 与网络的运行约定

每个 worker 首轮使用 **8 个物理执行核心 + 1 个独立控制核心**；其余核心留给网络/操作系统。CPU 编号在获取节点后依据 `lscpu -e`、SMT 和 NUMA 拓扑确定，RSpec 不预设绑定。各算法保持相同核心、网络和反馈预算。

client 上将负载发生器和入口调度器绑定到不同物理核心，避免绑定到同一核心的 SMT 线程。两者仍共享内存系统和 NIC，必须记录发生器是否达到目标发送率，以及 CPU/NIC 是否饱和。程序间本地交接成本应实测，各基线保持同样的数据路径；这不等同于五节点配置中的独立入口机器。

`capacity="25000000"` 的单位为 Kbps，是每接口到 LAN 的容量请求，写法与官方 geni-lib 的 `LAN` 序列化一致；不是每对节点都预留一条 25 Gbps 独立链路。[官方实现](https://docs.cloudlab.us/geni-lib/_modules/geni/rspec/pg.html#LAN)。

配置把节点固定到 Utah，但没有保证它们在同一物理机架/交换机，也没有保证无共享链路竞争。正式记录应保存分配 manifest，并核对实验接口、路由、链路速率和 RTT。请求、回复、反馈、迁移均使用上表实验 IP。[CloudLab 实验网要求](https://docs.cloudlab.us/control-net.html)。

## 使用

在 CloudLab 新建或编辑 profile，把三节点 `.rspec` 文件全文放入 XML/RSpec 编辑器，保存后实例化；若使用 Python 源码编辑器，则上传 `profile.py`。初次按默认 3 台申请即可。

为减少门户字符校验兼容问题，两个 `.rspec` 文件均使用纯 ASCII 文本。Profile 名称建议填写 `qbr3node`，配置全文放入 XML/RSpec 编辑器。若出现 `Illegal characters`，需根据报错位置区分名称字段与配置内容；本地 XML 解析成功不代表通过门户所有字段校验。

当前文件只描述资源，不自动下载代码、运行服务、调整内核或预订资源。已做 XML 解析、节点/IP 唯一性、接口引用及容量配置检查；CloudLab 端的可分配性和 Python profile 执行仍需门户验证。

启动后把节点的 SSH 登录信息及 manifest 保存下来，即可按 [CloudLab 实测步骤](../docs/CLOUDLAB_VALIDATION.md) 开始网络校准与后续实现部署。
