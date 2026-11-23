# 事件、压缩与阶段（参考） {#events}

> 本节是**当前代码**的事件模型参考（不是演进史，也不含"以后要做"）。名称以
> `include/hwshqtb/metaobject/event_t.h`、`event_dispatcher.h`、`event_loop_t.h` 与
> `include/hwshqtb/gui/window_event_t.h` 下的声明为准。

## 1. 一轮循环

`event_loop_t::exec()` 在没有设置阶段时就是 `while (!quit) dispatcher.process_events(flags);`，
而 `process_events` 每轮做三件事：

1. 触发到期定时器（把它们投进队列）；
2. **泵一个**系统事件（队列为空时先阻塞等待，最多 10 ms）；
3. 排空"开轮时"投递队列里的条目——本轮新投的留到下一轮。

所以逐事件模式下，**一轮 = 一个系统事件**。一串输入（连续移动、拖动）就是 N 轮。

### 系统事件从哪来

`event_dispatcher` 是**抽象类**：队列、合并、阶段预算、延迟派发、退出标志都在基类里，**平台只负责四件事**——
`start_platform` / `stop_platform` / `fetch_native`（泵一条并投递）/ `wait_native`（只阻塞等待，不投递）。

因此 `metaobject` **没有任何平台依赖**：不安装任何实现时，`instance()` 返回内建的最简实现
（不泵、不阻塞，事件全靠 `post_event` 进入），所以这个库可以跑在**纯软件模拟**上——测试正是这么做的。

真实平台上由 `gui` 提供实现（SDL3）：它负责视频/事件初始化、`SDL_PollEvent` 泵、`SDL_WaitEventTimeout` 等待。
原生消息的**翻译**不在分发器里——那由**平台半边**装的原生过滤器完成：`gui::detail::sdl_platform_window_t`
（`src/gui/`）把一条原生事件翻成**一步** `interaction_t`（后端无关的值：`point_t` / `mouse_button_flag` /
`modifier_t` / `int` / `std::string_view`），或翻成 lifecycle 上报（尺寸、移动、expose、关闭、焦点、指针离开），
交给自己的窗口；泵只负责把消息喂出来，`metaobject` 里没有任何后端类型。

## 2. 事件合并

### 2.1 协议

```cpp
virtual bool merge_with(const event_t& newer);
```

只有在事件**还挂在队列里、尚未派发**时才会被调用；合并后的事件**保留先到那条在队列里的位置**，
因此不同类型之间的相对顺序不变。默认实现返回 `true`（保留先到、丢弃后到）；需要"后者载荷胜出"
的类自行覆写并把载荷搬进来。

### 2.2 开关

`event_dispatcher::set_event_compression(bool)` **默认关闭**。关闭时 `post_event` 逐条入队，行为与
引入合并之前完全一致；`compressed_event_count()` 给出累计被吸收的条数（诊断与测试用）。

合并要求**同接收者 + 同事件类型 + 同优先级**。跨优先级不合并，否则把高优先级事件折进更早的普通
事件会让它被排到后面，破坏优先级语义。

### 2.3 拒绝合并的事件

合并对下面这些事件**不是幂等操作，而是把工作删掉或者把事实删掉**，因此它们显式覆写为
`merge_with → false`：

| 事件 | 合并的后果 |
|---|---|
| `timer_event_t` | 少一次定时回调 |
| `meta_call_event_t` | 少执行一次调用；blocking 形式被删除会触发 `notify_cancelled()` |
| `deferred_delete_event_t` | 丢掉删除请求，对象不释放 |
| `child_event_t` | 少看一次结构变化：两条同类型事件指向**不同的子对象**，合并后第二条连同它的子对象指针一起消失 |
| `dynamic_property_change_event_t` | 少看一次属性变化：两条同类型事件可能指向**不同的属性名**，合并后第二个属性完全不会被观察到 |

判定标准是"合并后剩下的是状态还是少了事实"：**状态**（窗口尺寸、位置、指针位置、expose）合并后
仍然描述当前情况，接受合并；**事实**（工作请求、结构变化、属性变化）每一条都必须送达。

其余事件（窗口状态事件、输入事件、自定义事件）默认接受合并。**输入事件被合并是有损的**：两次不同
键的 `KEY_PRESS`、两次不同按钮的 `MOUSE_BUTTON_PRESS` 在一个阶段内只送达一条——这是"一阶段 = 一个
状态"的代价，需要每一次输入的场合应关闭合并。

## 3. 阶段

### 3.1 形状

```cpp
struct event_phase_t {
    std::chrono::milliseconds budget{2};    // 一个阶段收集+派发的时间预算
    std::size_t max_events{1024};           // 单阶段派发条数上限（洪流兜底）
    bool paint_last{true};                  // PAINT 在本阶段最后派发
    bool merge{true};                       // 本阶段是否合并（一个开关，不按类型分档）
};
```

`budget` 与 `max_events` 同时也是**最大绘制延迟**：阶段不会无限收拢。

