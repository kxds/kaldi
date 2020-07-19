import sys

if __name__=="__main__":
    sym_w = sys.argv[1]   # words.txt
    nonterm_list = sys.argv[2]  # nonterm list
    nonterm_list = nonterm_list.split()
    dict_ = {}
    with open(sym_w, 'r') as fin:
        for line in fin:
            line = line.strip().split()
            dict_[line[0]] = line[1]

    with open('relabel.txt', 'w') as fout:
        for s in nonterm_list:
            fout.write('{} {}\n'.format(dict_['#nonterm:'+s], 0))

    