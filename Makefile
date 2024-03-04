
configure:
	rm -fr build || true
	cmake -DBUILD_STAGE=${STAGE} -DCMAKE_BUILD_TYPE=Release -S . -B build -G Ninja

build: configure
	cd build && ninja firmware

