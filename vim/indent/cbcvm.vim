if exists("b:did_indent")
    finish
endif
let b:did_indent = 1

setlocal cindent
setlocal cinoptions=L0,(s,Ws,J1,j1,m1
setlocal cinkeys=0{,0},!^F,o,O,0[,0],0(,0)

setlocal nolisp		" Make sure lisp indenting doesn't supersede us
setlocal autoindent	" indentexpr isn't much help otherwise
" Also do indentkeys, otherwise # gets shoved to column 0 :-/
setlocal indentkeys=0{,0},!^F,o,O,0[,0],0(,0)
setlocal cinwords=for,if,else,while,struct,function

setlocal indentexpr=cindent(v:lnum)

let s:save_cpo = &cpo
set cpo&vim

let &cpo = s:save_cpo
unlet s:save_cpo