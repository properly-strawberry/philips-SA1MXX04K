CXX := x86_64-w64-mingw32-g++-posix
FFMPEG ?= third_party/ffmpeg
CPPFLAGS := -I$(FFMPEG)/include -Igen/player
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -finput-charset=UTF-8
LDLIBS := -L$(FFMPEG)/lib -lavformat -lavcodec -lswresample -lavutil -lwinmm -lgdiplus -lshlwapi -lshell32 -lole32 -luuid
LDFLAGS := -municode -mwindows -static -static-libgcc -static-libstdc++

.PHONY: all assets package clean test-ui test-audio test-keys map-vmi
all: dist/philips.exe

gen/player/assets.h: tools/embed_assets.py tools/extract_host_ui.py gen/sb/03_hvmi.bin gen/sb/05_rsrc.bin gen/resources/inventory.tsv gen/resources/texts.tsv
	python3 -I tools/embed_assets.py

assets: gen/player/assets.h

map-vmi:
	python3 -I tools/map_vmi.py

gen/player/resources.o: src/player.rc ref/DEVICON.FIL
	mkdir -p gen/player
	x86_64-w64-mingw32-windres -i src/player.rc -o $@

SOURCES := src/main.cpp src/audio.cpp src/interface.cpp src/radio_config.cpp src/recorder.cpp
HEADERS := src/audio.h src/audio_buffer.h src/interface.h src/radio_config.h src/recorder.h src/equalizer.h src/unicode.h src/controls.h

gen/player/philips.exe: $(SOURCES) $(HEADERS) gen/player/assets.h gen/player/resources.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) gen/player/resources.o $(LDFLAGS) $(LDLIBS) -o $@

dist/philips.exe: gen/player/philips.exe
	mkdir -p dist
	cp $< $@

gen/ui-tests/interface_test: tests/interface_test.cpp src/interface.cpp src/radio_config.cpp $(HEADERS) gen/player/assets.h
	mkdir -p gen/ui-tests
	g++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Isrc -Igen/player tests/interface_test.cpp src/interface.cpp src/radio_config.cpp -o $@

test-ui: gen/ui-tests/interface_test
	gen/ui-tests/interface_test gen/ui-tests/fixture

gen/audio-tests/audio_buffer_test: tests/audio_buffer_test.cpp src/audio_buffer.h
	mkdir -p gen/audio-tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread -Isrc $< -o $@

test-audio: gen/audio-tests/audio_buffer_test
	gen/audio-tests/audio_buffer_test

gen/key-tests/key_test: tests/key_test.cpp src/interface.cpp src/radio_config.cpp $(HEADERS) gen/player/assets.h
	mkdir -p gen/key-tests
	g++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Isrc -Igen/player tests/key_test.cpp src/interface.cpp src/radio_config.cpp -o $@

test-keys: gen/key-tests/key_test
	gen/key-tests/key_test gen/key-tests/fixture

package: dist/philips.exe
	python3 -I tools/package.py

clean:
	rm -f dist/philips.exe
