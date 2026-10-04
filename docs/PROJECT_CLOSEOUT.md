# 项目开发结项与Git归档

2026-10-04按用户最新决定，在Application更新和Boot/Application检查后结束代码开发。本文是最终维护入口和由用户执行的完整归档步骤；开发结项不等于完整升级已经实机验收。

## 目录

- [结项结论](#结项结论)
- [最终检查与修正](#最终检查与修正)
- [验证记录与保留边界](#验证记录与保留边界)
- [维护文档入口](#维护文档入口)
- [Git完整归档步骤](#git完整归档步骤)
- [归档后的历史与恢复](#归档后的历史与恢复)

## 结项结论

- Application与独立裸机Bootloader的长期代码已接通，最终静态审查未再发现阻塞项，修正后的Application Debug构建通过。
- 不再开发WPF、增加业务或安排故障矩阵；现有保护代码和未完成实机验收记录保留。
- [项目简历](项目简历.md)按“实现成果与已验证成果”定稿，不宣称完整升级、掉电恢复或工业级安全OTA已实测通过。
- 用户已选择完整归档应用、Boot和项目文档，建立新的项目根仓库和新远端，保留原Application仓库历史。用户已完成旧历史改名保留、根仓库`main`初始化及新远端关联；首次暂存、提交和推送仍待完成。

## 最终检查与修正

本轮先只读核对模块职责、状态和资源所有权，再按用户新的明确授权只修改以下两个源码文件：

1. `firmware/User/app/startup/app_startup.c`：删除MQTT Task和业务Task创建之后遗留的两个独立失败返回块。失败返回必须位于各自的条件分支内；创建成功后继续创建FTP/业务Task，最后返回OK，交给`main()`启动调度器。此问题来自前次代码接入遗漏，先前“能构建”不能证明启动正确。
2. `firmware/User/app/ftp/app_ftp_task.c`：成功完成态不再按传输阶段计算并封顶95%，避免共享状态Queue改为Peek后重复快照把内部100%改回95%。

Boot源码和其构建配置本轮未改；没有新增Task、状态机、库、构建模式或跨工程公共头。Application以Storage为外部Flash和A/B元数据的唯一写入者；FTP与业务Task只Peek同一个最新快照，MQTT/FTP仍分别独占Socket 0与1/2。

交接审查已核对：512字节描述头、完整文件MD5、A/B元数据v3偏移/CRC32/CMIT、擦除前持久登记、仅应用区擦写、最多3次安装/未确认启动、MSP/VTOR/中断交接，以及本地首次确认。升级受理ACK在元数据完整提交后发送，ACK和升级中消息分别匹配原会话的末段SENDOK后才放行复位；SENDOK不是Broker或安装成功确认。

## 验证记录与保留边界

| 项目 | 当前证据 | 结项口径 |
| --- | --- | --- |
| CAN、TCP/MQTT业务、FTP正常下载 | 历史用户实机反馈及成功结果 | 已验证对应范围 |
| Boot源码、独立依赖、镜像布局 | 本轮静态审查及现有BIN；历史Debug/Release构建与双镜像烧录读回日志 | 不替代普通跳转和安装实机结果 |
| 修正后的Application | 本轮Debug构建、BIN头/长度/向量检查、Diff检查 | 可归档代码基线 |
| 正常升级、首次CONFIRMED、成功上报、再次复位 | 尚未提供最终运行反馈 | 未验收，随开发结项保留 |
| 故障注入、掉电恢复等失败矩阵 | 用户决定跳过 | 未执行，不宣称通过 |

Application最终Debug：ARM GCC 14.3.1；FLASH 87476 B / 448 KiB，RAM链接占用47112 B / 64 KiB；text 87292 B、data 184 B、bss 46928 B。生成`application.elf/.bin/.hex`，BIN与描述头中的完整长度均为87476 B，硬件`FIRE_F103_V2`、软件`APP.V1.0.0`、保留字节全0；描述头`0x08010000`，向量`0x08010200`，MSP `0x20010000`、Reset `0x08010D65`。Boot BIN 9088 B，MSP `0x20010000`、Reset `0x080001E5`。

沙箱内GCC启动报Windows DLL重定位错误，经授权在沙箱外使用本机已有GNU Make、原编译器及同一build完成增量构建；未修改CMake。两个重新编译的源码无新增警告，链接仍有既有newlib/nosys占位警告，不记为全工程无警告。旧64364 B Application产物受启动失败返回造成的链接裁剪影响，不能当作最终基线。

当前`heap_4`为32 KiB，Task栈与Queue等静态创建预算约30 KiB，未见明显超额；估算不是运行测量，最终增量的Heap余量及Task栈高水位未实机核对。未烧录或执行Git写操作。

能力限制保持不变：单候选文件、不保存旧固件副本、不自动回滚、首版无防降级或签名；MD5用于传输完整性而非真实性认证；QoS 0结果上报不保证跨重启恰好一次送达；运行期W5500全芯片静默复位协调未实现。

## 维护文档入口

- [项目上下文与架构决策](PROJECT_CONTEXT.md)
- [协议、描述头及元数据字节格式](protocol.md)
- [W5500所有权与并发契约](W5500_MULTITASK_ARCHITECTURE.md)
- [Application维护说明](../firmware/README.md)
- [Bootloader维护说明](../bootloader/README.md)
- [MQTT升级代码对照及未执行的实机步骤](MQTT_UPGRADE_IMPLEMENTATION_STEP.md)
- [历史阶段与验收记录](NEXT_STEPS.md)
- [可复制的项目简历](项目简历.md)

## Git完整归档步骤

所有命令由用户亲自执行，按顺序逐步进行；任一步报错先停止，不继续提交或强推。迁移前有效仓库仅`firmware/.git`，Boot和根文档不在旧仓库中。2026-10-04只读检查确认用户已建立根仓库`main`，旧历史位于`firmware/.git-legacy-backup`，新远端已关联；后续从步骤3继续，不重复改名或初始化。在`firmware/`内只提交应用不能完成整项目归档。

本方案是“新根仓库首快照＋原Application历史保留”，不搬迁或改写旧提交，也不修改原远端。旧历史不并入新仓库提交链，新根仓库从当前完整文件快照开始。

### 1. 新建空远端并准备忽略规则

在Git托管平台新建空仓库，例如`stm32-freertos-can-mqtt-ota`；不要自动添加README、许可证或.gitignore，不复用原Application远端。保存新URL，认证由Git交互处理，不把Token/密码嵌入URL或提交文件。

2026-10-04用户已提供[新项目仓库](https://github.com/MilkPuff0214/stm32-freertos-can-mqtt-ota)，HTTPS远端地址为`https://github.com/MilkPuff0214/stm32-freertos-can-mqtt-ota.git`。本地只读检查确认根忽略规则覆盖归档排除范围，旧应用仓库已改名保留，根仓库`main`及`origin`关联已完成；新远端是否包含初始提交尚未核实。若创建时已自动添加文件，推送前先处理其历史，不执行强推。

本轮没有直接修改根`.gitignore`的授权，以下由用户将根文件替换为：

```gitignore
# 旧应用历史完整保留，仅供本机查询
/firmware/.git-legacy-backup/

# 构建、临时、IDE及私有文件
build/
/tmp/
.vscode/
/.agents/
/.codex/
*.o
*.obj
*.elf
*.hex
*.bin
*.map
*.log
.env
.env.*
*.local.*
app_mqtt_private_config.h
app_mqtt_private_credentials.h
*.pem
*.key
*.pfx
*.p12

# 服务分发、厂商与客户原始材料只留在本机
/tools/
/resources/
/*.html
/docs/*.docx
/docs/.*.md
/firmware/笔记.md
```

`tools/`与`resources/`包含服务程序、私钥或客户原始材料，不能随首次公开归档提交。第三方库保留来源、版本和许可证；MQTT凭据仍由仓库外私有配置目录注入。`firmware/.vscode/`是历史本机绝对路径配置，本次不纳入新仓库；其他原始资料只留在本机，不删除。

按用户2026-10-04确认，`resources/FTP/`和`resources/TCP/`中更新的5份项目自有学习Markdown也只保留在本机，继续由`/resources/`整体排除；本次不为它们添加Git白名单。

### 2. 保留旧历史并建立根仓库

先在PowerShell进入**项目根目录**，不要停留在`firmware/`。下面只对已核对的`firmware/.git`改名，完整历史和原远端配置都保留；如果备份已存在，停止避免覆盖：

```powershell
$archiveRoot = (Resolve-Path -LiteralPath '.').Path
$archiveGitPath = Join-Path $archiveRoot 'firmware\.git'
$archiveBackupPath = Join-Path $archiveRoot 'firmware\.git-legacy-backup'

if (-not (Test-Path -LiteralPath 'firmware/CMakeLists.txt') -or
    -not (Test-Path -LiteralPath 'bootloader/CMakeLists.txt')) {
    throw '请先进入同时包含firmware和bootloader的项目根目录。'
}
if ((Resolve-Path -LiteralPath $archiveGitPath).Path -ne $archiveGitPath) {
    throw '旧Git目录不在预期位置，停止。'
}
if ((Get-Item -Force -LiteralPath $archiveGitPath).Attributes -band
    [IO.FileAttributes]::ReparsePoint) {
    throw '旧Git目录是链接，停止。'
}
if (Test-Path -LiteralPath $archiveBackupPath) {
    throw '历史备份已存在，停止，避免覆盖。'
}
Rename-Item -LiteralPath $archiveGitPath -NewName '.git-legacy-backup'
```

执行成功后，查询旧历史并初始化新根仓库（不删除根空`.git`）：

```powershell
git --git-dir=firmware/.git-legacy-backup --work-tree=firmware log -5 --oneline
git init -b main
```

第一次改名之后旧仓库不再被自动发现，但历史仍可用上述显式路径查询。不要再次执行改名。

### 3. 精确暂存应用、Boot与自有文档

不使用`git add .`。在项目根目录执行：

```powershell
$archivePaths = @(
    '.gitignore'
    'README.md'
    'AGENTS.md'
    'docs/*.md'
    'infrastructure/README.md'
    'dashboard/README.md'
    'firmware/.gitignore'
    'firmware/CMakeLists.txt'
    'firmware/README.md'
    'firmware/FreeRTOS学习.md'
    'firmware/User'
    'firmware/Libraries'
    'firmware/cmake'
    'firmware/linker'
    'bootloader/README.md'
    'bootloader/CMakeLists.txt'
    'bootloader/User'
    'bootloader/Libraries'
    'bootloader/cmake'
    'bootloader/linker'
)
git add -- $archivePaths
git status --short
git diff --cached --stat
git diff --cached --check
```

确认Application、Boot及本文等自有Markdown都已暂存；不要忽略真实的空白错误。LF/CRLF转换提示本身不等于错误，不为收尾批量格式化第三方源码。

再检查没有嵌套仓库gitlink、构建目录、服务分发、历史备份或密钥；以下两条应当**无输出**：

```powershell
git ls-files --stage | Select-String '^160000 '
git ls-files | Select-String '(^tools/|^resources/|\.git-legacy-backup/|(^|/)build/|\.pem$|\.key$)'
```

出现`160000`先检查未处理的嵌套仓库；其他命中也先停下，不用强制添加解决。最后查看`git diff --cached`，确认未包含私有配置或其他未打算公开的内容；浅扫不是完整保密审计。

### 4. 提交、推送与代码标签

确认暂存内容后执行：

```powershell
git commit -m "归档Application、Bootloader与项目文档"
git log -1 --oneline
```

在新项目根仓库中绑定用户已提供的新远端；不要在旧Application仓库执行，也不要填写原Application仓库地址。先确认新远端没有初始提交；如已有文件，暂停并核对历史，不强推覆盖：

```powershell
git remote add origin 'https://github.com/MilkPuff0214/stm32-freertos-can-mqtt-ota.git'
git push -u origin main
```

推送成功后，可为这个开发交付快照打代码标签：

```powershell
git tag -a v1.0.0-code -m "代码开发结项；完整升级实机验收未确认"
git push origin v1.0.0-code
git status --short
```

只推送本次标签，不使用`--tags`或`--force`。标签代表代码基线，不代表升级实机通过。工作区若出现未纳入范围的原始资料，不为“清空status”而全部添加；确认它们保持本机状态即可。

上述命令执行并推送成功后，才记录“Git完整归档完成”；当前文档不预先宣称提交或推送已发生。

## 归档后的历史与恢复

日常从项目根目录使用新仓库；原Application线上仓库保持不变，旧本地历史可用：

```powershell
git --git-dir=firmware/.git-legacy-backup --work-tree=firmware log --oneline
```

若要取消本地迁移，可在项目根目录、确认`firmware/.git`不存在时把备份改回原名：

```powershell
if (Test-Path -LiteralPath 'firmware/.git') {
    throw 'firmware/.git已存在，停止，避免覆盖。'
}
Rename-Item -LiteralPath 'firmware/.git-legacy-backup' -NewName '.git'
```

不删除任何Git目录。改回后旧应用仓库会重新被发现，应暂停根仓库的暂存操作，避免再次产生嵌套gitlink；平时只查询旧历史无需来回改名。
