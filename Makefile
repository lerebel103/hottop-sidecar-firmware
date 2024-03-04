
CODESIGN_CERTIFICATE_URL ?= https://ucueb9nr5d.execute-api.ap-southeast-2.amazonaws.com/dev/esp32-certificate
CODESIGN_FILE ?= certs/aws_codesign.crt

get-codesign-cert:
	@echo "Installing codesign certificate"
	mkdir certs || true
	./bin/get-codesign-cert.py ${CODESIGN_CERTIFICATE_URL} ${CODESIGN_FILE}
	@echo "Certificate installed to certs/aws_codesign.crt"

all: get-codesign-cert
	@echo "Building firmware"
