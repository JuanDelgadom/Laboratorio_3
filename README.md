# Laboratorio 3
Proyecto con implementaciones de un sistema publisher-subscriber en `TCP` y `UDP`, tanto en C como en Assembly.

## Carpetas
- `TCP`: sistema usando sockets TCP con broker, publisher y subscriber.
- `UDP`: sistema usando sockets UDP con broker, publisher y subscriber.
- `Capturas de Trafico`: Lugar donde se almacenaron las capturas de trafico realizadas con Wireshark. Aqui estan las pruebas usando C, como Assembly

## Archivos
- `*.c`: implementaciones en C.
- `*.s`: implementaciones en Assembly.
- `broker_udp_prueba.*`: versiones de prueba para simular perdida de paquetes y observar retransmision.
- `udp_common.h`: definiciones compartidas del protocolo UDP.
