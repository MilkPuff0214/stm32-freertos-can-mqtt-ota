# Windows基础服务

本目录用于记录Windows侧独立运行的EMQX MQTT Broker和FTP Server部署边界、脱敏示例配置及验收方法，不保存服务程序本体或真实凭据。

## 目录

- [当前状态](#当前状态)
- [EMQX职责](#emqx职责)
- [FTP Server职责](#ftp-server职责)
- [与MQTTX调试端的边界](#与mqttx调试端的边界)
- [安全与配置规则](#安全与配置规则)

## 当前状态

P1 bxCAN和P2分层TCP基础里程碑已经关闭，固件TCP提交为`13f1b6b`、标签为`freertos-w5500-tcp-v0.1`。EMQX已在Windows侧运行，STM32已实机连接`192.168.101.11:1883`并完成MQTT业务接入；EMQX具体版本、服务启动方式和防火墙规则仍需补录。P3不开发FTP Server程序，固定使用FileZilla Server免费标准版Windows 64位最新稳定版；安装后记录准确版本号。

## EMQX职责

- 作为Windows独立服务监听MQTT连接，P2当前端口为1883。
- 负责客户端连接、认证、订阅、消息路由和会话相关能力。
- 不解析STM32业务状态机，不把MQTT PUBACK解释为设备执行成功。
- 与MQTTX生命周期分离；关闭调试端不得停止Broker。

## FTP Server职责

- 作为Windows独立服务提供受控固件目录；软件固定为FileZilla Server免费标准版Windows 64位最新稳定版，不使用Pro Enterprise、Beta或Nightly。
- 使用FTP虚拟目录`/firmware`隔离真实Windows文件路径，不向MQTT Payload暴露本机绝对路径。
- 控制监听固定为`192.168.101.11:21`；被动模式固定使用`EPSV`，数据端口固定为TCP `50000-50009`。
- FileZilla Server与Windows防火墙的被动端口范围必须一致，并形成可复现记录。
- 不参与MQTT消息路由，也不直接写入STM32或W25Q64。

P3直接使用FileZilla Server完成主链路，不安排自研Server支线；FTP服务独立运行，不由MQTTX承载。STM32只实现`EPSV/229`，不实现`PASV/227`兼容分支。

## 与MQTTX调试端的边界

MQTTX以Client身份连接EMQX，订阅`Client2Server/*`设备上行，并发布`Server2Client/*`控制、下载及升级命令；心跳只订阅观察，不回复ACK。固件字节由独立FTP Server提供，MQTT只传命令、ACK和状态。本项目不开发WPF；正常升级实机验证通过后定稿项目简历并结束。

## 安全与配置规则

- 仓库只保存占位账号和脱敏示例，不提交真实用户名、密码、证书私钥或设备唯一凭据。
- 实验阶段的1883和FTP只允许在受控局域网使用，不暴露到不受信任网络。
- EMQX监听地址、Windows防火墙、FTP虚拟目录和被动端口均需在对应阶段记录并验证。
- Broker与FTP日志不得输出FTP密码、完整认证信息或其他秘密。
