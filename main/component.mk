#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)
COMPONENT_SRCDIRS := . adapters servers
COMPONENT_ADD_INCLUDEDIRS := . adapters servers
COMPONENT_EMBED_TXTFILES := servers/index.html
