# Debian package names allow only a-z 0-9 + - . while versions also allow A-Z ~ : (and -).
# trainz_package_name_part(VERSION OUT) maps a version to a package-name part that never
# collides: lower-case letters, digits and '.' stay; every other allowed character becomes
# '+' and a code letter ('-' +m, '~' +t, '+' +p, ':' +c, an upper-case letter +u then the
# letter in lower case). '+' never appears otherwise, so two different versions always give
# two different names (e.g. 1~rc1 -> 1+trc1, 1-rc1 -> 1+mrc1). Characters a Debian version
# cannot hold are an error.
function(trainz_package_name_part version out)
  if(version STREQUAL "")
    message(FATAL_ERROR "the package version is empty")
  endif()
  string(LENGTH "${version}" n)
  math(EXPR last "${n} - 1")
  set(result "")
  foreach(i RANGE 0 ${last})
    string(SUBSTRING "${version}" ${i} 1 c)
    if(c MATCHES "^[a-z0-9.]$")
      string(APPEND result "${c}")
    elseif(c STREQUAL "-")
      string(APPEND result "+m")
    elseif(c STREQUAL "~")
      string(APPEND result "+t")
    elseif(c STREQUAL "+")
      string(APPEND result "+p")
    elseif(c STREQUAL ":")
      string(APPEND result "+c")
    elseif(c MATCHES "^[A-Z]$")
      string(TOLOWER "${c}" lower)
      string(APPEND result "+u${lower}")
    else()
      message(FATAL_ERROR "'${c}' cannot be in a Debian version (\"${version}\")")
    endif()
  endforeach()
  set(${out} "${result}" PARENT_SCOPE)
endfunction()