### 3.2 一个阶段做什么

`event_dispatcher::process_phase(flags, phase)`：

1. 用 RAII 把压缩开关设置成 `phase.merge`，**阶段结束恢复进入前的原值**（异常路径同样恢复）；
2. 触发到期定时器；
3. **阻塞/非阻塞切换点**：队列为空时第一次取事件走阻塞等待（空闲睡眠），之后转非阻塞把当前可用的
   系统消息抽干，直到没有消息、`budget` 用尽或被打断。预算从唤醒之后开始计；
4. `merge` 打开时先做一次**存量压缩**：把阶段开始前已排队的同接收者/同类型/同优先级条目折叠——仅在
   阶段内设置压缩开关只能压到"阶段中新投"的事件，压不到已经躺在队列里的；
5. 按原顺序派发非 `PAINT` 条目；**属于阶段开始快照的 `PAINT`** 暂存到最后派发；
6. 只派发暂存的 `PAINT`；若预算或条数上限在第二趟之前用尽，暂存的 `PAINT` 放回队列留给下一阶段。

阶段内**新投**的 `PAINT`（例如 `resize_event` 的处理里调用 `update()`）不属于本阶段快照，一律留到
下一阶段，不在本阶段补派——否则阶段永远结束不了。

### 3.3 打开方式

```cpp
event_loop_t::set_phase(std::optional<event_phase_t>);   // 循环级
core_application::set_event_phase(std::optional<event_phase_t>);   // 应用级，必须在 exec() 之前
```

- 循环在 `exec()` 开始时**快照**阶段设置，槽函数里修改只影响下一次 `exec`；
- `process_events(flags, maximum_time)` 在已设阶段的循环内也走阶段路径（嵌套调用继承同一策略）；
- `POSTED_ONLY`（`thread_t` 的循环）**忽略**阶段：阶段泵会调用平台的系统泵，而那是主线程的事。

## 4. 帧与绘制

- `window_t::update()` 投一个 `event_type_enum::PAINT` 事件；`paint_pending` 保证每个窗口最多一条待派发；
- `PAINT` 事件的处理只有一件事：`repaint()`——**它与 `paint_event(painter_t&)` 不是两条绘制路径**，
  后者仍是唯一的绘制入口；
- `paint_frame(bool consume_input)`：正常帧传 `true`（画完重置输入边沿，一次绘制消费一次边沿）；
  **模态补帧传 `false`**（见下），因为那些边沿还没被派发过。

### 输入：一步一个 `interaction_t`

输入不是窗口上的一堆具名方法（没有 `handle_mouse_*` / `handle_key_*`），而是**一个事件对象带全部载荷**，
和 `QWindowSystemInterface` 一样：平台填**一步** `interaction_t`，交给唯一的入口
`window_t::handle_input(const interaction_t&)`。哪几个字段有值就说明这一步是什么：

| 这一步是什么 | 字段 |
|---|---|
| 指针移动 | 只有 `mouse`（窗口坐标）+ `buttons`（按键 LEVEL） |
| 按下 / 松开 | `button` + `mouse_pressed` / `mouse_released` 之一 |
| 滚轮 | `wheel`（非零） |
| 按键 | `key`（非零）+ `key_pressed` / `key_released` 之一 |
| 提交文本 | `text_input`（非空） |

窗口是唯一知道像素密度的一方：它把 `mouse` 从**窗口坐标**换算到像素空间（`pixel_point_of`），据此维护
`input_state`（位置、修饰键、滚轮累积、文本累积、按住的键、按键电平与边沿），然后把**换算后**的 step 交给
`interact_event()`，再清掉滚轮/文本累积，最后 `update()`。平台从不换算，也不维护任何每窗口输入状态。

### 窗口 → 宿主的落点

窗口不认识部件树，宿主（`ui::widget_window_t`）也不认识原生事件。两者之间只有五个普通成员函数，
由窗口的钩子转发；不是过滤器，也就没有安装顺序可仲裁：

| 窗口钩子（`gui::window_t`） | 何时调用 | 宿主落点 |
|---|---|---|
| `paint_event(painter)` | 每一趟绘制 | `paint(painter)` |
| `resize_event(event)` | 队列里的尺寸变化 | `resized(event)` |
| `interact_event(event)` | 每一步输入（移动/按下/松开/滚轮/按键/文本） | `interact(event)` |
| `focus_lost_event()` | 键盘焦点离开 | `focus_lost()` |
| `pointer_leave_event()` | 指针离开客户区 | `pointer_left()` |

`interact_event()` 收到的是上面那**一步**（已经换算到像素空间）：指针换算
（`window_to_pixel_point`）与 `input_state` 的写入都在窗口里完成，平台只交窗口坐标的 `point_t`。
自带窗口的宿主（`widget_window_t(title, size)`）用内部 `detail::tree_window_t` 提供这五处转发；挂到
别人的窗口时由那个窗口子类自己转发。

