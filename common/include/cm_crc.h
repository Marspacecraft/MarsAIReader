/*
 * 校验和。
 *
 * 只提供两个算法，够用且够标准：
 *   cm_crc16  = CRC-16/CCITT-FALSE（poly 0x1021, init 0xFFFF, 不反射, 无 xorout）
 *               用于短帧校验（AT 命令、NVS 小记录）
 *   cm_crc32  = CRC-32/ISO-HDLC（poly 0xEDB88320 反射, init 0xFFFFFFFF, 反射, xorout 0xFFFFFFFF）
 *               与 zlib / png / zip 一致，用于书库文件、字体包、镜像校验
 *
 * seed 语义（便于增量计算，不需要把整块数据放进连续内存）：
 *   cm_crc16(0xFFFF, a, na) → 再 cm_crc16(返回值, b, nb) == cm_crc16(0xFFFF, a+b)
 *   cm_crc32(0,      a, na) → 再 cm_crc32(返回值, b, nb) == cm_crc32(0,      a+b)
 * 口径不同是历史约定（crc16 的 seed 是"当前寄存器值"，crc32 的 seed 是"已算出的校验值"），
 * 两者都自洽，已在测试中用标准向量锁定。
 *
 * 实现选择：逐位运算，不查表。256×4 = 1KB 常量表对本项目"分区已满、flash 紧张"
 * 不划算；调用点都是 KB 级而非 MB 级数据。若将来出现热路径，可在不改接口的前提下换查表。
 */
#ifndef CM_CRC_H
#define CM_CRC_H

#include "cm_types.h"

uint16_t cm_crc16(uint16_t seed, const void *data, cm_size n);
uint32_t cm_crc32(uint32_t seed, const void *data, cm_size n);

#endif /* CM_CRC_H */
