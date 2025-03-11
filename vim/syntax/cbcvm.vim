let s:cpo_save = &cpo
set cpo&vim

syn case match

syn keyword cbcvmCommentTodo TODO FIXME NOTE XXX contained
syn match cbcvmComment "#.*" contains=@Spell,cbcvmCommentTodo
syn match cbcvmSpecial "\\."
syn region cbcvmString start=+"+ skip=+\\\\\|\\"+ end=+"+ contains=cbcvmSpecial
syn region cbcvmCharacter start=+'+ skip=+\\\\\|\\'+ end=+'+ contains=cbcvmSpecial

syn match cbcvmNumber "\<0x[0-9a-fA-F]\+\>"
syn match cbcvmNumber "\<[1-9]\d*\(\.\d*\)\?\>"
syn match cbcvmNumber "\<0\(\.\d*\)\?\>"
syn match cbcvmFunction "\h\w*" display contained

syn keyword cbcvmIdentifier this
syn keyword cbcvmConditional if else
syn keyword cbcvmRepeat for while 
syn keyword cbcvmBranch break continue
syn keyword cbcvmStatement return import
syn keyword cbcvmBoolean true false
syn keyword cbcvmKeyword let
syn keyword cbcvmNull null
syn keyword cbcvmException try catch throw
syn keyword cbcvmStatement function nextgroup=cbcvmFunction skipwhite
syn keyword cbcvmModifier export
syn keyword cbcvmInclude import
syn keyword cbcvmStructure struct nextgroup=cbcvmFunction skipwhite
syn keyword cbcvmIntrinsics print println tostring typeof string_chars
            \ string_from_chars string_bytes string_concat ord chr truncate32
            \ tofloat read_file read_file_bytes argv __upvalues apply now toint
            \ __gc_collect __dis arguments
syn match cbcvmBraces "[{}\[\]]"
syn match cbcvmParens "[()]"

syn match cbcvmStructField /\.\h\w*/hs=s+1 contains=ALLBUT,cbcvmFunction,cbcvmIntrinsics transparent
syn match cbcvmModuleMember /::\h\w*/hs=s+1 contains=ALLBUT,cbcvmFunction,cbcvmIntrinsics transparent

" FIXME: what does this do?
syn sync fromstart
syn sync maxlines=100
syn sync ccomment cbcvmComment

hi def link cbcvmComment Comment
hi def link cbcvmCommentTodo Todo
hi def link cbcvmSpecial Special
hi def link cbcvmString String
hi def link cbcvmCharacter Character
hi def link cbcvmNumber Number
hi def link cbcvmConditional Conditional
hi def link cbcvmRepeat Repeat
hi def link cbcvmBranch Conditional
hi def link cbcvmStatement Statement
hi def link cbcvmBraces Function
hi def link cbcvmNull Keyword
hi def link cbcvmBoolean Boolean
hi def link cbcvmRegexpString String
hi def link cbcvmIdentifier Identifier
hi def link cbcvmException Exception
hi def link cbcvmModifier StorageClass
hi def link cbcvmInclude Include
hi def link cbcvmKeyword Keyword
hi def link cbcvmStructure Structure
hi def link cbcvmFunction Function
hi def link cbcvmIntrinsics Function

let &cpo = s:cpo_save
unlet s:cpo_save