#!/usr/bin/env bash

# This script demonstrates how to use the grammar-decoding framework to build
# graphs made out of more than one part.  It demonstrates using `fstequivalent`
# that the graph constructed this way is equivalent to what you would create if
# you had the LM all as a single piece.  This uses the command line tools to
# expand to a regular FST (--write-as-grammar=false) In practice you might not
# want do to that, since the result might be large, and since writing the entire
# thing might take too much time.  The code itself allows you to construct these
# GrammarFst objects in lightweight way and decode using them.

stage=3
set -e
. ./path.sh
. utils/parse_options.sh


tree_dir=
nonterminal='currency datetime'
graph_dir=data/lang_nosp_grammar2
lm_file=

# For the purposes of this script we just need a biphone tree and associated
# transition-model for testing, because we're testing it at the graph level,
# i.e. testing equivalence of compiled HCLG graphs; there is no decoding
# involved here.

# We're doing this with the "no-silprobs" dictionary dir for now, as we
# need to write some scripts to support silprobs with this.

# For reference, the original command we
#utils/prepare_lang.sh data/local/dict_nosp \
#   "<UNK>" data/local/lang_tmp_nosp data/lang_nosp

if [ $stage -le 0 ]; then
#  [ -d data/local/dict_nosp_grammar1 ] && rm -r data/local/dict_nosp_grammar1
#  cp -r data/local/dict_nosp data/local/dict_nosp_grammar1
#  nonterminals.txt should be included in the dict

  [ -f data/lang_nosp_grammar1/G.fst ] && rm data/lang_nosp_grammar1/G.fst
  utils/prepare_lang.sh data/local/dict_nosp_grammar1 \
       "<SPOKEN NOISE>" data/local/lang_tmp_nosp data/lang_nosp_grammar1

  python3 get_relabel.py data/lang_nosp_grammar1/words.txt "$nonterminal"
fi

if [ $stage -le 1 ]; then
  # Most contents of these directories will be the same, only G.fst differs, but
  # it's our practice to make these things as directories combining G.fst with
  # everything else.
  rm -r  data/lang_nosp_grammar2{a,b} 2>/dev/null || true
  cp -r data/lang_nosp_grammar1 data/lang_nosp_grammar2a
  cp -r data/lang_nosp_grammar1 data/lang_nosp_grammar2b
  cp -r data/lang_nosp_grammar1 data/lang_nosp_grammar2
fi

if [ $stage -le 2 ]; then
  # Create a simple G.fst in data/lang_nosp_grammar1, which won't
  # actually use any grammar stuff, it will be a baseline to test against.

  lang=data/lang_nosp_grammar1
  cp /mnt/nfs2/yijing.zhou/asr/lats/data/lang_purebiaozhu/G.fst $lang/G.fst

  utils/mkgraph.sh --self-loop-scale 1.0 $lang $tree_dir $tree_dir/grammar1

fi

thraxdir=/mnt/nfs2/yijing.zhou/thrax-1.2.6/mygrammars/
mkdir -p data/local/lm
if [ $stage -le -1 ]; then 
  # prepare grammar fst by thrax and copy to data/local/lm dir
  for nonterm in $nonterminal;do
    thraxmakedep $thraxdir/${nonterm}.grm
    make
    farextract $thraxdir/${nonterm}.far 
    cp $thraxdir/${nonterm} data/local/lm/G_${nonterm}.fst
  done
fi


