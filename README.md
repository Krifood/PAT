# PAT 参数分析工具

PAT 是基于 C++20、Qt 6 的桌面应用，支持协议模板、离线数据解析和串口实时采集。

## Windows 构建与发布

安装 CMake 3.20+、C++20 编译器和 Qt 6（需要 Core、Widgets、Charts、Concurrent、SerialPort 模块；开启测试时另需 Test 模块）。支持 Qt 官方的 MSVC/MinGW 套件，也支持 MSYS2 UCRT64。项目根目录执行：

```powershell
.\scripts\build_and_run.ps1
```

脚本默认以 Release 配置编译并输出便携版，不编译或运行测试：

- 目录：`build/windows/package/PAT-Release/`
- 压缩包：`build/windows/package/PAT-Release-win64.zip`
- 校验文件：`build/windows/package/PAT-Release-win64.zip.sha256`

发布目录已包含 Qt DLL、Windows 平台插件和编译器运行库，目标电脑无需安装 Qt；解压后运行 `bin/pat_app.exe`。Qt 未被自动找到时传入路径，例如：

```powershell
.\scripts\build_and_run.ps1 -QtRoot C:\Qt\6.8.3\msvc2022_64
```

MSVC 路径默认使用 Visual Studio 2022 x64（安装“使用 C++ 的桌面开发”及 Windows SDK），ARM64 Qt 自动使用 ARM64；其他 VS 版本用 `-MsvcGenerator` 指定。MinGW Qt 必须配套对应编译器，不能与 MSVC 库混用。切换工具链时使用不同的 `-BuildDir`，避免沿用旧 CMake 缓存。MSVC 已启用 UTF-8 源码和标准 C++20 宏。

## VS Code 源码诊断

请以 **PAT 项目目录** 打开文件夹，而非只打开单个源文件。仓库提供 `.vscode/settings.json` 与扩展推荐，使用 CMake Tools 将真实 Qt include、功能宏和 C++20 配置提供给 C/C++ 扩展，不关闭错误提示来掩盖问题。

1. 安装推荐的 C/C++ 和 CMake Tools 扩展；执行 `CMake: Select a Kit`，选择与 Qt 匹配的编译器。
2. Qt 不在默认搜索路径时，在启动 VS Code 前设置 `QT_ROOT` 指向 Qt 套件根目录（包含 `lib/cmake/Qt6`）。重新打开 VS Code 后执行 `CMake: Configure`，再执行 `CMake: Build`，生成 Qt 的自动生成头文件。
3. 配置成功后若仍残留诊断，执行 `C/C++: Reset IntelliSense Database` 和 `Developer: Reload Window`。若配置失败，应先解决 CMake 输出中的 Qt/编译器错误，不要直接添加全盘 include 路径。

编辑器使用独立目录 `build/vscode`，不与脚本的 `build/local`、`build/windows` 混用。Makefiles/Ninja 会生成 `compile_commands.json`；Visual Studio 生成器不生成该文件，仍可由 CMake Tools 配置提供器获取正确参数。不要同时启用两套 C++ 诊断引擎。本机已安装上述扩展，但尚不能在终端中替你确认 VS Code 窗口里的所有红线都已消失。

常用操作：`-Action Build` 只编译，`-Action Test` 编译并测试，`-Action Run` 编译后启动，`-Action Package` 生成发布件；Build/Run/Package 可加 `-WithTests` 在后续操作前运行测试，测试失败时停止。跳过测试不代表已通过验收，正式交付前应执行显式验证。加 `-Clean` 可重新生成构建目录。如 Qt SDK 未安装 SerialPort，可用 `-DisableSerial` 构建不含实时串口采集的版本，该降级必须显式指定。

## macOS 自行构建

安装 CMake、C++ 编译器，以及 Qt 6 的 Core、Widgets、Charts、SerialPort 模块。在本目录运行：

```sh
./scripts/build_and_run.sh
```

脚本默认仅编译应用，再启动本次生成的 `build/local/pat_app.app`，不编译或运行测试，也不要求安装 Qt Test。只编译时运行 `RUN=0 ./scripts/build_and_run.sh`；编译并验证时运行 `TEST=1 RUN=0 ./scripts/build_and_run.sh`。显式启用测试后，失败仍会停止后续启动或打包，不会忽略失败结果。需要附带 Qt 运行库的应用包时运行 `PACKAGE=1 RUN=0 ./scripts/build_and_run.sh`，输出为 `build/local/stage/pat_app.app`；交付验证可同时加 `TEST=1`。不要运行历史构建遗留的 `build/pat_app` 裸程序。使用 Qt 官方安装包时，可设置 `QT_ROOT=/path/to/Qt/6.x/macos`。脚本可从任意当前目录通过其完整路径调用，构建目录仍位于 PAT 工程内。

打开应用后，选择“文件 → 打开格式”，可加载 `samples/cp01_b3_navigation.json`。串口连接及模板使用详见 [串口功能使用说明](docs/串口功能使用说明.md)；适配其他协议时参照 [协议模板 JSON 编辑指南](docs/协议模板JSON编辑指南.md)。

