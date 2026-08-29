// ============================================================
// ConfigManager.cpp
// 配置文件读写管理
// ------------------------------------------------------------
// 职责：管理配置文件在磁盘上的位置与读写。
//   - 文件固定放在可执行文件同目录（本机使用，不写注册表/用户目录）
//   - 文件名：steamlike_config.json
//   - 首次启动（无配置文件）或解析失败时，自动生成默认配置
//
// 注意：本文件只负责"文件层面"的存取，JSON 的编解码逻辑
// 全部委托给 ControllerConfig 命名空间（见 ControllerConfig.cpp）。
// ============================================================

// 【C++ 语法】#include "..."：双引号形式表示先从"当前源文件所在目录"查找并包含头文件，
// 通常用于本项目内部头文件；此处引入 ConfigManager 的类声明（本文件实现该类的各成员函数）
#include "ConfigManager.h"

// 【C++ 语法】#include "..."：包含 ControllerConfig.h，其中声明了 JSON 编解码函数
//（fromJson / toJson）以及 ControllerProfile 的默认配置工厂函数 createDefault()
#include "ControllerConfig.h"

// 【Qt】#include <QCoreApplication>：Qt 核心应用类，用于获取可执行文件所在目录（applicationDirPath）
#include <QCoreApplication>
// 【Qt】#include <QDir>：Qt 目录工具类，用于目录/路径操作，此处用到路径拼接方法 filePath()
#include <QDir>
// 【Qt】#include <QFile>：Qt 文件类，用于磁盘文件的打开、读取、写入与存在性判断（open/readAll/write/exists）
#include <QFile>

// 【C++ 语法】#include <stdexcept>：包含标准异常头文件，用于捕获 std::exception 及其派生异常
#include <stdexcept>

// 【C++ 语法】匿名命名空间 namespace { ... }：其中定义的名字只在当前编译单元（本 .cpp 文件）内可见，
// 不会对外链接暴露，效果类似"C++ 文件内 static"，用于放置仅供本文件使用的内部常量/辅助函数
namespace {
// 【C++ 语法】const char kConfigFileName[]：定义只读字符数组（C 风格字符串），
// 数组长度由编译器根据初始化字符串自动推导；const 保证内容不可被修改
// 【C++ 语法】文件级 const 变量具有内部链接（相当于 static），不会与其它文件的重名符号冲突
// 配置文件固定文件名（与安卓版保持一致）
const char kConfigFileName[] = "steamlike_config.json";
// 【C++ 语法】} 结束匿名命名空间（大括号后无分号，因为 namespace 是作用域而非声明语句）
}

// ============================================================
// configFilePath：返回配置文件绝对路径
// ============================================================
// 取可执行文件所在目录 + 固定文件名。
// 选择 exe 目录而非用户文档目录，是为了"绿色便携"——
// 拷贝整个程序目录即可迁移配置。
// 【C++ 语法】成员函数定义：QString ConfigManager::configFilePath() 中，作用域运算符 "::"
// 把函数归属于 ConfigManager 类；返回类型为 QString（Qt 字符串，UTF-16 编码、隐式共享）
QString ConfigManager::configFilePath() {
    // 【C++ 语法】return 语句：把右侧表达式的计算结果作为函数返回值返回
    // 【Qt】QCoreApplication::applicationDirPath()：Qt 核心静态函数，返回当前可执行文件所在的目录路径
    // 【C++ 语法】链式嵌套调用：先调用 applicationDirPath() 得到 exe 目录字符串，
    // 再用它构造 QDir 对象，随后调用 QDir::filePath() 拼接出完整的绝对路径
    // 【Qt】QDir::filePath()：安全地把相对文件名与目录拼接（自动处理末尾分隔符），返回完整路径
    // 【Qt】QLatin1String(kConfigFileName)：把 ASCII/拉丁字符数组包装为高效的 Latin-1 字符串常量，
    // 避免把字节串反复转换成 UTF-16 的额外开销
    return QDir(QCoreApplication::applicationDirPath()).filePath(QLatin1String(kConfigFileName));
// 【C++ 语法】} 结束 configFilePath() 函数体（大括号后无分号，函数定义不是声明语句）
}

