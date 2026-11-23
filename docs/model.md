# 最终数据模型（参考） {#model}

> 本节是**当前代码**的数据模型参考（不是演进史，也不含"以后要做"）。名称以 `include/hwshqtb/metaobject/` 下的声明为准。

## 1. 两张表、两种行类型

| 表 | 行类型 | 列 |
|---|---|---|
| 方法表 `method_infos` | `detail::method_info_t` | `signature` / `name` / `qualifier` / `param_types` / `param_names` / `param_qualifiers` / `return_type` / `caller` / `kind` / `access` / `annotations`，以及 `has_receiver()` |
| 构造器表 `constructor_infos` | `detail::constructor_info_t` | `signature` / `name` / `param_types` / `param_names` / `param_qualifiers` / `caller` / `access` / `annotations`，以及 `has_receiver()`（恒 `false`） |

两者**没有继承关系、没有公共基类**，不是"一个行类型 + 标志位"：构造器没有隐式对象（无 `qualifier`）、没有返回类型（无 `return_type`）、没有方法种类（无 `kind`），而"不适用的列"不该存在——否则每个读取者都要先问一次标志位才能读下一列。表的索引空间也独立：`method(index)` / `method_count()` 只数方法，`constructor(index)` / `constructor_count()` 只数构造器；句柄同样分开（`meta_method_t` / `meta_constructor_t`）。`meta_info_t` 只保存**本类**的表，继承链在 `meta_object_t`（own-first），构造器表不继承。

## 2. `method_kind_enum`

`enum struct method_kind_enum : std::uint8_t { PLAIN_METHOD, SIGNAL, STATIC_METHOD }`。

- **没有 `Constructor` 值**：构造器不在方法表里，它的"种类"由句柄类型 `meta_constructor_t` 回答。
- `PLAIN_METHOD` / `SIGNAL` 有接收者；`STATIC_METHOD` 没有（`has_receiver()` 即 `kind != STATIC_METHOD`）。
- 静态成员函数是方法表的第三类行，键是裸的 `f(int)`：静态成员没有隐式对象，因此没有 cvref 后缀，也没有对象 cvref 约束（匹配器对该行整块跳过）。
- 静态**数据**成员不是行：表只描述函数。

## 3. 两套世界

| | 运行期 | 编译期 |
|---|---|---|
| 实参表示 | `std::span<const cvref_meta_type_t>`：每个实参是 `(裸类型, cvref)` 身份 | 类型包 `Ts...` |
| 单行匹配 | `method_matches(slot, args, exact, object_qualifier)` / `constructor_matches(...)` | `check_method_args<Ts...>(slot, exact, object_qualifier)` / `check_constructor_args<Ts...>(...)` |
| 按名解析 | `resolve_method(name, args, object_qualifier)` → `method_resolution_t` | `resolve_method_types<Ts...>(name, object_qualifier)` → `method_resolution_t` |
| 规则本体 | `detail::callable_matches_at<Row>` | `detail::callable_check_args<Row, Ts...>` |

- 规则**每套世界只写一份**：两个行类型共用同一个模板（`Row` 是 `method_info_t` 或 `constructor_info_t`），列不存在时相应分支被 `requires` 跳过，而不是复制规则。
- 对象 cvref 块：对象带的每个 cv 位必须也在成员上（成员可多带）；成员有 ref 限定则对象的值类别必须相等；无接收者的行整块跳过；构造器行没有 `qualifier` 列，故对构造器不施加约束。
- 解析是**两遍**：先 `exact`（引用类别必须一致），再 accommodating（`check_param_qualifier` 决定）；同一遍两个命中 = 二义（平面表没有"更好转换"的排序规则）。

## 4. 地板

```cpp
constexpr void invoke(std::size_t slot, void* obj, void** params) const;  // method_infos[slot].caller(obj, params)
```

