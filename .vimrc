syntax on " 構文ハイライト有効

set belloff=all " ビープ音を消す
set termguicolors " True Color有効
set nocompatible " Vimの挙動にする
set nu " 行番号を表示
set tabstop=2 " タブ幅
set shiftwidth=2 " シフト幅
set expandtab " タブを展開
set autoindent " オートインデント
set nowrap " 折り返しなし
set incsearch " インクリメンタルサーチ有効
set ignorecase " 大文字小文字を区別しない
set smartcase " 検索パターンが大文字を含んでいたら大文字小文字を区別する
set virtualedit=onemore " 行末の後ろにカーソルを置けるようになる

" エンコーディング設定
set encoding=utf-8
set fileencodings=iso-2022-jp,cp932,sjis,euc-jp,utf-8

" ステータス行を常に表示
set laststatus=2

" ステータスラインの設定
set statusline=[%{&readonly?'読込':'通常'}]\ %f%m\ %=\ %y\ [%{&fileencoding!=''?&fileencoding:&encoding}]\ [%{&fileformat}]\ [%l]\ [%v]

" バッファ移動設定
nnoremap <silent>bp :bprevious<CR> " 前のバッファ
nnoremap <silent>bn :bnext<CR> " 次のバッファ
nnoremap <silent>bb :b#<CR> " N番目のバッファ

" インサートモードの時のカーソル形状を変更
let &t_SI .= "\e[5 q"
let &t_EI .= "\e[1 q"

" QuickFix設定
" 前へ
nnoremap [q :cprevious<CR>
" 次へ
nnoremap ]q :cnext<CR>
" 最初へ
nnoremap [Q :<C-u>cfirst<CR>
" 最後へ
nnoremap ]Q :<C-u>clast<CR>