// ============================================================
// hasConfigFile：判断配置文件是否已存在
// ============================================================
// 【C++ 语法】成员函数定义，返回类型为内置布尔类型 bool（取值为 true 或 false）
bool ConfigManager::hasConfigFile() {
    // 【C++ 语法】return 语句：把右侧表达式的结果作为函数返回值返回
    // 【C++ 语法】函数调用作为参数：先调用 configFilePath() 得到路径字符串，再传给 QFile::exists()
    // 【Qt】QFile::exists(路径)：QFile 的静态成员函数，无需创建文件对象即可直接判断指定路径的文件是否存在
    return QFile::exists(configFilePath());
// 【C++ 语法】} 结束 hasConfigFile() 函数体
}

// ============================================================
// load：加载配置，失败时回退默认配置
// ============================================================
// 流程：
//   1. 文件存在且可读 -> 读取全部字节并交给 ControllerConfig::fromJson；
//      - 解析成功：直接返回；
//      - 解析失败（语法错误/版本不符）：捕获异常后落入默认配置。
//   2. 文件不存在或读取失败：使用默认配置。
//   3. 无论哪种回退，都会把默认配置保存一份到磁盘，方便用户查看
//      并保证下次启动有据可依。
// 【C++ 语法】成员函数定义，按值返回 ControllerProfile 类型（自定义结构体，
// 返回时可能发生一次拷贝或移动；其内部使用 Qt 的 QVector 等容器存储数据）
ControllerProfile ConfigManager::load() {
    // 【C++ 语法】局部对象定义：在栈上构造一个 QFile 对象，构造参数为配置文件路径；
    // 该对象生命周期与函数作用域一致，函数结束时自动析构释放资源
    // 【Qt】QFile：封装底层文件句柄的 Qt 文件对象，后续通过它进行打开、读取等操作
    QFile file(configFilePath());
    // 【C++ 语法】if (条件) { ... }：条件判断语句——条件表达式为 true 时执行花括号内的代码块
    // 【C++ 语法】&&：逻辑与运算符，左右两侧条件同时成立整体才为 true；
    // 短路求值：左侧 exists() 为 false 时不再执行右侧的 open()
    // 【Qt】QFile::exists()：实例方法，判断该文件对象指向的文件是否存在
    // 【Qt】QFile::open(QIODevice::ReadOnly)：以"只读"模式打开文件，成功返回 true，失败返回 false
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        // 【C++ 语法】const 局部变量：声明后不可再被修改的变量
        // 【C++ 语法】类型推导/拷贝初始化：用 readAll() 的返回值初始化 data 变量
        // 【Qt】QByteArray：Qt 的字节数组容器，用于保存原始二进制/字节数据（此处为 JSON 文本的字节表示）
        // 【Qt】QFile::readAll()：一次性读取文件剩余全部内容并返回 QByteArray
        const QByteArray data = file.readAll();
        // 【Qt】QFile::close()：关闭文件，释放文件句柄；读取完毕后应及时关闭，避免占用资源
        file.close();
        // 【C++ 语法】try { ... }：异常捕获块的开始，块内抛出的异常可被后续 catch 捕获处理
        try {
            // 【C++ 语法】return：把函数调用结果作为 load() 函数的返回值直接返回
            // 【C++ 语法】作用域运算符调用：ControllerConfig::fromJson(data) 调用命名空间/类中的静态函数
            // 【C++ 语法】按值传递：data 是 QByteArray，采用隐式共享（写时复制），按值传入不会深拷贝数据
            return ControllerConfig::fromJson(data);
        // 【C++ 语法】catch (const std::exception&)：捕获 std::exception 及其所有派生异常；
        // 【C++ 语法】const 引用捕获：不复制异常对象、也不允许修改；此处省略了异常变量名（因为用不到）
        } catch (const std::exception&) {
            // 解析失败：回退到默认配置（损坏的配置不致命）
        // 【C++ 语法】} 结束 catch 块；此处是空块——仅"吞掉"异常、不做任何处理，随后落入默认配置逻辑
        }
    // 【C++ 语法】} 结束 if 代码块
    }

    // 【C++ 语法】const 局部变量：声明只读的 ControllerProfile 对象，由右侧表达式初始化
    // 【C++ 语法】静态工厂方法：ControllerProfile::createDefault() 创建一份默认配置，
    // 不依赖任何已加载的数据，用于"文件不存在或解析失败"时的兜底
    const ControllerProfile def = ControllerProfile::createDefault();
    // 【C++ 语法】函数调用语句：把默认配置通过 save()（本类静态成员）写入磁盘；
    // 【C++ 语法】实参绑定：def 是 const 对象，可无缝传给 save 的 const ControllerProfile& 形参，无需拷贝
    save(def);
    // 【C++ 语法】return：把默认配置作为函数返回值返回给调用方（发生一次拷贝/移动）
    return def;
