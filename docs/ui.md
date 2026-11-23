# 控件层（参考） {#ui}

> 本节是**当前代码**的 `hwshqtb::ui` 参考（不是演进史，也不含"以后要做"）。名称以
> `include/hwshqtb/ui/` 下的声明为准。

## 0. 窗口的三个角色

一个顶层窗口按 Qt 的 `QWindow` / `QPlatformWindow` / `QWidgetWindow` 分成三层，各自只有一件事：

| 角色 | 类 | 是什么 |
|---|---|---|
| 统一窗口接口 | `gui::window_t`（`include/hwshqtb/gui/`） | **具体、可单独使用、可继承**：`paint_device_t`，加帧策略——`update()` 合并与 PAINT 投递、脏区、趟数、呈现、滚动累积、`input_state`、截图。内部只持 `std::unique_ptr<platform_window_t>`，不持任何平台类型 |
| 平台半边 | `gui::platform_window_t` + 它的实现（`src/gui/`） | 公开纯虚接口（构造受保护、不可拷贝、**头文件里没有任何 SDL 类型**）；原生实现 `detail::sdl_platform_window_t` 负责原生窗口、渲染器、引擎、事件源，把原生事件翻成**一步** `interaction_t` 与 lifecycle 上报，并把持久表面以 `gui::backing_store_t*` **交出来**（表面本身不属于平台窗口）。`src/gui/platform_window_factory.*` 是"每个平台只有一份**原生**实现"的唯一落点。第二份实现 `detail::offscreen_platform_window` 没有原生窗口，是 `gui::offscreen_window_t` 平台半边：无 video、无显示器也能跑完整帧策略 |
| 部件树宿主 | `ui::widget_window_t` | **不是窗口**：没有虚函数、不继承任何窗口类，**组合**一个 `gui::window_t`（自建并拥有，或借用调用方的），把五处落点转给部件树。自建窗口时由内部 `detail::tree_window_t : gui::window_t` 提供转发；见下面"宿主是组合，不是窗口" |

判据（这三分存在的理由）：

- **不用宿主时窗口照样能用**：`examples/hello_gui` 只继承 `gui::window_t`，一行未改，仍然 show / 画 /
  收键鼠 / 存图；
- **`src/ui` 与 `include/hwshqtb/ui` 里零 SDL**：ui 只认 `gui::window_t` 与 `interaction_t`；
- **帧策略与平台无关**：`window_t` 一行未改就跑在第二份（无窗口的）平台上，见
  `tests/gui-test/offscreen_window_test.cpp`；
- 帧策略（合并、脏区、趟数、整面呈现、滚动累积、输入边沿）全在 `window_t`；原生资源与事件翻译全在
  `platform_window_t` 的实现里；表面的像素（`begin_paint`/`flush`/`scroll`）全在 `backing_store_t` 里；
  部件树、路由与会话落点全在宿主里。

**窗口 → 宿主的落点**是五个普通成员函数（`paint` / `resized` / `interact` / `focus_lost` /
`pointer_left`），公开但非虚：自带窗口的宿主由 `detail::tree_window_t` 转发，挂到已有窗口时由那个窗口
子类转发。没有原生过滤器，也就没有安装顺序。

### 宿主是组合，不是窗口

`widget_window_t` **没有虚函数**，也不继承任何窗口类：它持有一个 `gui::window_t`（`_window`）和一棵
部件树（`_root` + `_router`）。两个构造对应两种接线方式：

| 构造 | 谁提供窗口 | 五处落点由谁转发 |
|---|---|---|
| `widget_window_t(title, size)` | 宿主自己建（`_owned_window`，内部 `detail::tree_window_t : gui::window_t`） | 那个适配器窗口 |
| `widget_window_t(gui::window_t&)` | 调用方，宿主不拥有它（`_owned = false`） | **调用方的窗口子类** |

