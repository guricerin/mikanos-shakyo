/// メモリページング機能 : メモリアドレス空間を固定長の領域（ページ）単位で管理する
/// リニアアドレス（ソフトウェア[アセンブリ含む]が指定するアドレス）を物理アドレス（CPUがメモリを読み書きするときに使うアドレス）に変換
/// 正確にはソフトウェアが指定するのは論理アドレス（仮想アドレス）で、セグメンテーションによってリニアアドレスに変換されるが、
/// x86-64の64bitモードではセグメンテーションによる変換は行われないので、論理アドレス=リニアアドレス

#pragma once

#include <cstddef>
#include <cstdint>

#include "error.hpp"

/// 静的に確保するページディレクトリの個数
/// SetupIdentityPageMap()で使用される
/// 1つのページディレクトリには512個の2MiBページを設定できるので、
/// kPageDirectoryCount * 1GiBの仮想アドレスがマッピングされることになる
const size_t kPageDirectoryCount = 64;

/// 仮想アドレス=物理アドレスとなるようにページテーブルを設定
/// 最終的にCR3レジスタが正しく設定されたページテーブルを指すようになる
void SetupIdentityPageTable();

void InitializePaging();

/// CR3がOSカーネル用のPML4を指すように設定
void ResetCR3();

/// 階層ページング構造における仮想アドレス
union LinearAddress4Level {
    uint64_t value;

    struct {
        uint64_t offset : 12;
        uint64_t page : 9;
        uint64_t dir : 9;
        uint64_t pdp : 9;
        uint64_t pml4 : 9;
        uint64_t : 9;
    } __attribute__((packed)) parts;

    int Part(int page_map_level) const {
        switch (page_map_level) {
        case 0:
            return parts.offset;
        case 1:
            return parts.page;
        case 2:
            return parts.dir;
        case 3:
            return parts.pdp;
        case 4:
            return parts.pml4;
        default:
            return 0;
        }
    }

    void SetPart(int page_map_level, int value) {
        switch (page_map_level) {
        case 0:
            parts.offset = value;
            break;
        case 1:
            parts.page = value;
            break;
        case 2:
            parts.dir = value;
            break;
        case 3:
            parts.pdp = value;
            break;
        case 4:
            parts.pml4 = value;
            break;
        }
    }
};

/// ページング構造のエントリ
/// これが各階層毎に512個並んでいる
/// 63:52 : 全部0
/// 51:12 : 1つ下位の階層を指す物理アドレス
/// 11:0  : フラグ（Readable, Writable, Executable）
union PageMapEntry {
    uint64_t data;

    struct {
        /// エントリが有効 : 1
        uint64_t present : 1;
        /// エントリが表す仮想アドレス範囲への書き込みを許可 : 1
        uint64_t writable : 1;
        /// CPL（Current Privilege Level, CSのRPLフィールド、CPUの現在の動作権限レベル）がどんな値でもメモリアクセスを許可 : 1
        uint64_t user : 1;
        uint64_t write_through : 1;
        uint64_t cache_disable : 1;
        uint64_t accessed : 1;
        uint64_t dirty : 1;
        uint64_t huge_page : 1;
        uint64_t global : 1;
        uint64_t : 3;
        /// 1つ下位の階層ページング構造の先頭アドレス
        uint64_t addr : 40;
        uint64_t : 12;
    } __attribute__((packed)) bits;

    /// 1つ下位の階層ページング構造の先頭を指す
    PageMapEntry* Pointer() const {
        return reinterpret_cast<PageMapEntry*>(bits.addr << 12);
    }

    /// 1つ下位の階層ページング構造の先頭を設定
    void SetPointer(PageMapEntry* p) {
        bits.addr = reinterpret_cast<uint64_t>(p) >> 12;
    }
};

WithError<PageMapEntry*> NewPageMap();
Error FreePageMap(PageMapEntry* table);
Error SetupPageMaps(LinearAddress4Level addr, size_t num_4kpages, bool writable = true);
Error CleanPageMaps(LinearAddress4Level addr);
Error CopyPageMaps(PageMapEntry* dest, PageMapEntry* src, int part, int start);
/// デマンドページング : 初めはどのページに対してもフレームを割り当てないでおき、
/// ページに初めてアクセスされたときにそのページだけフレームを割り当てる
/// ページフォルトのエラーコードのビット定義 :
/// bit位置 | bit名 | 意味
/// 0       | P     | 0 = 存在しないページ、1 = ページの権限違反
/// 1       | W/R   | 0 = 読み込み、1 = 書き込み
/// 2       | U/S   | 0 = スーパーバイザーモードのアクセス、1 = ユーザーモードのアクセス
/// 3       | RSVD  | 0 = 予約ビットの違反が例外の原因ではない、1 = 予約ビットが1になっている
Error HandlePageFault(uint64_t error_code, uint64_t causal_addr);
