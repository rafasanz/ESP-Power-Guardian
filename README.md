# ESP Power Guardian

Firmware independiente para ESP32-S3 orientado a la supervisión de sistemas de
alimentación ininterrumpida.

## Objetivos

- Comunicación USB Host con SAI compatibles.
- Estado normalizado de red, batería, carga y autonomía.
- Servidor NUT en el puerto TCP 3493 para la integración oficial de Home Assistant.
- Interfaz web de administración.
- Configuración Wi-Fi y actualizaciones OTA.
- LED RGB WS2818 de la placa ESP32-S3 Super Mini, conectado a GPIO48.
- Colores de estado configurables.

## Estados del LED

| Estado | Color inicial |
| --- | --- |
| Iniciando | Azul |
| Alimentación de red | Verde |
| Funcionando con batería | Naranja |
| Batería baja | Rojo anaranjado |
| Alarma | Rojo |
| SAI desconectado | Gris |

Los colores y el brillo se almacenan en NVS y podrán modificarse desde
Administración → LED de estado.

## Compilación

El proyecto utiliza PlatformIO con ESP-IDF 5.3.1:

```shell
pio run
```

## Licencia

MIT. Copyright © 2026 Rafael Sanz.