if [ $stage -le 3 ]; then
  # create the top-level graph in data/lang_nosp_grammar2a

  # you can of course choose to put what symbols you want on the output side, as
  # long as they are defined in words.txt.  #nonterm:contact_list, #nonterm_begin
  # and #nonterm_end would be defined in this example.  This might be useful in
  # situations where you want to keep track of the structure of calling
  # nonterminals.
  lang=data/lang_nosp_grammar2a
  mkdir -p $graph_dir
  cp $lang/L*fst $lang/graph
  cp $lang/*.txt $lang/graph
  cp -r $lang/phone $lang/graph/.
  arpa2fst --disambig-symbol=#0 --max-arpa-warnings=-1 --read-symbol-table=$lang/words.txt \
          $lm_file $lang/graph/G.fst
  fstrelabel --relabel_opairs=relabel.txt $lang/graph/G.fst $lang/graph/b.fst
  mv $lang/graph/b.fst $lang/graph/G.fst
  utils/mkgraph.sh --self-loop-scale 1.0 $lang/graph $tree_dir $graph_dir/grammar2a

fi

if [ $stage -le 4 ]; then
  # Create the graph for the nonterminal in data/lang_nosp_grammar2b
  # Again, we don't choose to put these symbols on the output side, but it would
  # be possible to do so.
  lang=data/lang_nosp_grammar2b
  for nonterm in $nonterminal;do
    mkdir -p $lang/graph/$nonterm
    cp $lang/L*fst $lang/graph/$nonterm
    cp $lang/*.txt $lang/graph/$nonterm
    cp -r $lang/phones $lang/graph/$nonterm
    cp data/local/lm/G_$nonterm.fst $lang/graph/$nonterm/G.fst
    utils/mkgraph.sh --self-loop-scale 1.0 $lang/graph/$nonterm $tree_dir \
             $graph_dir/grammar2b/$nonterm
  done

fi

if [ $stage -le 5 ]; then
  # combine the top-level graph and the sub-graph together using the command
  # line tools. (In practice you might want to do this from appliation code).

  lang=data/lang_nosp_grammar2a
  offset=$(grep nonterm_bos $lang/phones.txt | awk '{print $2}') 
  clist1=$(grep nonterm:currency $lang/phones.txt | awk '{print $2}')
  clist2=$(grep nonterm:datetime $lang/phones.txt | awk '{print $2}') 
  # the graph in $tree_dir/grammar2/HCLG.fst will be a normal FST (ConstFst)
  # that was expanded from the grammar.  (we use --write-as-grammar=false to
  # make it expand it).  This is to test equivalence to the one in
  # $tree_dir/grammar1/
  make-grammar-fst --write-as-grammar=false --nonterm-phones-offset=$offset $graph_dir/grammar2a/HCLG.fst \
                   $clist1 $graph_dir/grammar2b/currency/HCLG.fst  \
                   $clist2 $graph_dir/grammar2b/datetime/HCLG.fst  $graph_dir/HCLG.fst
fi

if [ $stage -le 5 ]; then
  # combine the top-level graph and the sub-graph together using the command
  # line tools. (In practice you might want to do this from appliation code).

  lang=data/lang_nosp_grammar2a
  offset=$(grep nonterm_bos $lang/phones.txt | awk '{print $2}') 
  cmd_="make-grammar-fst --write-as-grammar=false --nonterm-phones-offset=$offset $graph_dir/grammar2a/HCLG.fst"
  for s in nonterminal;do
    clist=$(grep nonterm:$s $lang/phones.txt | awk '{print $2}')
    cmd_="$cmd_ $clist $graph_dir/grammar2b/$s/HCLG.fst"
  done
  cmd_="$cmd_ $graph_dir/HCLG.fst"
  $cmd_
fi

# if [ $stage -ge 6 ]; then
#   # Test equivalence using a random path.. can be useful for debugging if
#   # fstequivalent fails.
#   echo "$0: will print costs with the two FSTs, for one random path."
#   fstrandgen $tree_dir/grammar1/HCLG.fst > path.fst
#   for x in 1 2; do
#     fstproject --project_output=false path.fst | fstcompose - $tree_dir/grammar${x}/HCLG.fst | fstcompose - <(fstproject --project_output=true path.fst) > composed.fst
#     start_state=$(fstprint composed.fst | head -n 1 | awk '{print $1}')
#     fstshortestdistance --reverse=true composed.fst | awk -v s=$start_state '{if($1 == s) { print $2; }}'
#   done

# fi

# if [ $stage -ge 7 ]; then
#   echo "$0: will test equivalece using fstequivalent"
#   if fstequivalent --delta=0.01 --random=true --npath=100 $tree_dir/grammar1/HCLG.fst $tree_dir/grammar2/HCLG.fst; then
#     echo "$0: success: the two were equivalent"
#   else
#     echo "$0: failure: the two were inequivalent"
#   fi
# fi