窗口 API（`show` / `hide` / `update` / `repaint` / `present` / `close` / `is_visible` /
`close_requested` / `window_id` / `set_title` / `size` / `pending_damage` / `presented_region` /
`painted_frame_count` / `scroll` / `save_screenshot` / `save_painted_frame` / `input_state`）都是
**一行转发**，所以原来按窗口用法写的调用点不用改；`window()` 是逃生口，拿得到被组合的那个窗口。

判据：**不使用 `widget_window_t` 时，`gui::window_t` 单独就能 show / 画 / 收键鼠 / 存图**——
`examples/hello_gui` 就是直接继承 `gui::window_t` 的，一行都不用改。

## 1. item：树上的节点

`item_t` 是这一层的根：它继承 `object_t`（于是有属性、信号与运行时类型），并加入"视觉节点"的契约。

| 契约 | 内容 |
|---|---|
| 坐标 | `geometry()` 相对**父 item**；`rect()` 是同一矩形在自身坐标下的 `{0,0,w,h}` |
| 绘制 | `paint(painter_t&)`，painter 已平移到父原点并裁剪到父矩形；**非 const**（允许更新缓存） |
| 命中 | `hit(point_t local) -> item_t*`，输入是**本 item 坐标** |
| 交互 | `interact(const interaction_t&) -> bool`，返回是否消费该步 |
| 可见/启用 | `is_visible()` / `is_enabled()`；`is_visible_in_window()` 沿父链回答"是否真的看得见" |
| 尺寸 | 见第 4 节 |
| 焦点 | 见第 3 节 |

`is_item()` 是遍历用的标记（一个节点是不是 item），`is_widget()` 只用于**类型检查**
（"它是不是 `ui::widget_t`"），不要拿它做遍历判断。

## 2. 两种 item 宿主

（窗口级的三个角色见第 0 节；这里说的是**树内**的两种宿主。）

| | `widget_t`（保留式） | `im_host_t`（即时式） |
|---|---|---|
| 是什么 | 带属性/信号、子项与布局的 item | 叶子 item，一个"每帧重新生成内容"的区域 |
| 子项 | `item_t` 树，由布局摆放 | 不构成 item；行/控件由 frame handler 现建现用 |
| 绘制 | 遍历子树 | 每帧调一次 handler，控件在其中被驱动并绘制 |
| 输入 | 由 router 按命中/焦点投递 | 由宿主把本帧的交互批次攒起来，逐控件重放 |

**一套控件代码服务两者**，靠三条契约：

1. 每个持久状态都有 setter（`set_checked` / `set_value` / `set_text` …），所以调用方可以自己保存值、
   每帧灌进控件，控件不假定自己跨帧存活；
2. **User 属性只报"本帧由用户操作引起的事件"**，不承担持久状态；
3. 状态变化必须 `update()`（局部重绘的脏区），控件的 setter 与交互路径都已经这么做。

对手柄：`im_host_t::control(type, name)` 按名字找或建一个控件、重放本帧批次、绘制、返回它的 User 属性；
`widget(type, name)` 只拿控件不驱动。类型没有 User 属性时 `control()` 返回**有效的 void any_t**，
真失败（名字为空、类型未知、不是 widget）返回**默认构造的无效 any_t** 并记一条 Warning。

## 3. 焦点与指针状态

焦点、hover、按下、指针捕获**都是 item 状态**（`has_focus` / `is_hovered` / `is_pressed` /
`captures_pointer`）。原因：输出可以有多份、输入只有一条，视图只读这些状态。

| 焦点策略 | 语义 |
|---|---|
| `NO_FOCUS` | 永不获得焦点（默认） |
| `CLICK_FOCUS` | 内部按下时聚焦；也可被 Tab 到达 |
| `TAB_FOCUS` | 只能被 Tab 到达，按下不聚焦 |
| `STRONG_FOCUS` | 按下与 Tab 都行（可交互控件的默认） |

