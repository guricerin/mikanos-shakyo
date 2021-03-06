/// 割り込み用プログラム

#pragma once

#include <array>
#include <cstdint>
#include <deque>

#include "message.hpp"
#include "x86_descriptor.hpp"

union InterruptDescriptorAttribute {
    uint16_t data;
    // ビットフィールド : 任意のビット幅を指定できる
    struct {
        /// 今回は常に0を設定
        uint16_t interrupt_stack_table : 3;
        uint16_t : 5;
        DescriptorType type : 4;
        uint16_t : 1;
        /// DPL : 割り込みハンドラの実行権限
        uint16_t descriptor_privilege_level : 2;
        /// 記述子が有効 : 1
        uint16_t present : 1;
    } __attribute__((packed)) bits;
} __attribute__((packed)); // 構造体の各フィールドを詰める

/// 割り込み記述子
struct InterruptDescriptor {
    uint16_t offset_low;
    /// 割り込みハンドラを実行する際のコードセグメント
    uint16_t segment_selector;
    InterruptDescriptorAttribute attr;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

/// 割り込みベクタ番号（割り込み要因番号、割り込みベクトル）
class InterruptVector {
public:
    enum Number {
        kXHCI = 0x40,
        kLAPICTimer = 0x41,
    };
};

/// 割り込みフレーム
struct InterruptFrame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

/// 割り込み記述子テーブル : Interrupt Descriptor Table
/// 割り込み要因番号（割り込みベクトル）と割り込みハンドラの対応表
/// 番号は、x86-64アーキテクチャでは[0, 255]
extern std::array<InterruptDescriptor, 256> g_idt;

constexpr InterruptDescriptorAttribute MakeIDTAttr(
    DescriptorType type,
    uint8_t descriptor_privilege_level,
    bool present = true,
    uint8_t interrupt_stack_table = 0) {
    InterruptDescriptorAttribute attr{};
    attr.bits.interrupt_stack_table = interrupt_stack_table;
    attr.bits.type = type;
    attr.bits.descriptor_privilege_level = descriptor_privilege_level;
    attr.bits.present = present;
    return attr;
}

/// ISTのインデックス（[1, 7]まであり、どれを使うかは自由）
/// IST : interrupt stack table
/// 割り込みハンドラを実行する際、事前に設定しておいたスタックを必ず使うようにする仕組み
/// ISTはTSSに含まれる
const int kISTForTimer = 1;

void SetIDTEntry(InterruptDescriptor& desc,
                 InterruptDescriptorAttribute attr,
                 uint64_t offset,
                 uint16_t segment_selector);

void NotifyEndOfInterrupt();

void InitializeInterrupt();
