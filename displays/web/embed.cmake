# Turns page.html into page.inc (a C array), so box-web is one file to run.
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" n)
math(EXPR bytes "${n} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
file(WRITE "${OUT}" "static const char page_html[] = {${hex}};\nstatic const size_t page_html_len = ${bytes};\n")