`set_focus()` / `clear_focus()` 沿 item 父链走到**最近的焦点汇点**；宿主（`im_host_t`）自己也装汇点，
所以内层控件聚焦不会被外层抢走。按下聚焦发生在控件 `interact()` **之前**，所以控件在同一帧就能看到
`has_focus()`。

## 4. 布局与尺寸

一个 item 把尺寸说两遍，两者绝不混用：

- `size_hint()` / `minimum_size_hint()` 是**内容意见**：实现只读自己的内容，**不得**读硬约束；
- `minimum_size()` / `maximum_size()` 是调用方装的**硬约束**；`set_fixed_size()` 没有第三个真相，
  它就是"两者相等"。

布局只问三个有效值：

```
effective_size_hint()    = clamp(max(size_hint, minimum_size_hint), minimum_size, maximum_size)
effective_minimum_size() = 按策略取地板（FIXED 不收缩到 hint 以下；其余到 minimum_size_hint）
effective_maximum_size() = maximum_size()
```

分配规则：空间富余时按 `EXPANDING` 与 `stretch` 分配、触 `maximum` 封顶后再分；空间不足时在
`hint` 与地板之间按余量比例削减；**连地板之和都放不下时，按 Qt 的做法"从最大的开始砍"**，结果精确
适配父矩形（允许舍入误差），不溢出。

容器（`group_box_t` / `tab_widget_t` / `scroll_view_t`）与 `layout_t::minimum_size()` 都用这三件套
聚合子项，所以"子项设了 min/max，容器不认"不会发生。

**对齐**（`item_t::set_alignment`，Qt 的 `QLayoutItem::alignment`）：**没有对齐位就是填满格子**（默认，
与引入对齐之前逐字节一致）。两轴独立：只设水平位时垂直仍填满。多个同轴位同时设置时按 Qt 的解析顺序
（水平 `RIGHT` > `LEFT` > `HCENTER`，垂直 `BOTTOM` > `TOP` > `VCENTER`）。

对齐在两条轴上的表现不同，这一点与 Qt 一致：

- **box 的交叉轴与 grid 的 cell** 有多余空间 → 子项按对齐位**移动**（居中、贴右、贴底…）；
- **box 的主轴**格子就是解出来的槽、没有余量 → 带对齐位的子项分配上限被压到 `size_hint()`，并且不再
  参与该轴的扩张。所以主轴上的对齐位改变的是**尺寸**（不再填满），不是位置。

## 5. 控件

建模是成对的：**USER 事件是"改变状态的那个事件"，状态是它的 `notifier` 指向的可写属性**。两者由元数据里的
notifier 链接在一起，不需要额外标记。即时宿主的状态仓库就是**沿这条链接**找到唯一的状态属性来快照的。

| 控件 | USER 事件 | 状态属性 | 说明 |
|---|---|---|---|
| `push_button_t` | `clicked` | —（命令，无状态） | 背景 + 边框 + 居中文字；按下/悬停反馈 |
| `check_box_t` | `toggled` | `checked` | 指示器 + 文字；空格或点击翻转 |
| `radio_button_t` | `toggled` | `checked` | 同父自动互斥；`radio_group_t` 可跨父分组 |
| `slider_t` | `value_changed` | `value` | 水平/垂直；拖动、方向键、翻页键、Home/End |
| `scroll_bar_t` | `value_changed` | `value` | 手柄拖动、槽内翻页、键盘 |
| `line_edit_t` | `text_changed` | `text` | 光标/选区/密码模式/只读；只接受位图字体覆盖的字符 |
| `label_t` | — | —（文字是配置） | 文字与对齐；不可聚焦 |
| `progress_bar_t` | — | —（每趟由过程给） | 只读进度 |
| `scroll_view_t` | — | — | 单内容 item + 两根滚动条；滚轮/键盘/快路径 |
| `group_box_t` | — | — | 边框 + 标题 + 内容区 |
| `tab_widget_t` | — | — | 页签条自绘；只有当前页可见 |
| `list_view_t` | — | — | 见第 7 节 |

