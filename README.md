# Groonga output utility plugin

Groongaのoutput_columns向けプラグイン

### ```output_copy```関数
output出力結果を所定のカラムにコピーする。キャストなど特別なことはなにもしない。

### ```output_group```関数
output出力結果でtable_groupする。出力レコード１件１件ずつGroupするため、演算コストが高いことに注意。

## Install

Install libgroonga-dev / groonga-devel

Build this command.

    % sh autogen.sh
    % ./configure
    % make
    % sudo make install
