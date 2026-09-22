# PAT 协议模板 JSON 编辑指南

本文按当前 PAT 的解析实现编写，供把**固定帧头、固定总帧长的串口协议**映射成可加载的 JSON。已有格式示例见 [温度传感器模板](../samples/serial_sensor.json)和 [CP01-B3 导航模板](../samples/cp01_b3_navigation.json)。先取得协议原文和至少一帧人工可核对的原始数据，再填写模板；不要从曲线外观反推字节定义。

## 1. 先确定适配范围

串口实时采集需要 `schema_version: 2`、`template_id` 和 `framing`。首版只按连续十六进制帧头与固定总长度识别一类帧；可以配置低 8 位累加和 `sum8`，尚不能用 JSON 表达 CRC、转义、可变长度、字段级端序、字符串、BCD 或枚举文字映射。一个界面会话选择一个模板。若协议包含不同长度或不同帧头的多种报文，目前需分别建模板并分别采集或解析。

旧版无 `framing` 的 JSON 用于**离线固定记录**：从文件首字节起每 `record_size` 字节解一个记录，不扫描帧头；它不能直接用于串口实时采集。串口端口、波特率、数据位、校验位、停止位、流控和日志目录在采集面板设置，不写进协议 JSON。

## 2. 一份可直接试用的标准模板

下面假设 6 字节帧：`AA 55 90 01 05 96`。前两字节是帧头，第 3–4 字节是小端有符号温度原始值 `0x0190 = 400`，第 5 字节是状态位，第 6 字节是第 3–5 字节累加和的低 8 位 `0x96`。

```json
{
  "schema_version": 2,
  "template_id": "example_sensor_v1",
  "record_size": 6,
  "endianness": "little",
  "framing": {
    "kind": "fixed_header",
    "header_hex": "AA55",
    "frame_length": 6,
    "checksum": {
      "kind": "sum8",
      "start_byte": 2,
      "end_byte": 4,
      "byte_offset": 5
    }
  },
  "signals": [
    {
      "name": "温度",
      "byte_offset": 2,
      "value_type": "int16",
      "scale": 0.1,
      "bias": -40,
      "unit": "°C",
      "description": "探头温度",
      "group": "环境/温度"
    },
    {
      "name": "状态原值",
      "byte_offset": 4,
      "value_type": "uint8",
      "group": "状态"
    },
    {
      "name": "就绪位",
      "byte_offset": 4,
      "value_type": "uint8",
      "bit_offset": 0,
      "bit_width": 1,
      "description": "状态字节最低位；1 表示就绪",
      "group": "状态"
    }
  ],
  "groups": [
    { "path": "环境/温度", "description": "环境测量值" },
    { "path": "状态", "description": "设备状态" }
  ]
}
```

这帧应解析出温度 `0 °C`、状态原值 `5`、就绪位 `1`。`checksum` 可在协议没有校验时整个删除；不要保留一个空对象。

## 3. 顶层和分帧字段

| 字段 | 如何填写 | 当前规则 |
| --- | --- | --- |
| `schema_version` | 新串口模板填数字 `2` | 固定记录/串口支持 1、2；索引混合日志使用 3，见第 6 节 |
| `template_id` | 稳定且能区分版本的非空字符串，如 `sensor_v1` | 有 `framing` 时必填；修改字段含义后建议改版本后缀 |
| `record_size` | 完整帧的**总字节数**，包含帧头和校验字节 | 正整数；所有信号必须落在 `[0, record_size)` 内 |
| `endianness` | 多字节字段共同的 `little` 或 `big` | 默认 `little`；单字节不受影响；当前没有单字段覆盖 |
| `framing.kind` | 固定填 `fixed_header` | 当前唯一支持的分帧方式 |
| `framing.header_hex` | 连续十六进制字节，例如 `AA55` | 不写空格、`0x` 或逗号；必须非空且字符数为偶数 |
| `framing.frame_length` | 与 `record_size` 相同的总字节数 | 不能小于帧头长度 |
| `framing.checksum` | 协议确有累加和时填写下表字段 | 可省略；目前仅支持 `sum8` |
| `signals` | 至少一个信号对象 | v2 信号名不能重复；数组顺序也是信号树和成功解析 CSV 的列顺序 |
| `groups` | 可选的 `{ "path", "description" }` 数组 | 只给信号树的分组补说明；信号通过自身 `group` 指定路径 |
| `time_unit` | 旧离线记录的横轴单位，常用 `s`、`ms`、`us`、`ns` | 串口有帧头的会话横轴是成功帧采样序号，忽略时间轴换算 |

