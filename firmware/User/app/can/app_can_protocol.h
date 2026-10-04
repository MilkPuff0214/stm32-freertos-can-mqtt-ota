#ifndef APP_CAN_PROTOCOL_H
#define APP_CAN_PROTOCOL_H

/*
 * 固件侧协议常量集中定义在这里。
 * 字段语义、超时与兼容规则仍以docs/protocol.md为规范来源。
 */
#define APP_CAN_CONTROL_COMMAND_EXTENDED_ID    0x0401F001UL
#define APP_CAN_CONTROL_ACK_EXTENDED_ID        0x040201F0UL

#define APP_CAN_CONTROL_COMMAND_DLC            1U
#define APP_CAN_CONTROL_ACK_DLC                2U
#define APP_CAN_TARGET_RED_MASK                (1U << 0U)
#define APP_CAN_TARGET_BUZZER_MASK             (1U << 1U)
#define APP_CAN_TARGET_VALID_MASK              \
    (APP_CAN_TARGET_RED_MASK | APP_CAN_TARGET_BUZZER_MASK)

#define APP_CAN_COMMAND_RESULT_OK                   0x00U
#define APP_CAN_COMMAND_RESULT_INVALID_DLC          0x01U
#define APP_CAN_COMMAND_RESULT_INVALID_STATE        0x02U



#define APP_CAN_ADC_REPORT_EXTENDED_ID         0x110001FFUL

#define APP_CAN_ADC_REPORT_DLC                    6U
#define APP_CAN_ADC_INVALID_RAW_VALUE             0xFFFFU
#define APP_CAN_ADC_POTENTIOMETER_VALID_MASK      (1U << 0U)
#define APP_CAN_ADC_TEMPERATURE_VALID_MASK        (1U << 1U)

#endif /* APP_CAN_PROTOCOL_H */