# ESP Power Guardian

Firmware libre para ESP32-S3 que supervisa un sistema de alimentación
ininterrumpida (SAI) mediante USB Host y publica su estado en la red local.

El proyecto está desarrollado para la placa ESP32-S3 Super Mini con LED RGB
WS2818 en GPIO48. La comunicación USB y la alimentación de la placa son
independientes: el SAI se conecta al USB nativo y el ESP32 debe recibir 5 V por
los pines `5V` y `GND` cuando el puerto del SAI no entrega alimentación.

## Funciones actuales

- USB Host en ESP32-S3 y consulta Qx del SAI.
- Estado de alimentación, tensiones, frecuencia, carga, batería y autonomía.
- Servidor NUT compatible con `aionut 4.3.4` y la integración oficial de Home
  Assistant.
- Interfaz web en español con Resumen, Datos técnicos, Ajustes e Información.
- Configuración Wi-Fi inicial mediante punto de acceso.
- Búsqueda asíncrona de redes Wi-Fi desde la propia interfaz.
- DHCP por defecto e IP fija opcional.
- AP de recuperación: se apaga tras conectar al Wi-Fi y reaparece si la conexión
  guardada no puede recuperarse.
- Actualizaciones OTA desde el navegador con reinicio automático.
- Historial persistente de los diez últimos cortes eléctricos.
- Servidor HTTP/JSON para consulta e integración local.
- LED RGB con colores, brillo y parpadeo configurables por estado.
- Apariencia configurable: modo, tema, tipografía, tamaño, espaciado y bordes.
- Servidor mDNS accesible como `esp-power-guardian.local` cuando la red lo
  permite.

## Primera configuración

1. Alimenta el ESP32-S3.
2. Conéctate al AP `ESP-Power-Guardian-XXXX`.
3. Abre `http://192.168.4.1`.
4. En **Ajustes**, busca y selecciona la red Wi-Fi, introduce su contraseña y
   conserva **DHCP (automática)**.
5. Guarda la configuración. El ESP32 se reiniciará y el router le asignará una
   dirección.
6. Accede mediante esa dirección o `http://esp-power-guardian.local`.
7. Opcionalmente, configura una IP fija. Dejar vacía la contraseña conserva la
   que ya está guardada.

El AP se desactiva cuando la conexión Wi-Fi funciona. Si el ESP32 no consigue
conectarse, el AP vuelve a estar disponible como mecanismo de recuperación.

## Home Assistant mediante NUT

Añade la integración oficial **Network UPS Tools (NUT)** con estos datos:

- Host: dirección IP del ESP Power Guardian.
- Puerto: `3493`.
- Usuario: vacío.
- Contraseña: vacía.

El servidor anuncia un SAI con el nombre `guardian` y publica información del
dispositivo, MAC, firmware y variables eléctricas. Mientras no haya un SAI
conectado, `ups.status` se publica como `OFF`.

## Estados del LED

| Estado | Color inicial | Comportamiento inicial |
| --- | --- | --- |
| Iniciando | Azul puro | Fijo |
| Alimentación de red | Verde | Fijo |
| Funcionando con batería | Naranja | Fijo |
| Batería baja | Rojo anaranjado | Parpadeo |
| Alarma | Rojo | Parpadeo |
| SAI desconectado | Gris | Fijo |

Los colores, el brillo y el parpadeo se guardan automáticamente en NVS. Cada
estado puede previsualizarse durante cinco segundos desde Ajustes.

## API HTTP

La interfaz **Información** documenta los endpoints disponibles:

- `GET /api/status`
- `GET /api/outages`
- `GET /api/network`
- `GET /api/wifi/scan`
- `POST /api/wifi`
- `GET /api/led`
- `POST /api/led`
- `POST /api/led/test`
- `POST /api/ota`

La API no incorpora autenticación y debe utilizarse únicamente dentro de una
red local de confianza.

## Compilación

El proyecto utiliza PlatformIO con ESP-IDF 5.3.1:

```shell
pio run
```

Para grabar una placa en modo BOOT:

```shell
pio run -t upload --upload-port COM3
```

Adapta el puerto al asignado por el sistema. Las compilaciones posteriores
pueden instalarse desde **Ajustes → Actualización OTA**.

## Estructura

- `main/`: firmware, USB/Qx, servidor NUT, Wi-Fi, almacenamiento, LED y web.
- `platformio.ini`: entorno de compilación y versión.
- `partitions.csv`: particiones factory, OTA y almacenamiento.
- `sdkconfig.defaults`: configuración base de ESP-IDF.

## Licencia

MIT. Copyright © 2026 Rafael Sanz.