`sum8` 的 `start_byte`、`end_byte` 是**从 0 开始且两端包含**的求和范围，`byte_offset` 是存放结果的字节位置。校验值为该范围各字节相加后取低 8 位；校验字节不能落在求和范围内。校验不通过的候选帧不会进入成功解析数据，原始 `.bin` 日志仍保留收到的字节。其他 CRC 算法不能改名为 `sum8` 凑用。

当前加载器会忽略它不认识的 JSON 键；拼错 `scale`、`group` 等可选字段时，格式验证可能仍通过并使用默认值。保存前应按本表逐项核对字段名，再用已知帧检查数值。

## 4. 信号字段怎么填写

| 字段 | 填写方法 | 默认或限制 |
| --- | --- | --- |
| `name` | 对用户可辨认的字段名 | 非空；v2 中唯一 |
| `byte_offset` | 字段第一个字节距离帧首的偏移，**从 0 计数** | 必填；`byte_offset + 类型字节数 ≤ record_size` |
| `value_type` | 见下方类型表 | 必填；大小写会转成小写 |
| `scale` | 原始值乘以的分辨率或倍率 | 默认 `1` |
| `bias` | 倍率之后加的物理偏移 | 默认 `0`；与 `byte_offset` 完全不同 |
| `unit` | 工程单位，如 `m/s²`、`°` | 可省略；只影响展示，不参与计算 |
| `description` | 协议定义、取值范围、未确认说明 | 可省略；不会自动变成枚举解释 |
| `group` | 信号树层级路径，如 `导航/姿态` | 可省略；`/` 分隔层级 |
| `bit_offset`、`bit_width` | 从解码后**无符号整数的最低有效位**起，选取连续位段 | 必须同时出现；仅适用于 `uint8/16/32`，位段不得越界 |
| `time_scale`、信号级 `time_unit` | 旧无帧头离线记录每个采样的横轴步长和单位 | 有 `framing` 时不作为绘图横轴；建议串口模板不填 |

| `value_type` | 字节数 | 用途 |
| --- | ---: | --- |
| `int8` / `uint8` | 1 | 有符号 / 无符号单字节整数 |
| `int16` / `uint16` | 2 | 有符号 / 无符号双字节整数 |
| `int32` / `uint32` | 4 | 有符号 / 无符号四字节整数 |
| `int64` / `uint64` | 8 | 64 位整数；常用于设备计数或时间戳，超过双精度整数精确范围时曲线显示会损失最低位 |
| `float32` | 4 | IEEE 754 单精度浮点数 |
| `float64` | 8 | IEEE 754 双精度浮点数 |

`int*` 按二进制补码解释。解码顺序是**取原始数 → 如配置位段则截取位段 → 计算 `工程值 = 原始数 × scale + bias`**。同一字节可以定义原值和多个位段信号，互不排斥。当前不支持有符号位段的符号扩展。下面三个对象可按协议偏移放入 `signals`，分别示范定点电压、浮点角度和高半字节状态码；它们只是字段片段，所在模板的 `record_size` 必须足够大：

```json
[
  { "name": "电压", "byte_offset": 6, "value_type": "uint16", "scale": 0.001, "unit": "V" },
  { "name": "角度", "byte_offset": 8, "value_type": "float32", "unit": "°" },
  { "name": "状态码", "byte_offset": 12, "value_type": "uint8", "bit_offset": 4, "bit_width": 4 }
]
```

