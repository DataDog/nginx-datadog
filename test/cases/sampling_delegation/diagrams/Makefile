.PHONY: all
all: diagram.svg

%.svg: %.dot Makefile
	dot -Tsvg -o "$@" "$<"