没有 User 属性的控件在即时宿主里只能经 `widget()` 配置，`control()` 对它们返回 void any_t。

## 6. 样式

控件不写死颜色与度量。`style_t` 只有两个入口：

```cpp
int  pixel_metric(pixel_metric_enum) const;                       // 内边距、边框宽、行高、条厚…
void draw_control(control_element_enum, painter_t&, rect_t,
                  const control_state_t&) const;                  // 背景、指示器、槽、手柄、文字…
```

`control_state_t` 携带 `enabled / hovered / pressed / focused / checked / text`。默认实现是
`default_style()` 返回的全进程唯一实例；换肤就是换一个 `style_t` 实例，控件不动。

## 7. 脏区域、绘制与列表

绘制是**局部**的：`item_t::update()` / `update(rect)` 把矩形映射到窗口坐标并并进该 item 与**所有祖先**；
没有脏区就**不绘制、不 flush、不 present**。

**脏区域作为数据往下传**（Qt 的做法）：`paint_event_t` 携带"本 item 坐标下的脏区域"，每个 item 只收到与
自己矩形求交后的那块；**不透明子项会从父的剩余区域里被减掉**，完全被遮挡的子树**根本不被访问**。

**执行方式按后端能力选**：区域只有一个矩形时直接交给后端裁剪（最常见的情况，零额外开销）；区域有多个
矩形且引擎声明 `clip_region` 能力时，后端一趟画完；否则**调用方按矩形拆分**，逐矩形一趟。SDL 的渲染器
只有单矩形裁剪（`SDL_SetRenderClipRect`），所以它不声明该能力。

**呈现必须覆盖整个后备表面**：窗口 target 不是持久表面（flip 模型下帧 N 的 buffer 里是帧 N−2 的画面），
只贴脏区会让其余像素停在一帧之前。

滚动分三层，各自只做一件事：

| 类型 | 职责 |
|---|---|
| `viewport_t` | **取景器**：把给定 item 的某个区域（`source_rect()`）画到自己的某个位置（`destination_rect()`）。不建滚动条、没有偏移，也不需要继承——设好内容与两个矩形就能直接用 |
| `scroll_view_t` | **滚动层**：自己创建两根滚动条，拥有偏移与范围；"滚动"就是改 `source_rect()`，**内容 item 的几何不变** |
| `abstract_item_view_t` 及其子类 | 模型、选择、当前行、键盘；行由一个内部的**内容 item** 绘制，表头是**画在内容之上的子控件** |

两个矩形要分清：

| 名字 | 是什么 |
|---|---|
| `body_rect()` | 视口自己的绘制区：`rect()` 扣掉滚动条占位 |
| `destination_rect()` | 内容落到的位置（默认 `body_rect()`；表格会扣掉表头带） |
| 可拷贝区（实现内部） | 快路径允许搬运的像素：`destination_rect()` 再扣掉本类画在 widget 外缘的框线 |

快路径只可以**拷贝**可拷贝区；框线、表头这些装饰**不随内容移动**，由 `scroll_view_t` 自己标脏重画。
这条规则与画框的代码在同一个类里，所以"画了装饰却忘了排除它"不可能再发生。

滚动走快路径：容器 `notify_scroll(rect, dx, dy)` 请求位移已画好的像素并只重画露出的条带。
**位移矩形内的 pending damage 必须随像素一起平移**——否则两个位移之间挂起的脏区会落在旧坐标上。

`list_view_t` 是自绘的列表：行是**数据**（`item_model_t` 的 `data(row, role)`），不为每行建 item；
可见行按 `需要显示的行区间 = 偏移 / 行高` 计算，所以 `data()` 的调用次数只与视口有关；选择、当前行、
滚动偏移都在视图 item 上；模型的三条通知分别标脏对应行、更新滚动范围、或整体重置。

## 8. 文字：描述 → 解析 → 度量 → 整形 → 光栅化

