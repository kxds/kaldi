#!bin/sh

. ./path.sh
export PATH=$PATH:/Users/yijing.zhou1022/openfst-1.7.7/src/extensions/lookahead/.libs
export LD_LIBRARY_PATH=/Users/yijing.zhou1022/openfst-1.7.7/src/extensions/lookahead/.libs:/Users/yijing.zhou1022/openfst-1.7.7/src:$LD_LIBRARY_PATH:/usr/lib:/usr/local/lib
export LIBRARY_PATH=/Users/yijing.zhou1022/openfst-1.7.7/src/extensions/lookahead/.libs:$LIBRARY_PATH:/usr/lib:/usr/local/lib
export DYLD_LIBRARY_PATH=/Users/yijing.zhou1022/openfst-1.7.7/src/extensions/lookahead/.libs:$DYLD_LIBRARY_PATH:/usr/lib:/usr/local/lib
# utils/prepare_lang.sh --position-dependent-phones false data/dict "<SPOKEN_NOISE>" data/tmp data/lang


des_dir=graph/lookahead
mkdir -p $des_dir


utils/prepare_lang.sh --position-dependent-phones false data/dict "<SPOKEN_NOISE>" data/lang_tmp_nonterm data/lang_nonterm

cat >test.fst <<EOF
0 1 我 我
1 2 是 是
2 3 #nonterm:id_card4 #nonterm:id_card4
3 4 出 出
4 5 生 生
5 6 <eps> <eps>
6 
EOF
fstcompile --isymbols=data/lang_nonterm/words.txt --osymbols=data/lang_nonterm/words.txt test.fst data/lang_nonterm/G.fst
utils/mkgraph_lookahead.sh --self-loop-scale 1.0 data/lang_nonterm  /Users/yijing.zhou1022/code/model $des_dir