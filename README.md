# ESP Power Guardian

Firmware libre para ESP32-S3 que supervisa un sistema de alimentación
ininterrumpida (SAI) mediante USB Host y publica su estado en la red local.

El proyecto está desarrollado para la placa ESP32-S3 Super Mini con LED RGB
WS2818 en GPIO48. La comunicación USB y la alimentación de la placa son
independientes: el SAI se conecta al USB nativo y el ESP32 debe recibir 5 V por
los pines `5V` y `GND` cuando el puerto del SAI no entrega alimentación.

## Funciones actuales

- USB Host en ESP32-S3 y controlador Qx/Cypress para `0665:5161`.
- Sondeo de estado `Q1` con una única transacción pendiente y cebado Cypress
  compatible mediante `QGS`, `QS`, `F` e `I` cuando el puente devuelve un eco.
- Caducidad de datos: un estado antiguo nunca se conserva como `OL` válido.
- Supervisor de frescura independiente, watchdog de la tarea USB y reinicio
  controlado ante consultas o controladores bloqueados.
- Estado de alimentación, tensiones, frecuencia, carga, batería y autonomía.
- Servidor NUT compatible con `aionut 4.3.4` y la integración oficial de Home
  Assistant.
- Interfaz web en español con Resumen, Datos técnicos, Ajustes e Información.
- Configuración Wi-Fi inicial mediante punto de acceso.
- Búsqueda asíncrona de redes Wi-Fi desde la propia interfaz.
- DHCP por defecto e IP fija opcional.
- AP de recuperación: se apaga tras conectar al Wi-Fi y reaparece si la conexión
  guardada no puede recuperarse.
- Actualizaciones OTA desde el navegador con reinicio automático y modo de
  mantenimiento para que el watchdog USB no interrumpa la escritura del firmware.
- Historial persistente de los diez últimos cortes eléctricos.
- Historial persistente e independiente de pérdidas, recuperaciones,
  desconexiones y reinicios de comunicación.
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
conectado o los datos Qx estén obsoletos, `ups.status` se publica como `OFF` y
las variables eléctricas antiguas dejan de anunciarse.

### Detección rápida de cortes

El ESP32 consulta el SAI cada segundo y el servidor NUT responde inmediatamente
con el estado vigente. Home Assistant, sin embargo, sondea por defecto la
integración NUT cada 60 segundos; por ello un corte breve puede empezar y acabar
entre dos actualizaciones sin quedar registrado.

Para recibir los cambios en unos dos segundos sin generar sondeo innecesario:

1. En **Ajustes → Dispositivos y servicios → NUT**, abre el menú de la entrada.
2. En **Opciones del sistema**, desactiva **Habilitar sondeo para
   actualizaciones**.
3. Crea una automatización que solicite la actualización de una entidad de esa
   integración cada dos segundos. Todas las entidades del mismo SAI comparten
   el coordinador NUT, por lo que actualizar `sensor.guardian_status` renueva en
   la misma consulta batería, tensiones, carga, frecuencia y diagnósticos:

```yaml
alias: SAI entrada - Actualización NUT cada 2 segundos
description: >-
  Actualiza cada 2 segundos la integración NUT correspondiente al ESP32-S3
  situado en la entrada. Aunque la acción utiliza únicamente la entidad
  sensor.guardian_status, todas las entidades de este SAI se actualizan en la
  misma consulta porque comparten el coordinador de datos de la integración NUT.
triggers:
  - trigger: time_pattern
    seconds: "/2"
conditions: []
actions:
  - action: homeassistant.update_entity
    target:
      entity_id: sensor.guardian_status
mode: single
```

El estado publicado es `OL` con alimentación de red, `OB DISCHRG` funcionando
con batería y `OB LB DISCHRG` cuando además la batería está baja. Para ejecutar
una acción solo si el corte persiste, utiliza un disparador de estado con
`for: "00:03:00"`; la recuperación a `OL` cancela automáticamente ese
temporizador.

## Estabilidad USB/Qx

Los Salicru SPS ONE que utilizan el puente Cypress `0665:5161` pueden bloquear
ocasionalmente el endpoint de entrada al entregar una trama mayor que los ocho
bytes declarados. El firmware trata explícitamente esta situación:

1. Consulta el estado `Q1` sin solapar peticiones. Si el informe de entrada es
   el eco del comando saliente, cierra esa lectura y rota consultas de cebado
   hasta que el puente entrega la trama Q1 pendiente.
2. Exige que la respuesta completa llegue dentro de 2,5 segundos.
3. Considera obsoletos los datos que superan cuatro segundos sin renovación. Un
   supervisor independiente aplica esta caducidad aunque la tarea USB se haya
   detenido, y NUT, web y LED incorporan además su propia barrera de frescura.
4. La recuperación del endpoint `0x81` no ejecuta operaciones síncronas que
   puedan bloquear la monitorización. Espera hasta quince segundos el callback
   pendiente del puente Cypress y solo reinicia de forma controlada si la
   transferencia continúa huérfana. Durante esa espera, los datos caducan a los
   cuatro segundos y web, NUT y LED dejan de publicar un estado antiguo.
5. Un watchdog separado vigila el latido de la tarea USB. Los datos se marcan
   como no fiables a los cuatro segundos, pero el reinicio completo solo se
   utiliza si la tarea no avanza durante treinta segundos.
6. Si se producen tres desbordamientos consecutivos o seis fallos seguidos,
   reinicia de forma controlada el ESP32 y, con ello, el controlador USB.
7. Limita a tres los reinicios consecutivos para evitar bucles de arranque. El
   contador solo vuelve a cero después de cinco minutos de comunicación
   continuada sin fallos; una lectura válida aislada no oculta un bloqueo
   recurrente.

La secuencia de escritura y lectura sigue el orden del subcontrolador Cypress
de NUT: primero `SET_REPORT` y, tras completarse, lectura del endpoint `0x81`.
Esto evita interpretar un informe saliente como una respuesta del SAI.

El LED solo utiliza el color de alimentación de red cuando existe una lectura
reciente. Durante una recuperación usa el estado de inicio y, si la
comunicación queda obsoleta, el estado configurado para SAI desconectado.

Un fallo de comunicación no se registra como corte eléctrico. Si el SAI estaba
funcionando con batería cuando se pierde la comunicación, el corte permanece
abierto hasta que una lectura válida confirme que volvió la red.

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
- `GET /api/communication-events`
- `GET /api/network`
- `GET /api/wifi/scan`
- `POST /api/wifi`
- `GET /api/led`
- `POST /api/led`
- `POST /api/led/test`
- `POST /api/ota`

La API no incorpora autenticación y debe utilizarse únicamente dentro de una
red local de confianza.

`GET /api/status` incluye, además de las medidas, la edad del último dato y del
último error, fallos consecutivos, recuperaciones del endpoint, reinicios
automáticos y último resultado de transferencia USB.

## Compatibilidad de SAI

La revisión actual prioriza la estabilidad del Salicru SPS ONE con
`0665:5161`, cuyo transporte es Qx/Megatec sobre un puente USB Cypress. La capa
de estado, NUT, API, LED y web está desacoplada del transporte para poder añadir
un controlador USB HID Power Device genérico en una revisión posterior sin
alterar las integraciones existentes. Un dispositivo HID desconocido se marca
como no compatible; nunca se presenta falsamente como conectado o `OL`.

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
