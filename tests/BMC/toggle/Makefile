YOSYS ?= yosys
N ?= 4
.PHONY: export
export:
	mkdir -p build
	@python3 -c 'assert int("$(N)") >= 2, "N must be at least 2"'
	sed 's/^chparam -set N 4 /chparam -set N $(N) /' export.ys > build/export.ys
	$(YOSYS) -Q -T -l build/export.log -s build/export.ys

.PHONY: clean
clean:
	rm -rf build