### 表面属于 backing_store_t

帧的持久表面不是平台窗口的一部分，而是 `gui::backing_store_t`（Qt 的 `QBackingStore`）：
`resize` / `begin_paint` / `end_paint` / `flush` / `scroll` / `is_valid` 都是它的，平台窗口只通过
`platform_window_t::backing_store()` 把它**交出来**（拒绝时返回 nullptr）。`window_t` 驱动它：
每帧 `resize(pixel_size())` → `begin_paint()`（返回 false 说明不能进表面，直画窗口 target）→ 画 → `end_paint()`；
有表面就 `flush(整窗)`（`flush` 自己会 present，不要再调 `present()`），无表面才 `present()`。

### 局部绘制，整体呈现

绘制是**局部**的：一帧只为脏区里每个矩形铺一趟裁剪，没有脏区就**不绘制、不 flush、不 present**。
但**呈现（flush）必须覆盖整个后备表面**，不能只贴画过的那几个矩形。

原因是窗口 target 不是持久表面：SDL3 的 D3D11 renderer 给普通窗口建的是 **2 个 buffer 的 flip
模型**，所以帧 N 画进的那个 buffer 里装的是**帧 N-2** 的画面，
`presented(N) = presented(N-2) + 本帧贴上去的矩形`。只贴脏区意味着其余像素停在一帧之前——
滚动后移位过的像素永远到不了屏幕（残影），两帧前被画成悬停色的行会复活（离散高亮）。

这条只在**离屏测试**里看不出来：dummy/software 渲染器的 target 是持久的，局部贴图恰好等价于整屏贴图。
所以渲染路径的测试必须按"present 后画面 = 本帧贴上去的部分 + 两帧前的画面"建模，
否则"局部绘制"的 bug 会在单元测试里全绿。

代价是每帧一次 1:1 的表面贴图（整窗一次），与树的重绘量无关：静止窗口仍然零绘制、零 present。

## 5. 平台模态尺寸循环的补帧

手动拖动窗口边框时，线程停在 `SDL_WaitEventTimeout` 的 `DispatchMessage` 里，进入系统的模态尺寸
循环（`WM_ENTERSIZEMOVE` … `WM_EXITSIZEMOVE`），阶段泵执行不到。这段代码整个属于**平台半边**
（`gui::detail::sdl_platform_window_t`）：它在 Windows 上挂一个窗口消息钩子，并调用窗口的两个
「平台实现调用」入口（`handle_resize(true)` / `repair_frame()`）。

| 消息 | 动作 |
|---|---|
| `WM_ENTERSIZEMOVE` | 置标志，`handle_expose()`（整窗置脏 + 调度一帧 + 排队 expose 钩子），`SetTimer(hwnd, size_move_timer_id, 16, nullptr)` |
| `WM_TIMER`（id 匹配且标志已置） | `handle_resize(true)` 同步真实尺寸（变化则**直接**调用 `resize_event`，不投递——此时没有泵，并且 `update()` 会落到 `defer_repaint()` 上把补帧置脏）；若脏则 `repair_frame()`（= `paint_frame(false)`） |
| `WM_EXITSIZEMOVE` | `KillTimer`，清标志；积压的 SDL resize/expose 交给正常阶段泵 |

约束：

- 钩子里**不得**调用 `SDL_PollEvent` / `SDL_PumpEvents`（会在模态循环里再套一层）；
- 补帧**不消费输入边沿**，也不投递事件；
- 尺寸同步是幂等的：分派路径与当前尺寸比较后跳过，模态期间已经处理过的 resize 不会在循环结束后重复重排；
- 拖拽期间 `update()` 走 `platform_window_t::defer_repaint()`：平台接管这一帧（只置脏），由 16 ms 定时器画。

## 6. 实测

`examples/ui_demo --self-test` 一次性注入 20 条 `MOUSE_MOTION`：

| 模式 | 绘制帧数 |
|---|---|
| 阶段模式（默认） | **1** |
| `--no-phase`（逐事件） | **20** |

自检同时验证模态补帧路径：伪造 `WM_ENTERSIZEMOVE` + 两次 `WM_TIMER` → 补帧被执行、未派发的输入边沿
被保留、`WM_EXITSIZEMOVE` 之后 `WM_TIMER` 不再触发补帧。

## 7. 边界

- `SDL_SetWindowsMessageHook` 是进程级单钩子；本实现自建 HWND 表，但若别的库后装钩子会覆盖它；
- `SetTimer` 失败时模态期间不补帧（无回退）；
- 补帧里的用户回调若抛异常会穿过 Win32 回调（与现有事件派发同类风险）；
- 阶段压缩是**每调度器**的（本线程队列），跨线程投递按各自调度器的开关处理；
- 不同 `event_loop_t` 对象之间的嵌套 `exec()` 不继承外层阶段；只有同一循环的
  `process_events(flags, max_time)` 使用它自己的阶段设置。