文字是五层，每层只回答一个问题；**内置字面是回退链的最后一环，也是测试确定性的来源**。

| 层 | 谁 | 回答什么 |
|---|---|---|
| 描述 | `gui::font_t` | "我要哪个家族、多大像素"。空家族 = 数据库默认家族，像素 0 = 该家族的默认大小；它是纯值类型，没有字形 |
| 解析 | `gui::font_database_t` | "这个描述对应哪个引擎"。进程级单例，缓存 `(家族, 像素) → engine`；默认家族 = 系统 UI 面（`lfMessageFont`），拿不到就是内置。**`engine_for()` 是 `noexcept`：解析失败一律回答内置**，所以文字不会因为缺字体而消失 |
| 度量 | `gui::font_engine_t` | "ascent/descent/leading、这个码点的 advance、这个字形的 8bpp 蒙版"。`detail::fallback_font_engine_t` 按 `[主引擎, 系统面, 内置]` 顺序逐字符问，命中即用，并把"第几个子引擎"编进字形索引 |
| 整形 | `gui::text_layout_t` | "这段 UTF-8 排成哪些字形、笔位在哪"。一次解码（`utf8.h`）得到**码点**，逐码点问引擎要字形，按引擎分段产出 `glyph_run_t`，并给出 extent |
| 光栅化 | `paint_engine_t::draw_glyphs` | "把这些字形画出来"。SDL 后端按 `(engine, glyph)` 缓存纹理（上限 1024 张，整批清空，不做 LRU）；平台引擎自己的 `glyph_bitmap()` 负责把 1bpp/4bpp 源统一铺成 8bpp |

三条不变量：

- **一个码点 = 一个字形 = 一次 advance**（不是一字节）：`"中"` 是 3 字节、1 格。
- **字形索引必须有引擎**：`glyph_run_t` 带 `engine` 指针，索引 0 是该引擎的缺字框。
- **确定性靠钉住**：测试与截图哈希在创建任何控件之前调 `gui::pin_builtin_font_family()`，把默认家族切回内置字面；系统字体只用于语义验证（`ui_demo --screenshot-font`）。

## 9. 边界

- 没有 delegate、没有单元格编辑、行高固定；表格与树不在本层；
- 对齐只影响"摆放"与主轴上的"分配上限"：交叉轴与 grid 移动位置，主轴把上限压到 `size_hint()`；没有
  RTL / 布局方向；
- 内置位图字体只有 ASCII 32–126 的字形（非 ASCII 能存能排版，但画成缺字框）；系统字体后端有真实字形，中文可以显示，但**一行里不同引擎的字形按主引擎的基线对齐**，没有逐字形的垂直边距（垂直偏移由引擎烘进蒙版；水平由 `glyph_bitmap_t::bearing_x` 提供）；`line_edit_t` 仍只接受位图字体覆盖的字符；
- 字形的**水平**边距由引擎填进 `glyph_bitmap_t::bearing_x`，渲染时加在笔位上（`pen.x() + bearing_x`）；**垂直**偏移不进字段——引擎已经把它烘进蒙版（蒙版覆盖整个行盒），所以渲染器把蒙版放在 `pen.y - engine->ascent()` 即可。行盒的高度取引擎的 `ascent() + descent()`，**不是** `font_t::glyph_height()`（那是内置 5×7 字面的常量，用它在系统字面上会基线偏低、字形被控件下边缘切掉）；
- 绘制没有独立的对象：`item_t::paint(painter, damage)` 是树自己的成员，树递归到自己身上。**帧决定趟数、清屏和裁剪**
  （`window_t::draw_frame`），平台半边从不参与这个决定，`ui` 层也不认识引擎能力、不清屏——它只回答"给我区域，我画我这一块"。
  **不持有输入**：输入只有一条会话，由窗口持有（`gui::input_state_t`），经 `interact_event()` 换算成交给宿主的
  `interaction_t`，再由 router 路由。