VMC 自描述混合日志可通过“文件 → 导入 VMC 混合日志...”选择一个或多个源文件。PAT 会先在后台完成只读扫描，显示流、字段、有效记录、元数据、尾部状态和预计输出大小；确认时间戳采用纳秒/微秒/毫秒/秒或原始 tick 后，再生成同名 `.patbin` 和 schema 3 `.json`。BIN 按消息流紧凑分块并带文件尾索引，JSON 保存流、字段、时间比例和来源段。转换完成后会直接打开结果；以后只需打开 JSON，应用会自动查找其 `storage.data_file`。信号按勾选项及空间图当前坐标在后台按需解码；时序缩放/平移自动重读当前窗口，图表保留原交互状态。默认缓存预算为 128 MiB，可在“视图 → 大文件缓存预算”切换 64/256 MiB。数据量增加不会触发“每条记录 × 全部字段”的内存展开。HA/VMC 日志还支持文件末尾的记录形全 FF 终止标记，中途出现相同异常仍会严格报错。

普通无帧头固定长度 BIN/DAT 预计点载荷超过 64 MiB 时自动使用同一后台索引读取方式，无需改 JSON 或转换源文件。索引数据的信号右键菜单提供“当前读取窗口统计...”，统计基于抽稀前的原始解析值。适用范围及 20 倍样本验证结果见 [大文件分析重构与验证](docs/大文件分析重构与验证.md)。

“帮助 → 版本信息”可查看当前构建版本、发布状态、Qt 运行版本及操作系统；“帮助 → 更新日志”可离线查看随应用打包的 [更新日志](CHANGELOG.md)。当前为 `0.1.2` 开发检查点（2026-10-24），非正式发行版；节点管理、验证和已知限制见 [0.1.2 检查点](docs/状态与验证-0.1.2.md)。此前详细验证过程保留在 [0.1.1 状态记录](docs/状态与验证-0.1.1.md)。

对于已知消息在时间戳末尾中断、长度为 `FF` 且后续全部为 `FF` 的末尾残帧，导入会保留此前完整记录，在确认页明确提示忽略条数、字节数和偏移；输出 JSON 的 `source_segments` 保存 `discarded_tail_records`、`discarded_tail_bytes`（含填充）和 `discarded_tail_offset`（原文件字节偏移）。不会补造记录或修改原文件。中途非 FF 数据、未知消息残帧及普通截断仍报错，真实解析失败不再误报为“扫描已取消”。

加载模板后，右侧“二维/三维轨迹”页默认显示一组二维子图，可继续新增二维或三维子图，也可用各子图标题栏右侧的“× 删除子图”删到零组。每条轨迹可先按协议分组筛选，父分组会包含所有子分组；分组及 X/Y/Z 下拉框均可直接输入关键字做包含搜索。二维绑定 X/Y，三维绑定 X/Y/Z；CP01-B3 默认选择组合经纬度和高度。拖动一个子图的标题到另一个同维度子图可合并多条轨迹。实时源缓存滚动、暂停显示、切换页签和调整视图不会清除空间轨迹的本次会话历史；每条轨迹最多绘制 4,000 个抽稀点。当前视图是数值空间，不含地图底图，地图包预留见 [设计方案第 11 章](docs/DESIGN.md)。

离线数据在绘图区上方提供共用的“显示区间”：拖动左右端点选择时间段，拖动中间色带平移，也可直接输入起止值、用 ◀/▶ 移动一个区间或点击“全范围”恢复。单位沿用时序横轴；没有时间戳的串口帧使用采样序号。时序图缩放与区间栏双向同步，切换到二维/三维页仍沿用该区间，按 X 信号的时间筛选配对轨迹。大日志在松开滑块后按区间后台读取；筛选不会修改原文件，实时采集时此控件禁用。

时序图光标数据用 `k:` / `v:` 紧凑显示。鼠标悬停辅助线时，交点信息持续显示，不再自动超时；移开鼠标、拖动、删除辅助线或离开图表时收起。

左侧“信号检索与快捷操作”可按名称、说明、分组或单位查找信号，并直接勾选、拖拽或打开右键操作。信号属性可设置 LSB 比例与整体纵向平移，时序和空间图同步应用并可复原；选择两个同单位信号可创建 `A-B` 差值或 `A+B` 和值关系子图。派生图标题声明数据来源，且不添加虚构的信号树条目。这些操作兼容离线与串口动态数据，不修改原始解析值和日志。详细规则见 [设计方案第 12、14 章](docs/DESIGN.md)。

“视图 → 显示主题”可在室内深色、室内亮色和户外高对比之间切换。主题同步覆盖控件、时序曲线、二维/三维轨迹、坐标轴和辅助线，并在下次启动时恢复上次选择；户外高对比主题使用白色背景、深色文字和更粗的数据线。
