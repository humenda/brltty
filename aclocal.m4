AC_DEFUN([BRLTTY_SEARCH_LIBS], [dnl
brltty_uc="`echo "$1" | sed -e 'y%abcdefghijklmnopqrstuvwxyz%ABCDEFGHIJKLMNOPQRSTUVWXYZ%'`"
AC_SEARCH_LIBS([$1], [$2], [AC_DEFINE_UNQUOTED(HAVE_FUNC_${brltty_uc})])])

AC_DEFUN([BRLTTY_VAR_TRIM], [dnl
changequote(, )dnl
$1="`expr "${$1}" : ' *\(.*[^ ]\) *$'`"
changequote([, ])])

AC_DEFUN([BRLTTY_DEFINE_EXPANDED], [dnl
eval 'brltty_expanded="'$2'"'
AC_DEFINE_UNQUOTED([$1], ["${brltty_expanded}"])])

AC_DEFUN([BRLTTY_ARG_WITH], [dnl
AC_ARG_WITH([$1], BRLTTY_HELP_STRING([--with-$1=$2], [$3]), [$4="${withval}"], [$4=$5])])

AC_DEFUN([BRLTTY_ARG_ENABLE], [dnl
BRLTTY_ARG_FEATURE([$1], [$2], [enable], [no], [$3], [$4], [$5])])

AC_DEFUN([BRLTTY_ARG_DISABLE], [dnl
BRLTTY_ARG_FEATURE([$1], [$2], [disable], [yes], [$3], [$4], [$5])])

AC_DEFUN([BRLTTY_ARG_FEATURE], [dnl
AC_ARG_ENABLE([$1], BRLTTY_HELP_STRING([--$3-$1], [$2]), [], [enableval="$4"])
case "${enableval}" in
   yes)
      $5
      ;;
   no)
      $6
      ;;
   *)
      ifelse($7, [], [AC_MSG_ERROR([unexpected value for feature $1: ${enableval}])], [$7])
      ;;
esac
pushdef([var], brltty_enabled_[]translit([$1], [-], [_]))dnl
var="${enableval}"
BRLTTY_SUMMARY_ITEM([$1], var)dnl
popdef([var])])

