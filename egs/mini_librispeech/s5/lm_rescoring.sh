#!/bin/sh

## rescoring
baiying_lm=/mnt/nfs2/yijing.zhou/lm/ngram/lm_3m/baiying_1911.lm
utils/format_lm.sh /mnt/nfs2/yijing.zhou/egs/mini_librispeech/s5/data/lang_nosp_grammar1 $baiying_lm \
    /mnt/nfs2/yijing.zhou/egs/mini_librispeech/s5/data/local/dict_nosp  \
    /mnt/nfs2/yijing.zhou/asr/lats/data/lang_baiying_1911

src_dir=/mnt/nfs2/yijing.zhou/asr/lats/exp/xiaomei/chain_converData/
lat1_dir=
lat2_dir=
G_old1=/mnt/nfs2/yijing.zhou/asr/lats/data/
G_old2=/mnt/nfs2/yijing.zhou/asr/lats/data/lang_baiying_1911/HCLG.fst
carpaG_new=/mnt/nfs/jiachen.huang/lm/lm/cuishou_merge_case_5gram.carpa
lat_dir=lm_rescore

acscale=0.1

lattice-to-nbest --acoustic-scale=$acscale --n=5 ark:$lat1_dir ark:$lat_dir/lat.1.nbest
lattice-to-nbest --acoustic-scale=$acscale --n=5 ark:$lat2_dir ark:$lat_dir/lat.2.nbest
lattice-lmrescore --lm-scale=-1.0 ark:$lat_dir/lat.1.nbest $G_old1 ark:$lat_dir/n1.lats
lattice-lmrescore --lm-scale=-1.0 ark:$lat_dir/lat.2.nbest $G_old2 ark:$lat_dir/n2.lats
lattice-combine $lat_dir/n1.lats $lat_dir/n2.lats ark:$lat_dir/nolm.lats
lattice-lmrescore-const-arpa --lm-scale=1.0 ark:$lat_dir/nolm.lats $carpaG_new ark:$lat_dir/out_nolm.lats

lattice-to-nbest --acoustic-scale=1 --n=10 ark:$lat_dir/out_nolm.lats ark:$lat_dir/out_nolm.nbest
nbest-to-linear ark:$lat_dir/out_nolm.nbest ark:$lat_dir/nolm.ali 'ark,t:|int2sym.pl -f 2- words.txt > $lat_dir/nolm.words' ark:$lat_dir/nolm.lmscore ark:$lat_dir/nolm.acscore

paste $lat_dir/nolm.acscore $lat_dir/nolm.lmscore $lat_dir/nolm.words | awk '{$1=-"$acscale"*$2-$4;$2="";$3="";$4="";print $0}' > $lat_dir/nolm.new 


