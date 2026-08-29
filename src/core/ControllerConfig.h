// 【C++ 语法】#pragma once：头文件保护宏，确保本头文件在单个编译单元内只被处理一次（作用等同 include guard）
#pragma once

// 【C++ 语法】#include "..."：以引号形式包含项目内自定义头文件（MappingTypes.h 中定义了 ControllerProfile 等映射数据模型）
#include "MappingTypes.h"

// 【C++ 语法】#include <...>：以尖括号形式包含系统路径下的 Qt / 标准库头文件（编译器按系统包含路径查找）
#include <QByteArray>  // 【Qt】QByteArray：Qt 的字节数组容器，用于承载 JSON 序列化后的文本字节数据
#include <stdexcept>   // 【C++ 语法】标准库 <stdexcept> 提供 std::runtime_error 异常类（JSON 解析失败时抛出）

// =====================================================================
// ControllerConfig —— 配置序列化（等效安卓版 ControllerConfig）
//
// JSON 格式与安卓版（version=2）同版本号，同一份配置文件可在
// Windows 版与安卓版之间互换使用。
//
// 顶层结构（新格式，含操作集）：
//   {
//     "version": 2,
//     "globalSettings": { ... },
//     "activeOperationSet": "Set1",
//     "operationSets": [
//       {
//         "id": "Set1",
//         "name": "默认操作集",
//         "commonLayer": { ... },
//         "layers": [ ... ]
//       }
//     ]
//   }
// 兼容：旧 v2 配置（顶层直接 commonLayer/layers）加载时自动包装成
// 单个「默认操作集」，实现无缝升级。
// 动作 type 取值：keyboard / mouse / mouseToggle / switchLayer /
//                 mouseMove / lookAround
// 鼠标按钮名：大写（LEFT/RIGHT/MIDDLE/FORWARD/BACK）
// =====================================================================
namespace ControllerConfig {  // 【C++ 语法】namespace 命名空间：将配置转换函数收纳到 ControllerConfig 之下，避免全局符号冲突
    // 当前配置版本号（不匹配时 fromJson 抛异常）
    // 【C++ 语法】constexpr：编译期常量，CONFIG_VERSION 在编译阶段即确定为 2，供 fromJson 做版本校验
    constexpr int CONFIG_VERSION = 2;  // 配置版本号（与安卓版 version=2 保持一致）

    // 序列化：ControllerProfile -> JSON 文本（indent 为格式化缩进）
    // 【C++ 语法】const ControllerProfile&：常量引用传参，只读访问、不拷贝、不修改原对象
    // 【C++ 语法】int indent = 2：默认参数——调用时不传 indent 则取默认值 2（输出带缩进的格式化 JSON）
    QByteArray toJson(const ControllerProfile& profile, int indent = 2);  // 把配置对象序列化为 JSON 字节串

    // 反序列化：JSON 文本 -> ControllerProfile
    // 解析失败（版本不匹配 / 格式错误 / 字段无效）时抛出 std::runtime_error
    // 【C++ 语法】const QByteArray& 常量引用传参；返回 ControllerProfile 对象（值语义，靠返回值携带结果）
    ControllerProfile fromJson(const QByteArray& json);  // 把 JSON 字节串解析回配置对象，失败时抛 std::runtime_error
}  // namespace ControllerConfig 结束