- 方法表的**唯一调用原语**，除此之外什么都不做：不查名字、不比签名、不验返回类型、不记日志、不分配。
- `slot` 必须是 `method_infos` 的合法槽位——越界是**未定义行为**，由调用者保证。
- `obj` 是该行所需的上下文，按行种类解释：`PLAIN_METHOD` / `SIGNAL` 是对象地址（`object_t`，或 gadget 值的地址）；`STATIC_METHOD` 不使用它（thunk 不读）。`nullptr` 只在"这一行不需要对象"时合法；需要对象的行由上层（`meta_method_t::invoke_caller`）在到达本原语之前就以静默 no-op 拒绝。
- 构造器**不走**本原语：构造有独立表，construction thunk 由 `meta_constructor_t::construct` / `meta_object_t::new_instance()` 以"空对象 + `params[0]` 接收新实例地址"的约定直接执行。

## 5. 调用面与返回约定

| 层 | 入口 | 语义 |
|---|---|---|
| 表 | `meta_info_t::invoke(slot, obj, params)` | 地板，无校验 |
| 句柄 | `meta_method_t::invoke(...)` / `invoke_static(...)` | 单行调用；静态行用 `invoke_static`，用错是误用（一条 Critical，不执行） |
| 句柄 | `meta_constructor_t::construct(...)` / `meta_object_t::new_instance(...)` | 构造一个实例 |
| 对象层 | `meta_object_t::invoke(signature, ...)` | 按**签名**，沿链 own-first |
| 对象层 | `meta_object_t::call(name, ...)` / `call<"name">(obj, ...)` | 按**名**，沿链，解析一次后委托句柄的 `invoke` |
| 对象层 | `invoke_static` / `call_static` | 无接收者的行（静态成员） |

`any_t` 的返回约定（四种形状）：

| 形状 | 含义 |
|---|---|
| 已构造（`is_initialized()`） | 同步执行完成，值在 |
| 只声明 void（`is_valid() && !is_initialized()`） | void 行成功 |
| 无效（`!is_valid()`） | 失败：无效句柄、无此行、实参不符、返回类型不符、连接被拒绝 |
| 只声明（非 void） | 非阻塞 `Queued` 已投递，值稍后由目标线程写入 |

`Ret&` 形态：同步 / `BlockingQueued` 返回 `true` 且变量已写入；非阻塞 `Queued` 返回 `true` 但变量**原值不动**（值稍后到达）；失败或被取消返回 `false` 且变量不动。`Queued` 的锋利边是**生命周期**：事件持有的是地址，非 Ret 形态返回的 `any_t` 在事件执行前不得 move / copy / 销毁，`Ret&` 变量必须活到事件执行；事件被取消时槽不动（只声明 / 原值）。

## 6. 边界决策

- **不反射静态数据成员**；静态成员函数进方法表并标 `STATIC_METHOD`（键无 cvref 后缀）。
- **deducing-this 不支持**。未标注 `[[= make_hidden()]]` 的显式对象参数成员 → `build_meta<T>()` 第一步 `detail::reject_explicit_object_members<T>()` 抛 `std::meta::exception`，消息点名该成员并说明理由（显式对象参数不等价于隐式对象参数：`f(this X const x)` 不是 `f() const`）。带 `[[= make_hidden()]]` → 该成员被静默排除（`is_aimed_function` 不收集它），既不进方法表也不进属性扫描，且无诊断。
- **签名键**：带成员 cvref（`f() const` / `f() &`）；参数的**顶层 cv 在正规化时剥离**（`f(const int)` → `f(int)`，`f(int* const)` → `f(int*)`）；**引用与被指对象的 cv 保留**（`f(const int&)`、`f(const int*)` 不变）。两套世界同一条规则：编译期由反射的形参类型天然满足，运行期由 `detail::strip_parameter_top_level_cv()` 在 `original_parameter_alias()` 之前执行。
- **值类别**：`meta_type_t` 默认构造 = 无效类型（`std::type_index` 语义），`void` 只能经 `from_type<void>()` 得到；`any_t` 三态（invalid / declared-only / defined），无效检查先于未初始化检查。
