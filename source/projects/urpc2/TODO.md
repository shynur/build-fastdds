# urpc2 TODO

以下是已知但尚未着手的改进项, 均围绕 `src/urpc2.cpp` 的 `urpc2::Urpc2::Impl::call()`.
它们不是遗留 bug (调用不存在 instance 会 abort 的问题已在 trunk 修复: 异常翻译 +
去掉盲 sleep), 而是主动推迟的后续工作.

## 1. 区分「从未发现 server」与「调用中途失联」

现状: `call()` 把 Fast DDS 的 `rpc::RpcBrokenPipeException` 统一翻译成
`urpc2::ServerNotFound`.  这在当前实现下是对的 —— client 侧的 broken pipe 只可能
来自「发请求时 requester 未匹配」(见生成的 `send_request`: `wait_for_matching`
失败即返回失败码, 进而 `set_exception(RpcBrokenPipeException)`).  但「已匹配、请求
已发, 对端中途挂掉」这种真正的连接中断, 目前只会表现为 `call()` 自己的 `future`
超时 (`urpc2::Timeout`), 无法与「对端一直很慢」区分开.

改进: 引入显式的 matched-status 检测 (如 `on_publication_matched` /
`get_publication_matched_status`), 让 `call()` 能分辨:
  - 从未匹配 -> `ServerNotFound`;
  - 曾匹配、后失联 -> 新增 `ConnectionLost` (作为 `Timeout` 或独立子类).

难点: 生成的 `gen::Processor` client 接口只暴露 `router()`, 未透出底层 requester
的匹配状态, 需要另找途径 (participant 层发现事件, 或封装自己的 requester).

## 2. 发现超时可配置

现状: 去掉盲 sleep 后, 一次调用的「发现预算」等于 Fast DDS 内部 `send_request`
里硬编码的 `wait_for_matching` 超时 (3 秒), 无法调整.  在慢网络上, 一个真实存在
但发现较慢 (>3s) 的 server 可能被误判为 `ServerNotFound`; 旧的 5s pre-sleep 反而
更宽容.

改进: 给 `call()` (以及 rbk 层的封装) 增加可配置的发现超时, 与「等回复」的
`timeout` 参数分开.  由于 Fast DDS 生成的 client 未暴露该内部超时, 可能需要在
发送前自行等待 requester 匹配到目标预算, 再调用 `router()`.

## 3. 缓存 client participant / requester

现状: `call()` 每次都新建一个 `DomainParticipant` + client, 用完即毁.
`DomainParticipant` 是较重的 DDS 对象, 按每次 RPC 创建/销毁是明显浪费.

改进: 按目标 instance 名缓存 client (participant/requester), 跨多次 `call()` 复用
(registry 里已经缓存了 server 端的 `Urpc2` 实例, client 载体可同理处理).  注意
线程安全与生命周期 (参考 `urpc2_rbk.cpp` 里 registry「故意泄漏、永不析构」以规避
Fast DDS 工厂单例析构顺序问题的做法).
