"
" Vim syntax highlighting for hibernate.conf files.
"
" To load manually, open your hibernate.conf and type :set ft=hibernate
"
" To load automatically, copy this file to your ~/.vim/syntax/ and put the
" following into your .vimrc:
"
"   augroup filetypedetect
"       au BufNewFile,BufRead hibernate.conf set filetype=hibernate
"   augroup END
"

" Initial setup:
syntax clear
syntax case ignore

" Erroneous components:
syntax match hibernate_error /\S\+/
highlight link hibernate_error Error

" Various allowable lines:
syntax match hibernate_start_line /^/ nextgroup=hibernate_error,hibernate_comment,hibernate_conf

" General syntax items:
syntax keyword hibernate_boolean on off yes no 1 0 contained nextgroup=hibernate_error
highlight link hibernate_boolean Constant

syntax match hibernate_integer /\d\+/ contained nextgroup=hibernate_error
highlight link hibernate_integer Constant

syntax match hibernate_order_num /\d\d/ contained nextgroup=hibernate_filenames
highlight link hibernate_order_num Constant

" Builtins :
syntax keyword hibernate_conf swsuspvt contained nextgroup=hibernate_integer skipwhite
syntax keyword hibernate_conf verbosity contained nextgroup=hibernate_verbosity skipwhite
syntax keyword hibernate_conf logfile contained nextgroup=hibernate_filenames skipwhite
syntax keyword hibernate_conf logverbosity contained nextgroup=hibernate_verbosity skipwhite
syntax keyword hibernate_conf alwaysforce contained nextgroup=hibernate_boolean skipwhite
syntax keyword hibernate_conf alwayskill contained nextgroup=hibernate_boolean skipwhite
syntax keyword hibernate_conf distribution contained nextgroup=hibernate_distribution skipwhite
highlight link hibernate_conf Keyword

syntax match hibernate_filenames /.*$/ contained
highlight link hibernate_filenames Constant

syntax match hibernate_verbosity /[0-4]/ contained
highlight link hibernate_verbosity Constant

syntax keyword hibernate_distribution debian fedora mandrake redhat gentoo suse slackware contained
highlight link hibernate_distribution Special

" bootsplash
syntax keyword hibernate_conf bootsplash contained nextgroup=hibernate_boolean skipwhite
syntax keyword hibernate_conf bootsplashconfig contained nextgroup=hibernate_filenames skipwhite

" clock
syntax keyword hibernate_conf saveclock contained nextgroup=hibernate_boolean,hibernate_clock_restore_only skipwhite
syntax keyword hibernate_clock_restore_only restore[-only] contained
highlight link hibernate_clock_restore_only Constant

" devices
syntax keyword hibernate_conf incompatibledevices contained nextgroup=hibernate_filenames skipwhite

" disk cache
syntax keyword hibernate_conf disablewritecacheon contained nextgroup=hibernate_filenames skipwhite

" filesystems
syntax keyword hibernate_conf unmount contained nextgroup=hibernate_filenames skipwhite

" grub
syntax keyword hibernate_conf changegrubmenu contained nextgroup=hibernate_boolean skipwhite
syntax keyword hibernate_conf grubmenufile contained nextgroup=hibernate_filenames skipwhite
syntax keyword hibernate_conf alternategrubmenufile contained nextgroup=hibernate_filenames skipwhite

" lock
syntax match hibernate_username /[a-zA-Z0-9\-_]\+/ contained skipwhite
highlight link hibernate_username Constant

syntax keyword hibernate_conf lockkde contained nextgroup=hibernate_boolean skipwhite
syntax keyword hibernate_conf lockxscreensaver contained nextgroup=hibernate_filenames skipwhite
syntax keyword hibernate_conf lockconsoleas contained nextgroup=hibernate_username skipwhite

" misclaunch
syntax keyword hibernate_conf onsuspend contained nextgroup=hibernate_order_num skipwhite
syntax keyword hibernate_conf onresume contained nextgroup=hibernate_order_num skipwhite

" modules
syntax match hibernate_modules /[a-zA-Z0-9\-_]\+/ contained nextgroup=hibernate_modules skipwhite
highlight link hibernate_modules Constant

syntax keyword hibernate_modules_auto auto contained nextgroup=hibernate_modules skipwhite
highlight link hibernate_modules_auto Constant

syntax keyword hibernate_conf unloadmodules contained nextgroup=hibernate_modules skipwhite
syntax keyword hibernate_conf loadmodules contained nextgroup=hibernate_modules_auto,hibernate_modules skipwhite

syntax keyword hibernate_conf unloadallmodules contained nextgroup=hibernate_boolean skipwhite
syntax keyword hibernate_conf loadmodulesfromfile contained nextgroup=hibernate_filenames skipwhite

" modules_gentoo
syntax keyword hibernate_conf gentoomodulesautoload contained nextgroup=hibernate_boolean skipwhite

" network
syntax match hibernate_interfaces /[a-z0-9\.]\+/ contained nextgroup=hibernate_interfaces skipwhite
highlight link hibernate_interfaces Constant

syntax keyword hibernate_network_auto auto contained nextgroup=hibernate_interfaces skipwhite
highlight link hibernate_network_auto Constant

syntax keyword hibernate_conf downinterfaces contained nextgroup=hibernate_interfaces skipwhite
syntax keyword hibernate_conf upinterfaces contained nextgroup=hibernate_interfaces,hibernate_network_auto skipwhite

" programs
syntax match hibernate_programs /\S\+/ contained nextgroup=hibernate_programs skipwhite
highlight link hibernate_programs Constant

syntax keyword hibernate_conf incompatibleprograms contained nextgroup=hibernate_programs skipwhite

" services
syntax match hibernate_services /\S\+/ contained nextgroup=hibernate_services skipwhite
highlight link hibernate_services Constant

syntax keyword hibernate_conf stopservices contained nextgroup=hibernate_services skipwhite
syntax keyword hibernate_conf startservices contained nextgroup=hibernate_services skipwhite
syntax keyword hibernate_conf restartservices contained nextgroup=hibernate_services skipwhite

" swsusp2
syntax match hibernate_swsusp2allsettings /\d\+\(\s\+\d\+\)\+/ contained skipwhite
highlight link hibernate_swsusp2allsettings Constant

syntax keyword hibernate_conf useswsusp2 contained nextgroup=hibernate_boolean skipwhite
syntax keyword hibernate_conf reboot contained nextgroup=hibernate_boolean skipwhite
syntax keyword hibernate_conf enableescape contained nextgroup=hibernate_boolean skipwhite
syntax keyword hibernate_conf defaultconsolelevel contained nextgroup=hibernate_integer skipwhite
syntax keyword hibernate_conf swsusp2allsettings contained nextgroup=hibernate_swsusp2allsettings skipwhite

" sysfspowerstate
syntax keyword hibernate_conf usesysfspowerstate contained nextgroup=hibernate_sysfspowerstate skipwhite
syntax match hibernate_sysfspowerstate /\(disk\|mem\|standby\)/ contained skipwhite
highlight link hibernate_sysfspowerstate Special

" xhacks
syntax keyword hibernate_conf leavexbeforesuspend contained nextgroup=hibernate_boolean skipwhite
syntax keyword hibernate_conf nvidiahack contained nextgroup=hibernate_boolean skipwhite

" Full-line comments:
syntax match hibernate_comment /^#.*/
highlight link hibernate_comment Comment

