all: viewer.js

viewer.js: ../m3d.h main.c
	emcc -s WASM=1 -s WASM_BIGINT -s EXPORTED_FUNCTIONS='["_m3d_viewer","_malloc","_free"]' -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' -s USE_WEBGL2=1 -s ALLOW_MEMORY_GROWTH=1 -s EXIT_RUNTIME=1 -I.. -I/usr/include/GL main.c -o viewer.js

clean:
	rm viewer.js viewer.wasm 2>/dev/null || true
