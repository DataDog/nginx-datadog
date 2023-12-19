.PHONY: all
all: delegation.svg auth.svg

%.svg: %.dot Makefile
	dot -Tsvg -o "$@" "$<"