字节偏移示例：协议文档写“第 25–26 字节”时，JSON `byte_offset` 为 `24`；若类型为 `int16`，将读取偏移 24、25 两字节。小端 `90 01` 是 `0x0190`，大端则是 `0x9001`；端序要按协议写，不能按计算机系统猜测。`scale: 0.01` 表示原始数 1234 显示 12.34；若原始数是 `-1234`，显示 `-12.34`。枚举值如 `0=关闭、1=开启` 可先写入 `description`，当前曲线和 CSV 仍输出数值。

## 5. 从其他协议制作模板的步骤

1. 列出帧头、固定总长、校验算法和覆盖范围；确定帧头是否足以区分消息类型。若长度可变、采用转义或校验不是 `sum8`，先记录为待实现能力，不要构造会产生错误值的模板。
2. 建一张字段对照表：协议中的字节序号、JSON `byte_offset`、占用字节、原始有无符号、端序、分辨率、偏移、单位及一帧期望值。对状态字再写位编号表。
3. 复制上面的 JSON 或使用“文件 → 新建串口模板”，替换标识、长度、帧头、字段和校验；JSON 不能包含注释或尾随逗号，十六进制帧头必须写成字符串。
4. 在 PAT 中用“编辑格式 → 验证”检查结构，再用“预览帧解码”输入一整帧十六进制数据，逐项核对人工期望值。验证通过只说明模板结构合法，不保证字节映射正确。
5. 先把同一帧保存为 `.hex` 或 `.bin` 做离线解析，再连接设备采集。`.hex/.txt/.log/.csv` 按十六进制文本读取；`.bin/.dat` 按原始字节读取。本程序生成的**成功解析 CSV 是工程值表，不是原始帧回放文件**，回放请用原始 `.bin`。采集时检查最近帧、成功解析 `.csv` 和原始 `.bin`；有原始字节但无成功帧时，依次核对帧头、长度、校验、串口参数和接线。

已有 86 字节、多字段、位段和校验的实际填写例子见 [CP01-B3 模板说明](CP01-B3导航模板说明.md)。该文件中的合成帧可用来练习核对，但不是设备抓包。

## 6. schema 3 索引混合日志 JSON

“文件 → 导入 VMC 混合日志...”会自动生成 schema 3 JSON。它与 `.patbin` 成对使用，不是串口分帧模板。外层至少包含：

```json
{
  "schema_version": 3,
  "template_id": "vmc_unified_xxx",
  "storage": {
    "kind": "indexed_stream_chunks",
    "version": 1,
    "layout_sha256": "64位十六进制结构指纹",
    "data_file": "同目录数据文件.patbin"
  },
  "timestamp": {
    "value_type": "uint64",
    "unit": "tick",
    "scale": null
  },
  "streams": [
    {
      "stream_id": 123,
      "source_message_id": "0x00E7",
      "name": "ah_nav",
      "payload_length": 139,
      "endianness": "big",
      "signals": [
        {
          "name": "cur_pos_x",
          "source_name": "cur_pos_x",
          "byte_offset": 0,
          "value_type": "float32",
          "source_type": "r32",
          "scale": 1.0,
          "bias": 0.0,
          "unit": "",
          "description": "",
          "group": "ah_nav [0x00E7]"
        }
      ]
    }
  ],
  "source_segments": []
}
```

可以人工补充或修正 `signals[].scale`、`bias`、`unit`、`description` 和 `group`；确认设备时间基准后，可把 `timestamp.unit` 改为真实单位，并把 `timestamp.scale` 从 `null` 改为换算到该单位的正数。`storage.data_file` 可在成对文件改名后更新为新的相对文件名。

不要人工修改 `storage.kind/version/layout_sha256`、`stream_id`、`payload_length`、`endianness`、`byte_offset`、`value_type` 或信号顺序，除非同时重新生成 BIN；这些字段参与结构校验或原始负载解码，单独修改会被拒绝或得到错误数值。`source_name`、`source_type`、`source_message_id`、`source_control`、`source_format` 和 `source_segments` 用于追溯源日志，建议保持原样。若其他设备也采用自描述、多消息 ID、不同负载长度的日志，应新增相应源读取器并复用统一写入器，不应把所有消息补齐成一条超宽固定记录。