AC_DEFUN([BRLTTY_HELP_STRING], [dnl
AC_HELP_STRING([$1], patsubst([$2], [
.*$]), [brltty_help_prefix])dnl
changequote(<, >)dnl
patsubst(patsubst(<$2>, <\`[^
]*>), <
>, <\&brltty_help_prefix>)<>dnl
changequote([, ])dnl
])
m4_define([brltty_help_indent], 32)
m4_define([brltty_help_prefix], m4_format([%]brltty_help_indent[s], []))
m4_define([brltty_help_width], m4_eval(79-brltty_help_indent))

AC_DEFUN([BRLTTY_ITEM], [dnl
define([brltty_item_list_$1], ifdef([brltty_item_list_$1], [brltty_item_list_$1])[
m4_text_wrap([$3], [      ], [- $2  ], brltty_help_width)])dnl
brltty_item_entries_$1="${brltty_item_entries_$1} $2-$3"
brltty_item_codes_$1="${brltty_item_codes_$1} $2"
brltty_item_names_$1="${brltty_item_names_$1} $3"])

AC_DEFUN([BRLTTY_BRAILLE_DRIVER], [dnl
BRLTTY_ITEM([braille], [$1], [$2])])

AC_DEFUN([BRLTTY_SPEECH_DRIVER], [dnl
BRLTTY_ITEM([speech], [$1], [$2])])

AC_DEFUN([BRLTTY_ARG_ITEM], [dnl
BRLTTY_ITEM([$1], [no], [])
brltty_item_entries_$1=" ${brltty_item_entries_$1} "
BRLTTY_VAR_TRIM([brltty_item_codes_$1])
BRLTTY_VAR_TRIM([brltty_item_names_$1])
BRLTTY_ARG_WITH(
   [$1-$2], translit([$2], [a-z], [A-Z]),
   [$1 $2 to build in]brltty_item_list_$1,
   [brltty_item], ["no"]
)
changequote(, )dnl
brltty_item_unknown=true
brltty_item_length="`expr "${brltty_item}" : '[a-zA-Z0-9]*$'`"
if test ${brltty_item_length} -eq 2
then
   brltty_item_entry="`expr "${brltty_item_entries_$1}" : '.* \('"${brltty_item}"'-[^ ]*\)'`"
   if test -n "${brltty_item_entry}"
   then
      brltty_item_code_$1="${brltty_item}"
      brltty_item_name_$1="`expr "${brltty_item_entry}" : '[^-]*-\(.*\)$'`"
      brltty_item_unknown=false
   fi
elif test ${brltty_item_length} -gt 2
then
   brltty_item_entry="`expr "${brltty_item_entries_$1}" : '.* \([^- ]*-'"${brltty_item}"'[^ ]*\)'`"
   if test -z "${brltty_item_entry}"
   then
      brltty_lowercase="`echo "${brltty_item_entries_$1}" | sed 'y%ABCDEFGHIJKLMNOPQRSTUVWXYZ%abcdefghijklmnopqrstuvwxyz%'`"
      brltty_item_code="`expr "${brltty_lowercase}" : '.* \([^- ]*\)-'"${brltty_item}"`"
      if test -n "${brltty_item_code}"
      then
         brltty_item_entry="`expr "${brltty_item_entries_$1}" : '.* \('"${brltty_item_code}"'-[^ ]*\)'`"
      fi
   fi
   if test -n "${brltty_item_entry}"
   then
      brltty_item_code_$1="`expr "${brltty_item_entry}" : '\([^-]*\)'`"
      brltty_item_name_$1="`expr "${brltty_item_entry}" : '[^-]*-\(.*\)$'`"
      brltty_item_unknown=false
   fi
fi
changequote([, ])dnl
if "${brltty_item_unknown}"
then
   AC_MSG_ERROR([unknown $1 $2: ${brltty_item}])
fi
if test "${brltty_item_code_$1}" = "no"
then
   brltty_item_name_$1=""
fi
AC_SUBST([brltty_item_code_$1])
AC_SUBST([brltty_item_name_$1])
AC_SUBST([brltty_item_codes_$1])
AC_SUBST([brltty_item_names_$1])
AC_DEFINE_UNQUOTED(translit([$1_$2s], [a-z], [A-Z]), ["${brltty_item_codes_$1}"])])

AC_DEFUN([BRLTTY_ARG_DRIVER], [dnl
BRLTTY_ARG_ITEM([$1], [driver])
BRLTTY_SUMMARY_ITEM([$1-driver], [brltty_item_name_$1])
if test "${brltty_enabled_$1_support}" != "no"
then
   if test -n "${brltty_item_name_$1}"
   then
      $1_driver='$(BLD_TOP)Drivers/'"${brltty_item_name_$1}/$1.o"
      AC_DEFINE(translit([$1_builtin], [a-z], [A-Z]))
   fi
fi
if test "${brltty_standalone_programs}" != "yes"
then
   $1_drivers="$1-drivers"
   install_drivers="install-drivers"
fi
AC_SUBST([$1_driver])
AC_SUBST([$1_drivers])])

AC_DEFUN([BRLTTY_TEXT_TABLE], [dnl
define([brltty_tables_text], ifdef([brltty_tables_text], [brltty_tables_text])[
m4_text_wrap([$2], [                    ], [- m4_format([%-17s], [$1]) ], brltty_help_width)])])

AC_DEFUN([BRLTTY_ATTRIBUTES_TABLE], [dnl
define([brltty_tables_attributes], ifdef([brltty_tables_attributes], [brltty_tables_attributes])[
m4_text_wrap([$2], [                 ], [- m4_format([%-14s], [$1]) ], brltty_help_width)])])

AC_DEFUN([BRLTTY_SUMMARY_BEGIN], [dnl
brltty_summary_lines="
Options Summary:"])

AC_DEFUN([BRLTTY_SUMMARY_END], [dnl
AC_OUTPUT_COMMANDS([echo "${brltty_summary_lines}"], [brltty_summary_lines="${brltty_summary_lines}"])])

AC_DEFUN([BRLTTY_SUMMARY_ITEM], [dnl
brltty_summary_lines="${brltty_summary_lines}
   $1=${$2}"])

AC_DEFUN([BRLTTY_PORTABLE_DIRECTORY], [dnl
   BRLTTY_TOPLEVEL_DIRECTORY([$1], [$2], [prefix])])

AC_DEFUN([BRLTTY_ARCHITECTURE_DIRECTORY], [dnl
if test "${exec_prefix}" = "NONE"
then
   BRLTTY_TOPLEVEL_DIRECTORY([$1], [$2], [exec_prefix])
fi])

AC_DEFUN([BRLTTY_TOPLEVEL_DIRECTORY], [dnl
if test "${prefix}" = "NONE"
then
   if test -z "${execute_root}"
   then
changequote()dnl
      if test `expr "${$1} " : '\${$3}/[^/]*$'` -gt 0
changequote([, ])dnl
      then
         $1="`echo ${$1} | sed -e 's%/%$2/%'`"
      fi
   fi
fi])

AC_DEFUN([BRLTTY_EXECUTABLE_PATH], [dnl
changequote()dnl
if test `expr "${$1} " : '[^/ ][^/ ]*/'` -gt 0
changequote([, ])dnl
then
   $1="`pwd`/${$1}"
fi])