// 【C++ 语法】} 结束 load() 函数体
}

// ============================================================
// save：把配置写入磁盘
// ============================================================
// 以"写覆盖（Truncate）"方式打开文件，写入序列化后的 JSON。
// 写入失败（如磁盘只读/目录无权限）返回 false，由调用方提示用户。
// 【C++ 语法】成员函数定义，返回 bool；形参为 const ControllerProfile&——
// "对常量对象的引用"，只读访问、不复制传入的配置对象，避免拷贝开销
bool ConfigManager::save(const ControllerProfile& profile) {
    // 【C++ 语法】局部对象定义：在栈上构造 QFile 对象，指向配置文件路径；函数结束自动析构
    QFile file(configFilePath());
    // 【C++ 语法】if (!条件)：逻辑非运算符 "!" 对 open() 结果取反——
    // 当 open() 失败（返回 false，取反后为 true）时，进入下一行代码块
    // 【Qt】QIODevice::WriteOnly：只写模式——打开后只能写入、不能读取
    // 【Qt】QIODevice::Truncate：截断模式——打开文件时把原有内容全部清空，实现"覆盖写入"
    // 【Qt】QIODevice::WriteOnly | QIODevice::Truncate：按位或 "|" 组合多个打开标志，可同时生效
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        // 【C++ 语法】return false：打开失败则立即返回 false（提前退出），不再执行后面的代码
        return false;
    // 【C++ 语法】const bool 定义：声明只读布尔变量，记录"写入是否成功"的判定结果
    // 【C++ 语法】比较运算符 !=：判断右侧表达式的结果是否不等于 -1（相等时为 false）
    // 【Qt】QFile::write(QByteArray)：向文件写入数据，成功返回实际写入的字节数，失败返回 -1
    // 【C++ 语法】函数调用作为操作数：ControllerConfig::toJson(profile) 把配置结构体序列化为 JSON 字节数组
    const bool ok = file.write(ControllerConfig::toJson(profile)) != -1;
    // 【Qt】QFile::close()：关闭文件，确保缓冲区数据真正刷新落盘
    file.close();
    // 【C++ 语法】return ok：把"写入是否成功"的布尔结果返回给调用方
    return ok;
// 【C++ 语法】} 结束 save() 函数体
}

// ============================================================
// resetToDefault：重置为默认配置并落盘
// ============================================================
// 【C++ 语法】成员函数定义，返回类型 void（无返回值）；无参数
void ConfigManager::resetToDefault() {
    // 【C++ 语法】函数调用语句：调用本类静态成员 save()，实参为 createDefault() 生成的默认配置
    // 【C++ 语法】临时对象：createDefault() 返回的 ControllerProfile 临时对象直接绑定到
    // save 的 const 引用形参上，临时对象在完整表达式结束后自动销毁
    save(ControllerProfile::createDefault());
// 【C++ 语法】} 结束 resetToDefault() 函数体
}
