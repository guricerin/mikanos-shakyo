/// メモリページング機能 : メモリアドレス空間をページ単位で管理する
/// リニアアドレス（ソフトウェア[アセンブリ含む]が指定するアドレス）を物理アドレス（CPUがメモリを読み書きするときに使うアドレス）に変換
/// 正確にはソフトウェアが指定するのは論理アドレスで、セグメンテーションによってリニアアドレスに変換されるが、64bitモードではセグメンテーションによる変換は行われないので論理アドレス=リニアアドレス

#pragma once

#include <cstddef>

/// 静的に確保するページディレクトリの個数
/// SetupIdentityPageMap()で使用される
/// 1つのページディレクトリには512個の2MiBページを設定できるので、
/// kPageDirectoryCount * 1GiBの仮想アドレスがマッピングされることになる
const size_t kPageDirectoryCount = 64;

/// 仮想アドレス=物理アドレスとなるようにページテーブルを設定
/// 最終的にCR3レジスタが正しく設定されたページテーブルを指すようになる
void SetupIdentityPageTable();