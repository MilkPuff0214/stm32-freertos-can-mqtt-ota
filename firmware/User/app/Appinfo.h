#ifndef APPINFO_H
#define APPINFO_H

/* 固定16字节字段：字符串自动NUL结尾，剩余字节补0。 */
static const char s_acAppHardwareModel[16]
    __attribute__((used, section(".appinfo.hardware"), aligned(1))) = "FIRE_F103_V2";
static const char s_acAppSoftwareModel[16]
    __attribute__((used, section(".appinfo.software"), aligned(1))) = "APP.V1.0.0";

#endif /* APPINFO_H */
