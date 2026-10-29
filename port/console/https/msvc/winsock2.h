/*
WINSOCK2.H (the Xbox consoles')

Empty: Mbed TLS includes it on a compiler that says _WIN32
(library/x509_crt.c, for inet_pton), and without its AF_INET6 uses its own
address parser, which is what the consoles want. On the include path of
port/console/https's units alone (tools/console_build.py,
console_https_flags with windows_crt).
*/
